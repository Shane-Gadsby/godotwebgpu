/**************************************************************************/
/*  box3d_joint_3d.h                                                      */
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

#include "../misc/box3d_common.h"

#include "core/templates/self_list.h"
#include "servers/physics_3d/physics_server_3d.h"

class Box3DBody3D;
class Box3DSpace3D;

class Box3DJoint3D {
public:
	// Where the two ends of a joint attach, already converted into what Box3D wants.
	struct Frames {
		b3BodyId body_a = b3_nullBodyId;
		b3BodyId body_b = b3_nullBodyId;
		Transform3D frame_a;
		Transform3D frame_b;
	};

protected:
	SelfList<Box3DJoint3D> rebuild_element{ this };

	bool enabled = true;
	bool collision_disabled = false;

	int velocity_iterations = 0;
	int position_iterations = 0;

	b3JointId joint_id = b3_nullJointId;

	Box3DBody3D *body_a = nullptr;
	Box3DBody3D *body_b = nullptr;

	RID rid;

	Transform3D local_ref_a;
	Transform3D local_ref_b;

	// Creates the Box3D joint. Returns a null id if the joint could not be created.
	virtual b3JointId _create(b3WorldId p_world, const Frames &p_frames) { return b3_nullJointId; }
	virtual void _joint_created() {}

	static b3Transform _to_b3_frame(const Transform3D &p_frame);
	static void _fill_base(b3JointDef &r_base, const Frames &p_frames);

	void _wake_up_bodies();

	void _enabled_changed();

	String _bodies_to_string() const;

	void _report_dropped(const String &p_feature, const String &p_detail) const;
	void _report_default_only(const char *p_name, double p_value, double p_default) const;

public:
	Box3DJoint3D() = default;
	Box3DJoint3D(const Box3DJoint3D &p_old_joint, Box3DBody3D *p_body_a, Box3DBody3D *p_body_b, const Transform3D &p_local_ref_a, const Transform3D &p_local_ref_b);
	virtual ~Box3DJoint3D();

	virtual PhysicsServer3D::JointType get_type() const { return PhysicsServer3D::JOINT_TYPE_MAX; }

	RID get_rid() const { return rid; }
	void set_rid(const RID &p_rid) { rid = p_rid; }

	Box3DSpace3D *get_space() const;

	b3JointId get_joint_id() const { return joint_id; }

	bool is_enabled() const { return enabled; }
	void set_enabled(bool p_enabled);

	int get_solver_priority() const;
	void set_solver_priority(int p_priority);

	int get_solver_velocity_iterations() const { return velocity_iterations; }
	void set_solver_velocity_iterations(int p_iterations);

	int get_solver_position_iterations() const { return position_iterations; }
	void set_solver_position_iterations(int p_iterations);

	bool is_collision_disabled() const { return collision_disabled; }
	void set_collision_disabled(bool p_disabled);

	float get_applied_force() const;
	float get_applied_torque() const;

	void destroy();

	// Rebuilds the Box3D joint right away.
	void rebuild();

	// Rebuilds the Box3D joint before the next step. Godot configures joints one setting at a time, so this avoids
	// building (and reporting on) joints in the half configured states in between.
	void request_rebuild();
};

class Box3DPinJoint3D final : public Box3DJoint3D {
	virtual b3JointId _create(b3WorldId p_world, const Frames &p_frames) override;

public:
	Box3DPinJoint3D(const Box3DJoint3D &p_old_joint, Box3DBody3D *p_body_a, Box3DBody3D *p_body_b, const Vector3 &p_local_a, const Vector3 &p_local_b);

	virtual PhysicsServer3D::JointType get_type() const override { return PhysicsServer3D::JOINT_TYPE_PIN; }

	Vector3 get_local_a() const { return local_ref_a.origin; }
	void set_local_a(const Vector3 &p_local_a);

	Vector3 get_local_b() const { return local_ref_b.origin; }
	void set_local_b(const Vector3 &p_local_b);

	double get_param(PhysicsServer3D::PinJointParam p_param) const;
	void set_param(PhysicsServer3D::PinJointParam p_param, double p_value);
};

class Box3DHingeJoint3D final : public Box3DJoint3D {
	double limit_lower = 0.0;
	double limit_upper = 0.0;

	double motor_target_speed = 0.0;
	double motor_max_torque = 0.0;

	bool limits_enabled = false;
	bool motor_enabled = false;

