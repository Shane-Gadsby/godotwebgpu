/**************************************************************************/
/*  metahuman_dna.cpp                                                     */
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

#include "metahuman_dna.h"

#include "metahuman_riglogic.h"

#include "core/object/class_db.h"

namespace {

String _dna_string(const dna::StringView &p_view) {
	return String::utf8(p_view.data(), p_view.size());
}

} // namespace

PackedByteArray MetaHumanDNA::strip_geometry(const PackedByteArray &p_full_dna) {
	ERR_FAIL_COND_V(p_full_dna.is_empty(), PackedByteArray());

	auto in = rl4::makeScoped<rl4::MemoryStream>(p_full_dna.size());
	in->write(reinterpret_cast<const char *>(p_full_dna.ptr()), p_full_dna.size());
	in->seek(0);

	dna::Configuration config;
	config.layer = dna::DataLayer::RBFBehavior | dna::DataLayer::JointBehaviorMetadata | dna::DataLayer::TwistSwingBehavior | dna::DataLayer::MachineLearnedBehavior;
	config.unknownLayerPolicy = dna::UnknownLayerPolicy::Ignore;
	auto reader = rl4::makeScoped<rl4::BinaryStreamReader>(in.get(), config);
	reader->read();
	ERR_FAIL_COND_V_MSG(!rl4::Status::isOk(), PackedByteArray(), vformat("MetaHuman: could not read the DNA: %s", rl4::Status::get().message));

	auto out = rl4::makeScoped<rl4::MemoryStream>();
	auto writer = rl4::makeScoped<rl4::BinaryStreamWriter>(out.get());
	writer->setFrom(reader.get(), config.layer, dna::UnknownLayerPolicy::Ignore);
	writer->write();
	ERR_FAIL_COND_V_MSG(!rl4::Status::isOk(), PackedByteArray(), vformat("MetaHuman: could not write the DNA: %s", rl4::Status::get().message));

	PackedByteArray result;
	result.resize(out->size());
	out->seek(0);
	out->read(reinterpret_cast<char *>(result.ptrw()), result.size());
	return result;
}

void MetaHumanDNA::_clear() {
	if (riglogic) {
		rl4::RigLogic::destroy(riglogic);
		riglogic = nullptr;
	}
	if (reader) {
		rl4::BinaryStreamReader::destroy(reader);
		reader = nullptr;
	}
	load_attempted = false;
	joints.clear();
	gui_control_names.clear();
	raw_control_names.clear();
	animated_map_names.clear();
	lod_count = 0;
}

void MetaHumanDNA::_load() const {
	MetaHumanDNA *self = const_cast<MetaHumanDNA *>(this);
	if (load_attempted) {
		return;
	}
	self->load_attempted = true;
	if (data.is_empty()) {
		return;
	}

	auto stream = rl4::makeScoped<rl4::MemoryStream>(data.size());
	stream->write(reinterpret_cast<const char *>(data.ptr()), data.size());
	stream->seek(0);
	dna::BinaryStreamReader *r = rl4::BinaryStreamReader::create(stream.get());
	r->read();
	if (!rl4::Status::isOk()) {
		ERR_PRINT(vformat("MetaHuman: could not read the DNA in %s: %s", get_path(), rl4::Status::get().message));
		rl4::BinaryStreamReader::destroy(r);
		return;
	}

	rl4::Configuration config;
	config.rotationType = rl4::RotationType::Quaternions;
	config.floatingPointType = rl4::FloatingPointType::Float;
	// No MetaHuman so far uses blend shapes for the face, and the animated maps need region masks
	// that exports do not carry (webgpu_notes/TASKS.md §2.33).
	config.loadBlendShapes = false;
	config.loadAnimatedMaps = false;
	rl4::RigLogic *rl = rl4::RigLogic::create(r, config);
	if (!rl || !rl4::Status::isOk()) {
		ERR_PRINT(vformat("MetaHuman: RigLogic rejected the DNA in %s: %s", get_path(), rl4::Status::get().message));
		if (rl) {
			rl4::RigLogic::destroy(rl);
		}
		rl4::BinaryStreamReader::destroy(r);
		return;
	}
	self->reader = r;
	self->riglogic = rl;

	self->lod_count = r->getLODCount();
	const rl4::ConstArrayView<float> neutral = rl->getNeutralJointValues();
	const uint16_t joint_count = r->getJointCount();
	self->joints.resize(joint_count);
	for (uint16_t i = 0; i < joint_count; i++) {
		Joint &joint = self->joints.write[i];
		joint.name = _dna_string(r->getJointName(i));
		const uint16_t parent = r->getJointParentIndex(i);
		joint.parent = parent == i ? -1 : parent;
		const float *v = neutral.data() + i * MetaHumanRigLogic::JOINT_ATTRIBUTE_COUNT;
		joint.translation = Vector3(v[0], v[1], v[2]) * DNA_TO_GODOT_SCALE;
		joint.rotation = Quaternion(v[3], v[4], v[5], v[6]).normalized();
		joint.scale = Vector3(v[7], v[8], v[9]);
	}
	for (uint16_t i = 0; i < r->getGUIControlCount(); i++) {
		self->gui_control_names.push_back(_dna_string(r->getGUIControlName(i)));
	}
	for (uint16_t i = 0; i < r->getRawControlCount(); i++) {
		self->raw_control_names.push_back(_dna_string(r->getRawControlName(i)));
	}
	for (uint16_t i = 0; i < r->getAnimatedMapCount(); i++) {
		self->animated_map_names.push_back(_dna_string(r->getAnimatedMapName(i)));
	}
}

