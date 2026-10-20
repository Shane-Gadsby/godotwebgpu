/**************************************************************************/
/*  metahuman_rig.h                                                       */
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

#pragma once

#include "metahuman_dna.h"

#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "scene/3d/skeleton_modifier_3d.h"

namespace rl4 {
class RigInstance;
} // namespace rl4

// Evaluates a MetaHuman's body and face rigs with RigLogic and poses the skeleton.
//
// The body rig only adds correctives (RBF, twist and swing joints) driven by the pose animation
// gives the main joints. The face rig turns face controls into the ~800 face joints. Each rig
// writes only the joints that move at its current LOD; no rig moves a joint that animation drives.
class MetaHumanRig : public SkeletonModifier3D {
	GDCLASS(MetaHumanRig, SkeletonModifier3D);

public:
	enum ControlMode {
		// The raw expression controls (`CTRL_expressions.*`): what Unreal's face animation curves,
		// ARKit and Live Link drive.
		CONTROL_MODE_EXPRESSIONS,
		// The face board controls (`CTRL_L_brow_down.ty`, ...), mapped to expressions by the rig.
		CONTROL_MODE_FACE_BOARD,
	};

private:
	struct Driver {
		int bone = -1;
		uint16_t raw_control = 0;
		Quaternion neutral_inverse;
	};

	struct Rig {
		Ref<MetaHumanDNA> dna;
		rl4::RigInstance *instance = nullptr;
		LocalVector<int> joint_bones;
		LocalVector<Driver> drivers;
		// Per LOD: the joints that move at that LOD and exist in the skeleton.
		LocalVector<LocalVector<uint16_t>> lod_joints;
		LocalVector<float> last_inputs;
		int evaluated_lod = -1;
		int lod = -1;
		PackedFloat32Array lod_distances;
		// Where LOD distance is measured from: the bone nearest the center of the rig's meshes, which is
		// where their visibility ranges measure from (the center of their AABB).
		int lod_reference_bone = -1;
		bool bound = false;
	};

	Rig body;
	Rig face;
	ControlMode control_mode = CONTROL_MODE_EXPRESSIONS;

	// Face control values, by raw control index (expressions) and GUI control index (face board).
	LocalVector<float> expression_values;
	LocalVector<float> face_board_values;

	// Face controls the user can set, by control name. Driver inputs (`head.qx`, ...) are not among
	// them: the rig reads those from the skeleton.
	HashMap<StringName, int> expression_controls;
	HashMap<StringName, int> face_board_controls;
	LocalVector<StringName> expression_names;
	LocalVector<StringName> face_board_names;

	void _set_rig_dna(Rig &r_rig, const Ref<MetaHumanDNA> &p_dna);
	void _release_rig(Rig &r_rig);
	void _bind_rig(Rig &r_rig, Skeleton3D *p_skeleton, bool p_face);
	void _unbind_rigs();
	void _rebuild_control_names();
	int _pick_lod(const Rig &p_rig) const;
	void _evaluate_rig(Rig &r_rig, Skeleton3D *p_skeleton, bool p_face);
	void _controls_changed();

protected:
	static void _bind_methods();
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;
	bool _property_can_revert(const StringName &p_name) const;
	bool _property_get_revert(const StringName &p_name, Variant &r_property) const;
	void _validate_property(PropertyInfo &p_property) const;

	virtual void _skeleton_changed(Skeleton3D *p_old, Skeleton3D *p_new) override;
	virtual void _process_modification(double p_delta) override;

public:
	void set_body_dna(const Ref<MetaHumanDNA> &p_dna);
	Ref<MetaHumanDNA> get_body_dna() const;
	void set_face_dna(const Ref<MetaHumanDNA> &p_dna);
	Ref<MetaHumanDNA> get_face_dna() const;

	void set_control_mode(ControlMode p_mode);
	ControlMode get_control_mode() const;

	void set_body_lod(int p_lod);
	int get_body_lod() const;
	void set_face_lod(int p_lod);
	int get_face_lod() const;
	void set_body_lod_distances(const PackedFloat32Array &p_distances);
	PackedFloat32Array get_body_lod_distances() const;
	void set_face_lod_distances(const PackedFloat32Array &p_distances);
	PackedFloat32Array get_face_lod_distances() const;

	PackedStringArray get_control_names() const;
	void set_control(const StringName &p_name, float p_value);
	float get_control(const StringName &p_name) const;
	void reset_controls();

	int get_current_body_lod() const;
	int get_current_face_lod() const;

	~MetaHumanRig();
};

VARIANT_ENUM_CAST(MetaHumanRig::ControlMode);