	virtual b3JointId _create(b3WorldId p_world, const Frames &p_frames) override;

public:
	Box3DHingeJoint3D(const Box3DJoint3D &p_old_joint, Box3DBody3D *p_body_a, Box3DBody3D *p_body_b, const Transform3D &p_local_ref_a, const Transform3D &p_local_ref_b);

	virtual PhysicsServer3D::JointType get_type() const override { return PhysicsServer3D::JOINT_TYPE_HINGE; }

	double get_param(PhysicsServer3D::HingeJointParam p_param) const;
	void set_param(PhysicsServer3D::HingeJointParam p_param, double p_value);

	bool get_flag(PhysicsServer3D::HingeJointFlag p_flag) const;
	void set_flag(PhysicsServer3D::HingeJointFlag p_flag, bool p_enabled);
};

class Box3DSliderJoint3D final : public Box3DJoint3D {
	double limit_lower = 0.0;
	double limit_upper = 0.0;

	virtual b3JointId _create(b3WorldId p_world, const Frames &p_frames) override;

public:
	Box3DSliderJoint3D(const Box3DJoint3D &p_old_joint, Box3DBody3D *p_body_a, Box3DBody3D *p_body_b, const Transform3D &p_local_ref_a, const Transform3D &p_local_ref_b);

	virtual PhysicsServer3D::JointType get_type() const override { return PhysicsServer3D::JOINT_TYPE_SLIDER; }

	double get_param(PhysicsServer3D::SliderJointParam p_param) const;
	void set_param(PhysicsServer3D::SliderJointParam p_param, double p_value);
};

class Box3DConeTwistJoint3D final : public Box3DJoint3D {
	double swing_span = Math::PI / 4.0;
	double twist_span = Math::PI;

	virtual b3JointId _create(b3WorldId p_world, const Frames &p_frames) override;

public:
	Box3DConeTwistJoint3D(const Box3DJoint3D &p_old_joint, Box3DBody3D *p_body_a, Box3DBody3D *p_body_b, const Transform3D &p_local_ref_a, const Transform3D &p_local_ref_b);

	virtual PhysicsServer3D::JointType get_type() const override { return PhysicsServer3D::JOINT_TYPE_CONE_TWIST; }

	double get_param(PhysicsServer3D::ConeTwistJointParam p_param) const;
	void set_param(PhysicsServer3D::ConeTwistJointParam p_param, double p_value);
};

class Box3DGeneric6DOFJoint3D final : public Box3DJoint3D {
	enum {
		AXIS_LINEAR_X,
		AXIS_LINEAR_Y,
		AXIS_LINEAR_Z,
		AXIS_ANGULAR_X,
		AXIS_ANGULAR_Y,
		AXIS_ANGULAR_Z,
		AXIS_COUNT,
	};

	double limit_lower[AXIS_COUNT] = {};
	double limit_upper[AXIS_COUNT] = {};
	bool limit_enabled[AXIS_COUNT] = {};
	bool motor_enabled[AXIS_COUNT] = {};
	bool spring_enabled[AXIS_COUNT] = {};

	double motor_speed[AXIS_COUNT] = {};
	double motor_limit[AXIS_COUNT] = {};
	double spring_stiffness[AXIS_COUNT] = {};
	double spring_damping[AXIS_COUNT] = {};
	double spring_equilibrium[AXIS_COUNT] = {};

	virtual b3JointId _create(b3WorldId p_world, const Frames &p_frames) override;

	bool _is_locked(int p_axis) const { return limit_enabled[p_axis] && limit_lower[p_axis] == limit_upper[p_axis]; }
	bool _is_free(int p_axis) const { return !limit_enabled[p_axis] || limit_lower[p_axis] > limit_upper[p_axis]; }

public:
	Box3DGeneric6DOFJoint3D(const Box3DJoint3D &p_old_joint, Box3DBody3D *p_body_a, Box3DBody3D *p_body_b, const Transform3D &p_local_ref_a, const Transform3D &p_local_ref_b);

	virtual PhysicsServer3D::JointType get_type() const override { return PhysicsServer3D::JOINT_TYPE_6DOF; }

	double get_param(Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisParam p_param) const;
	void set_param(Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisParam p_param, double p_value);

	bool get_flag(Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisFlag p_flag) const;
	void set_flag(Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisFlag p_flag, bool p_enabled);
};