void MetaHumanDNA::set_data(const PackedByteArray &p_data) {
	_clear();
	data = p_data;
	emit_changed();
}

PackedByteArray MetaHumanDNA::get_data() const {
	return data;
}

bool MetaHumanDNA::is_valid() const {
	_load();
	return riglogic != nullptr;
}

rl4::RigLogic *MetaHumanDNA::get_riglogic() const {
	_load();
	return riglogic;
}

const dna::BinaryStreamReader *MetaHumanDNA::get_reader() const {
	_load();
	return reader;
}

const Vector<MetaHumanDNA::Joint> &MetaHumanDNA::get_joints() const {
	_load();
	return joints;
}

int MetaHumanDNA::get_joint_count() const {
	_load();
	return joints.size();
}

String MetaHumanDNA::get_joint_name(int p_joint) const {
	_load();
	ERR_FAIL_INDEX_V(p_joint, joints.size(), String());
	return joints[p_joint].name;
}

int MetaHumanDNA::get_lod_count() const {
	_load();
	return lod_count;
}

PackedStringArray MetaHumanDNA::get_gui_control_names() const {
	_load();
	return gui_control_names;
}

PackedStringArray MetaHumanDNA::get_raw_control_names() const {
	_load();
	return raw_control_names;
}

PackedStringArray MetaHumanDNA::get_animated_map_names() const {
	_load();
	return animated_map_names;
}

void MetaHumanDNA::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_data", "data"), &MetaHumanDNA::set_data);
	ClassDB::bind_method(D_METHOD("get_data"), &MetaHumanDNA::get_data);
	ClassDB::bind_method(D_METHOD("is_valid"), &MetaHumanDNA::is_valid);
	ClassDB::bind_method(D_METHOD("get_joint_count"), &MetaHumanDNA::get_joint_count);
	ClassDB::bind_method(D_METHOD("get_joint_name", "joint"), &MetaHumanDNA::get_joint_name);
	ClassDB::bind_method(D_METHOD("get_lod_count"), &MetaHumanDNA::get_lod_count);
	ClassDB::bind_method(D_METHOD("get_gui_control_names"), &MetaHumanDNA::get_gui_control_names);
	ClassDB::bind_method(D_METHOD("get_raw_control_names"), &MetaHumanDNA::get_raw_control_names);
	ClassDB::bind_method(D_METHOD("get_animated_map_names"), &MetaHumanDNA::get_animated_map_names);
	ClassDB::bind_static_method("MetaHumanDNA", D_METHOD("strip_geometry", "full_dna"), &MetaHumanDNA::strip_geometry);

	ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_data", "get_data");
}

MetaHumanDNA::~MetaHumanDNA() {
	_clear();
}
