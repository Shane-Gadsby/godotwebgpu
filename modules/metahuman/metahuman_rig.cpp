/**************************************************************************/
/*  metahuman_rig.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "metahuman_rig.h"

#include "metahuman_riglogic.h"

#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/main/viewport.h"

namespace {

const String EXPRESSIONS_PREFIX = "expressions/";
const String FACE_BOARD_PREFIX = "face_board/";

// A raw control the rig reads from the skeleton: `<joint>.qx` .. `.qw`, the joint's rotation
// relative to its neutral. Returns the component (0-3), or -1 for a user control.
int _driver_component(const String &p_raw_name, String &r_joint) {
	const int dot = p_raw_name.rfind_char('.');
	if (dot < 0 || dot + 3 != p_raw_name.length() || p_raw_name[dot + 1] != 'q') {
		return -1;
	}
	const char32_t c = p_raw_name[dot + 2];
	const int component = c == 'x' ? 0 : c == 'y' ? 1
			: c == 'z'							  ? 2
			: c == 'w'							  ? 3
												  : -1;
	if (component >= 0) {
		r_joint = p_raw_name.substr(0, dot);
	}
	return component;
}

} // namespace

void MetaHumanRig::_release_rig(Rig &r_rig) {
	if (r_rig.instance) {
		rl4::RigInstance::destroy(r_rig.instance);
		r_rig.instance = nullptr;
	}
	r_rig.joint_bones.clear();
	r_rig.drivers.clear();
	r_rig.lod_joints.clear();
	r_rig.last_inputs.clear();
	r_rig.evaluated_lod = -1;
	r_rig.bound = false;
}

void MetaHumanRig::_set_rig_dna(Rig &r_rig, const Ref<MetaHumanDNA> &p_dna) {
	_release_rig(r_rig);
	r_rig.dna = p_dna;
	if (p_dna.is_valid() && p_dna->is_valid()) {
		r_rig.instance = rl4::RigInstance::create(p_dna->get_riglogic());
	}
}

void MetaHumanRig::_bind_rig(Rig &r_rig, Skeleton3D *p_skeleton, bool p_face) {
	r_rig.bound = true;
	if (!r_rig.instance) {
		return;
	}
	const Vector<MetaHumanDNA::Joint> &joints = r_rig.dna->get_joints();
	r_rig.joint_bones.resize(joints.size());
	for (int i = 0; i < joints.size(); i++) {
		r_rig.joint_bones[i] = p_skeleton->find_bone(joints[i].name);
	}
	r_rig.lod_reference_bone = p_skeleton->find_bone(p_face ? "head" : "pelvis");

	// Drivers: one per joint with `.qx` .. `.qw` raw controls.
	const PackedStringArray raw_names = r_rig.dna->get_raw_control_names();
	HashMap<String, int> driver_by_joint;
	r_rig.drivers.clear();
	for (int i = 0; i < raw_names.size(); i++) {
		String joint_name;
		const int component = _driver_component(raw_names[i], joint_name);
		if (component < 0) {
			continue;
		}
		HashMap<String, int>::Iterator E = driver_by_joint.find(joint_name);
		if (!E) {
			int joint = -1;
			for (int j = 0; j < joints.size(); j++) {
				if (joints[j].name == joint_name) {
					joint = j;
					break;
				}
			}
			Driver driver;
			driver.bone = p_skeleton->find_bone(joint_name);
			if (joint >= 0) {
				driver.neutral_inverse = joints[joint].rotation.inverse();
			}
			E = driver_by_joint.insert(joint_name, r_rig.drivers.size());
			r_rig.drivers.push_back(driver);
		}
		// The four components are consecutive raw controls in every DNA seen so far; keep the index
		// of the first one and check the rest.
		Driver &driver = r_rig.drivers[E->value];
		if (component == 0) {
			driver.raw_control = i;
		} else if (i != driver.raw_control + component) {
			WARN_PRINT(vformat("MetaHuman: driver controls of '%s' are not consecutive; its correctives will be wrong.", joint_name));
		}
	}

	const rl4::RigLogic *rl = r_rig.dna->get_riglogic();
	const int lod_count = MAX(1, r_rig.dna->get_lod_count());
	r_rig.lod_joints.resize(lod_count);
	for (int lod = 0; lod < lod_count; lod++) {
		LocalVector<uint16_t> &list = r_rig.lod_joints[lod];
		list.clear();
		const rl4::ConstArrayView<uint16_t> attributes = rl->getJointVariableAttributeIndices(lod);
		int last = -1;
		for (uint16_t attribute : attributes) {
			const int joint = attribute / MetaHumanRigLogic::JOINT_ATTRIBUTE_COUNT;
			if (joint == last) {
				continue;
			}
			last = joint;
			if (joint < (int)r_rig.joint_bones.size() && r_rig.joint_bones[joint] >= 0) {
				list.push_back(joint);
			}
		}
	}
	r_rig.last_inputs.clear();
	r_rig.evaluated_lod = -1;
}

void MetaHumanRig::_unbind_rigs() {
	body.bound = false;
	face.bound = false;
}

void MetaHumanRig::_rebuild_control_names() {
	expression_controls.clear();
	face_board_controls.clear();
	expression_names.clear();
	face_board_names.clear();
	expression_values.clear();
	face_board_values.clear();
	if (face.dna.is_null() || !face.dna->is_valid()) {
		notify_property_list_changed();
		return;
	}
	const PackedStringArray raw_names = face.dna->get_raw_control_names();
	expression_values.resize(raw_names.size());
	for (int i = 0; i < raw_names.size(); i++) {
		expression_values[i] = 0.0;
		String joint_name;
		if (_driver_component(raw_names[i], joint_name) >= 0) {
			continue;
		}
		const StringName name = raw_names[i];
		expression_controls.insert(name, i);
		expression_names.push_back(name);
	}
	const PackedStringArray gui_names = face.dna->get_gui_control_names();
	face_board_values.resize(gui_names.size());
	for (int i = 0; i < gui_names.size(); i++) {
		face_board_values[i] = 0.0;
		const StringName name = gui_names[i];
		face_board_controls.insert(name, i);
		face_board_names.push_back(name);
	}
	notify_property_list_changed();
}

int MetaHumanRig::_pick_lod(const Rig &p_rig) const {
	const int lod_count = p_rig.lod_joints.size();
	if (lod_count <= 1) {
		return 0;
	}
	if (p_rig.lod >= 0) {
		return MIN(p_rig.lod, lod_count - 1);
	}
	if (p_rig.lod_distances.is_empty() || !is_inside_tree()) {
		return 0;
	}
	const Camera3D *camera = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
	if (!camera) {
		return 0;
	}
	const Skeleton3D *skeleton = get_skeleton();
	Vector3 origin = skeleton ? skeleton->get_global_position() : get_global_position();
	if (skeleton && p_rig.lod_reference_bone >= 0) {
		origin = skeleton->get_global_transform().xform(skeleton->get_bone_global_pose(p_rig.lod_reference_bone).origin);
	}
	const real_t distance = camera->get_global_position().distance_to(origin);
	for (int i = 0; i < p_rig.lod_distances.size() && i < lod_count; i++) {
		if (distance < p_rig.lod_distances[i]) {
			return i;
		}
	}
	return MIN(p_rig.lod_distances.size(), lod_count - 1);
}

void MetaHumanRig::_evaluate_rig(Rig &r_rig, Skeleton3D *p_skeleton, bool p_face) {
	if (!r_rig.instance) {
		return;
	}
	if (!r_rig.bound) {
		_bind_rig(r_rig, p_skeleton, p_face);
	}
	const rl4::RigLogic *rl = r_rig.dna->get_riglogic();
	rl4::RigInstance *instance = r_rig.instance;

	const int lod = _pick_lod(r_rig);
	instance->setLOD(lod);

	rl4::ArrayView<float> raw = instance->getRawControlValues();
	if (p_face) {
		if (control_mode == CONTROL_MODE_FACE_BOARD) {
			rl4::ArrayView<float> gui = instance->getGUIControlValues();
			const uint32_t count = MIN((uint32_t)gui.size(), face_board_values.size());
			for (uint32_t i = 0; i < count; i++) {
				gui[i] = face_board_values[i];
			}
			rl->mapGUIToRawControls(instance);
		} else {
			const uint32_t count = MIN((uint32_t)raw.size(), expression_values.size());
			for (uint32_t i = 0; i < count; i++) {
				raw[i] = expression_values[i];
			}
		}
	}
	for (const Driver &driver : r_rig.drivers) {
		Quaternion q;
		if (driver.bone >= 0) {
			q = driver.neutral_inverse * p_skeleton->get_bone_pose_rotation(driver.bone);
		}
		// The solvers' poses all have w >= 0; q and -q are the same rotation but not the same input.
		if (q.w < 0) {
			q = -q;
		}
		if (driver.raw_control + 3u < raw.size()) {
			raw[driver.raw_control] = q.x;
			raw[driver.raw_control + 1] = q.y;
			raw[driver.raw_control + 2] = q.z;
			raw[driver.raw_control + 3] = q.w;
		}
	}

	// An idle face (or a body holding still) gives the same outputs; skip the evaluation.
	bool changed = lod != r_rig.evaluated_lod || r_rig.last_inputs.size() != raw.size();
	if (!changed) {
		for (uint32_t i = 0; i < raw.size(); i++) {
			if (r_rig.last_inputs[i] != raw[i]) {
				changed = true;
				break;
			}
		}
	}
	if (changed) {
		r_rig.last_inputs.resize(raw.size());
		for (uint32_t i = 0; i < raw.size(); i++) {
			r_rig.last_inputs[i] = raw[i];
		}
		rl->calculate(instance);
		r_rig.evaluated_lod = lod;
	}

	// Poses are restored after every skeleton update, so the joints are written every time.
	const rl4::ConstArrayView<float> deltas = instance->getJointOutputs();
	const Vector<MetaHumanDNA::Joint> &joints = r_rig.dna->get_joints();
	for (uint16_t joint : r_rig.lod_joints[lod]) {
		const float *d = deltas.data() + joint * MetaHumanRigLogic::JOINT_ATTRIBUTE_COUNT;
		const MetaHumanDNA::Joint &neutral = joints[joint];
		const int bone = r_rig.joint_bones[joint];
		p_skeleton->set_bone_pose_position(bone, neutral.translation + Vector3(d[0], d[1], d[2]) * MetaHumanDNA::DNA_TO_GODOT_SCALE);
		p_skeleton->set_bone_pose_rotation(bone, (neutral.rotation * Quaternion(d[3], d[4], d[5], d[6])).normalized());
		p_skeleton->set_bone_pose_scale(bone, neutral.scale + Vector3(d[7], d[8], d[9]));
	}
}

void MetaHumanRig::_skeleton_changed(Skeleton3D *p_old, Skeleton3D *p_new) {
	_unbind_rigs();
}

void MetaHumanRig::_process_modification(double p_delta) {
	Skeleton3D *skeleton = get_skeleton();
	if (!skeleton) {
		return;
	}
	// Body first: the face reads the neck and head the body's animation posed.
	_evaluate_rig(body, skeleton, false);
	_evaluate_rig(face, skeleton, true);
}

void MetaHumanRig::_controls_changed() {
	Skeleton3D *skeleton = get_skeleton();
	if (skeleton) {
		skeleton->force_update_deferred();
	}
}

void MetaHumanRig::set_body_dna(const Ref<MetaHumanDNA> &p_dna) {
	_set_rig_dna(body, p_dna);
	update_configuration_warnings();
}

Ref<MetaHumanDNA> MetaHumanRig::get_body_dna() const {
	return body.dna;
}

void MetaHumanRig::set_face_dna(const Ref<MetaHumanDNA> &p_dna) {
	_set_rig_dna(face, p_dna);
	_rebuild_control_names();
	update_configuration_warnings();
}

Ref<MetaHumanDNA> MetaHumanRig::get_face_dna() const {
	return face.dna;
}

void MetaHumanRig::set_control_mode(ControlMode p_mode) {
	control_mode = p_mode;
	notify_property_list_changed();
	_controls_changed();
}

MetaHumanRig::ControlMode MetaHumanRig::get_control_mode() const {
	return control_mode;
}

void MetaHumanRig::set_body_lod(int p_lod) {
	body.lod = p_lod;
}

int MetaHumanRig::get_body_lod() const {
	return body.lod;
}

void MetaHumanRig::set_face_lod(int p_lod) {
	face.lod = p_lod;
}

int MetaHumanRig::get_face_lod() const {
	return face.lod;
}

void MetaHumanRig::set_body_lod_distances(const PackedFloat32Array &p_distances) {
	body.lod_distances = p_distances;
}

PackedFloat32Array MetaHumanRig::get_body_lod_distances() const {
	return body.lod_distances;
}

void MetaHumanRig::set_face_lod_distances(const PackedFloat32Array &p_distances) {
	face.lod_distances = p_distances;
}

PackedFloat32Array MetaHumanRig::get_face_lod_distances() const {
	return face.lod_distances;
}

int MetaHumanRig::get_current_body_lod() const {
	return body.evaluated_lod;
}

int MetaHumanRig::get_current_face_lod() const {
	return face.evaluated_lod;
}

PackedStringArray MetaHumanRig::get_control_names() const {
	PackedStringArray names;
	const LocalVector<StringName> &list = control_mode == CONTROL_MODE_FACE_BOARD ? face_board_names : expression_names;
	for (const StringName &name : list) {
		names.push_back(name);
	}
	return names;
}

void MetaHumanRig::set_control(const StringName &p_name, float p_value) {
	if (const int *index = expression_controls.getptr(p_name)) {
		expression_values[*index] = p_value;
	} else if (const int *gui_index = face_board_controls.getptr(p_name)) {
		face_board_values[*gui_index] = p_value;
	} else {
		ERR_FAIL_MSG(vformat("MetaHuman: no face control named '%s'.", p_name));
	}
	_controls_changed();
}

float MetaHumanRig::get_control(const StringName &p_name) const {
	if (const int *index = expression_controls.getptr(p_name)) {
		return expression_values[*index];
	}
	if (const int *gui_index = face_board_controls.getptr(p_name)) {
		return face_board_values[*gui_index];
	}
	ERR_FAIL_V_MSG(0.0, vformat("MetaHuman: no face control named '%s'.", p_name));
}

void MetaHumanRig::reset_controls() {
	for (float &value : expression_values) {
		value = 0.0;
	}
	for (float &value : face_board_values) {
		value = 0.0;
	}
	_controls_changed();
}

bool MetaHumanRig::_set(const StringName &p_name, const Variant &p_value) {
	const String name = p_name;
	if (name.begins_with(EXPRESSIONS_PREFIX)) {
		if (const int *index = expression_controls.getptr(StringName(name.substr(EXPRESSIONS_PREFIX.length())))) {
			expression_values[*index] = p_value;
			_controls_changed();
			return true;
		}
	} else if (name.begins_with(FACE_BOARD_PREFIX)) {
		if (const int *index = face_board_controls.getptr(StringName(name.substr(FACE_BOARD_PREFIX.length())))) {
			face_board_values[*index] = p_value;
			_controls_changed();
			return true;
		}
	}
	return false;
}

bool MetaHumanRig::_get(const StringName &p_name, Variant &r_ret) const {
	const String name = p_name;
	if (name.begins_with(EXPRESSIONS_PREFIX)) {
		if (const int *index = expression_controls.getptr(StringName(name.substr(EXPRESSIONS_PREFIX.length())))) {
			r_ret = expression_values[*index];
			return true;
		}
	} else if (name.begins_with(FACE_BOARD_PREFIX)) {
		if (const int *index = face_board_controls.getptr(StringName(name.substr(FACE_BOARD_PREFIX.length())))) {
			r_ret = face_board_values[*index];
			return true;
		}
	}
	return false;
}

void MetaHumanRig::_get_property_list(List<PropertyInfo> *p_list) const {
	// Both sets are listed so animations keying either keep working when the mode changes; only the
	// active one is shown in the inspector.
	const uint32_t expressions_usage = control_mode == CONTROL_MODE_EXPRESSIONS ? PROPERTY_USAGE_DEFAULT : PROPERTY_USAGE_STORAGE;
	const uint32_t face_board_usage = control_mode == CONTROL_MODE_FACE_BOARD ? PROPERTY_USAGE_DEFAULT : PROPERTY_USAGE_STORAGE;
	for (const StringName &name : expression_names) {
		p_list->push_back(PropertyInfo(Variant::FLOAT, EXPRESSIONS_PREFIX + String(name), PROPERTY_HINT_RANGE, "0,1,0.001", expressions_usage));
	}
	for (const StringName &name : face_board_names) {
		p_list->push_back(PropertyInfo(Variant::FLOAT, FACE_BOARD_PREFIX + String(name), PROPERTY_HINT_RANGE, "-1,1,0.001", face_board_usage));
	}
}

bool MetaHumanRig::_property_can_revert(const StringName &p_name) const {
	const String name = p_name;
	return name.begins_with(EXPRESSIONS_PREFIX) || name.begins_with(FACE_BOARD_PREFIX);
}

bool MetaHumanRig::_property_get_revert(const StringName &p_name, Variant &r_property) const {
	if (!_property_can_revert(p_name)) {
		return false;
	}
	r_property = 0.0;
	return true;
}

void MetaHumanRig::_validate_property(PropertyInfo &p_property) const {
	if ((p_property.name == "body_lod_distances" && body.lod >= 0) || (p_property.name == "face_lod_distances" && face.lod >= 0)) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

void MetaHumanRig::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_body_dna", "dna"), &MetaHumanRig::set_body_dna);
	ClassDB::bind_method(D_METHOD("get_body_dna"), &MetaHumanRig::get_body_dna);
	ClassDB::bind_method(D_METHOD("set_face_dna", "dna"), &MetaHumanRig::set_face_dna);
	ClassDB::bind_method(D_METHOD("get_face_dna"), &MetaHumanRig::get_face_dna);
	ClassDB::bind_method(D_METHOD("set_control_mode", "mode"), &MetaHumanRig::set_control_mode);
	ClassDB::bind_method(D_METHOD("get_control_mode"), &MetaHumanRig::get_control_mode);
	ClassDB::bind_method(D_METHOD("set_body_lod", "lod"), &MetaHumanRig::set_body_lod);
	ClassDB::bind_method(D_METHOD("get_body_lod"), &MetaHumanRig::get_body_lod);
	ClassDB::bind_method(D_METHOD("set_face_lod", "lod"), &MetaHumanRig::set_face_lod);
	ClassDB::bind_method(D_METHOD("get_face_lod"), &MetaHumanRig::get_face_lod);
	ClassDB::bind_method(D_METHOD("set_body_lod_distances", "distances"), &MetaHumanRig::set_body_lod_distances);
	ClassDB::bind_method(D_METHOD("get_body_lod_distances"), &MetaHumanRig::get_body_lod_distances);
	ClassDB::bind_method(D_METHOD("set_face_lod_distances", "distances"), &MetaHumanRig::set_face_lod_distances);
	ClassDB::bind_method(D_METHOD("get_face_lod_distances"), &MetaHumanRig::get_face_lod_distances);
	ClassDB::bind_method(D_METHOD("get_control_names"), &MetaHumanRig::get_control_names);
	ClassDB::bind_method(D_METHOD("set_control", "name", "value"), &MetaHumanRig::set_control);
	ClassDB::bind_method(D_METHOD("get_control", "name"), &MetaHumanRig::get_control);
	ClassDB::bind_method(D_METHOD("reset_controls"), &MetaHumanRig::reset_controls);
	ClassDB::bind_method(D_METHOD("get_current_body_lod"), &MetaHumanRig::get_current_body_lod);
	ClassDB::bind_method(D_METHOD("get_current_face_lod"), &MetaHumanRig::get_current_face_lod);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "body_dna", PROPERTY_HINT_RESOURCE_TYPE, MetaHumanDNA::get_class_static()), "set_body_dna", "get_body_dna");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "face_dna", PROPERTY_HINT_RESOURCE_TYPE, MetaHumanDNA::get_class_static()), "set_face_dna", "get_face_dna");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "control_mode", PROPERTY_HINT_ENUM, "Expressions,Face Board"), "set_control_mode", "get_control_mode");
	ADD_GROUP("LOD", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "body_lod", PROPERTY_HINT_RANGE, "-1,7,1"), "set_body_lod", "get_body_lod");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "body_lod_distances", PROPERTY_HINT_NONE, "suffix:m"), "set_body_lod_distances", "get_body_lod_distances");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "face_lod", PROPERTY_HINT_RANGE, "-1,7,1"), "set_face_lod", "get_face_lod");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "face_lod_distances", PROPERTY_HINT_NONE, "suffix:m"), "set_face_lod_distances", "get_face_lod_distances");

	BIND_ENUM_CONSTANT(CONTROL_MODE_EXPRESSIONS);
	BIND_ENUM_CONSTANT(CONTROL_MODE_FACE_BOARD);
}

MetaHumanRig::~MetaHumanRig() {
	_release_rig(body);
	_release_rig(face);
}
