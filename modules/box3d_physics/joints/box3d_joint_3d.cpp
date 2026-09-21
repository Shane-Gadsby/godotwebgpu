/**************************************************************************/
/*  box3d_joint_3d.cpp                                                    */
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

#include "box3d_joint_3d.h"

#include "../box3d_physics_server_3d.h"
#include "../misc/box3d_diagnostics.h"
#include "../objects/box3d_body_3d.h"
#include "../spaces/box3d_space_3d.h"

#include "core/config/engine.h"

namespace {

constexpr int JOINT_DEFAULT_SOLVER_PRIORITY = 1;

constexpr double PIN_DEFAULT_BIAS = 0.3;
constexpr double PIN_DEFAULT_DAMPING = 1.0;
constexpr double PIN_DEFAULT_IMPULSE_CLAMP = 0.0;

constexpr double HINGE_DEFAULT_BIAS = 0.3;
constexpr double HINGE_DEFAULT_LIMIT_BIAS = 0.3;
constexpr double HINGE_DEFAULT_SOFTNESS = 0.9;
constexpr double HINGE_DEFAULT_RELAXATION = 1.0;

constexpr double SLIDER_DEFAULTS[PhysicsServer3D::SLIDER_JOINT_MAX] = {
	0.0,
	0.0, // Linear limit upper and lower are stored on the joint itself.
	1.0,
	0.7,
	1.0, // Linear limit softness, restitution, damping
	1.0,
	0.7,
	0.0, // Linear motion softness, restitution, damping
	1.0,
	0.7,
	1.0, // Linear orthogonal softness, restitution, damping
	0.0,
	0.0, // Angular limit upper and lower
	1.0,
	0.7,
	0.0, // Angular limit softness, restitution, damping
	1.0,
	0.7,
	1.0, // Angular motion softness, restitution, damping
	1.0,
	0.7,
	1.0, // Angular orthogonal softness, restitution, damping
};

constexpr double CONE_TWIST_DEFAULT_BIAS = 0.3;
constexpr double CONE_TWIST_DEFAULT_SOFTNESS = 0.8;
constexpr double CONE_TWIST_DEFAULT_RELAXATION = 1.0;

constexpr float MAX_LIMIT_ANGLE = 0.99f * B3_PI;

double estimate_physics_step() {
	Engine *engine = Engine::get_singleton();

	const double step = 1.0 / engine->get_user_physics_ticks_per_second();
	const double step_scaled = step * engine->get_effective_time_scale();

	return step_scaled;
}

// Rotates a joint frame so that the axis with index `p_axis` becomes its z-axis, which is the axis Box3D joints act around.
Transform3D _with_axis_as_z(const Transform3D &p_frame, int p_axis) {
	const Vector3 x = p_frame.basis.get_column(0);
	const Vector3 y = p_frame.basis.get_column(1);
	const Vector3 z = p_frame.basis.get_column(2);

	switch (p_axis) {
		case 0: {
			return Transform3D(Basis(y, z, x), p_frame.origin);
		}
		case 1: {
			return Transform3D(Basis(z, x, y), p_frame.origin);
		}
		default: {
			return p_frame;
		}
	}
}

// Rotates a joint frame so that the axis with index `p_axis` becomes its x-axis, which is the axis Box3D prismatic joints slide along.
Transform3D _with_axis_as_x(const Transform3D &p_frame, int p_axis) {
	const Vector3 x = p_frame.basis.get_column(0);
	const Vector3 y = p_frame.basis.get_column(1);
	const Vector3 z = p_frame.basis.get_column(2);

	switch (p_axis) {
		case 1: {
			return Transform3D(Basis(y, z, x), p_frame.origin);
		}
		case 2: {
			return Transform3D(Basis(z, x, y), p_frame.origin);
		}
		default: {
			return p_frame;
		}
	}
}

} // namespace

/* Base */

b3Transform Box3DJoint3D::_to_b3_frame(const Transform3D &p_frame) {
	Basis basis = p_frame.basis;
	Vector3 scale;
	Box3DMath::decompose(basis, scale);

	return b3Transform{ to_b3(p_frame.origin), to_b3(basis) };
}

void Box3DJoint3D::_fill_base(b3JointDef &r_base, const Frames &p_frames) {
	r_base.bodyIdA = p_frames.body_a;
	r_base.bodyIdB = p_frames.body_b;
	r_base.localFrameA = _to_b3_frame(p_frames.frame_a);
	r_base.localFrameB = _to_b3_frame(p_frames.frame_b);
	// Whether the connected bodies collide is decided by the collision exceptions instead, to match Godot.
	r_base.collideConnected = true;
}

void Box3DJoint3D::_wake_up_bodies() {
	if (body_a != nullptr) {
		body_a->wake_up();
	}

	if (body_b != nullptr) {
		body_b->wake_up();
	}
}

void Box3DJoint3D::_enabled_changed() {
	rebuild();
	_wake_up_bodies();
}

String Box3DJoint3D::_bodies_to_string() const {
	return vformat("'%s' and '%s'", body_a != nullptr ? body_a->to_string() : "<World>", body_b != nullptr ? body_b->to_string() : "<World>");
}

void Box3DJoint3D::_report_dropped(const String &p_feature, const String &p_detail) const {
	BOX3D_UNSUPPORTED_KEYED(p_feature,
			vformat("A %s joint was configured with a setting that Box3D joints cannot express.", PhysicsServer3D::JointType(get_type()) == PhysicsServer3D::JOINT_TYPE_6DOF ? "6DOF" : "physics"),
			"Box3D only has pin (spherical), hinge (revolute), slider (prismatic), weld and wheel joint types, so this setting has no equivalent.",
			vformat("joint=%s type=%d bodies=%s %s", rid, (int)get_type(), _bodies_to_string(), p_detail),
			vformat("joint_%s_%d", p_feature, (int)get_type()));
}

void Box3DJoint3D::_report_default_only(const char *p_name, double p_value, double p_default) const {
	if (Math::is_equal_approx(p_value, p_default)) {
		return;
	}

	_report_dropped(p_name, vformat("requested_value=%f default_value=%f", p_value, p_default));
}

Box3DJoint3D::Box3DJoint3D(const Box3DJoint3D &p_old_joint, Box3DBody3D *p_body_a, Box3DBody3D *p_body_b, const Transform3D &p_local_ref_a, const Transform3D &p_local_ref_b) :
		enabled(p_old_joint.enabled),
		collision_disabled(p_old_joint.collision_disabled),
		body_a(p_body_a),
		body_b(p_body_b),
		rid(p_old_joint.rid),
		local_ref_a(p_local_ref_a),
		local_ref_b(p_local_ref_b) {
	if (body_a != nullptr) {
		body_a->add_joint(this);
	}

	if (body_b != nullptr) {
		body_b->add_joint(this);
	}
}

Box3DJoint3D::~Box3DJoint3D() {
	if (body_a != nullptr) {
		body_a->remove_joint(this);
	}

	if (body_b != nullptr) {
		body_b->remove_joint(this);
	}

	destroy();
}

Box3DSpace3D *Box3DJoint3D::get_space() const {
	if (body_a != nullptr && body_b != nullptr) {
		Box3DSpace3D *space_a = body_a->get_space();
		Box3DSpace3D *space_b = body_b->get_space();

		if (space_a == nullptr || space_b == nullptr) {
			return nullptr;
		}

		ERR_FAIL_COND_V_MSG(space_a != space_b, nullptr, vformat("Joint was found to connect bodies in different physics spaces. This joint will effectively be disabled. This joint connects %s.", _bodies_to_string()));

		return space_a;
	} else if (body_a != nullptr) {
		return body_a->get_space();
	} else if (body_b != nullptr) {
		return body_b->get_space();
	}

	return nullptr;
}

void Box3DJoint3D::set_enabled(bool p_enabled) {
	if (enabled == p_enabled) {
		return;
	}

	enabled = p_enabled;

	_enabled_changed();
}

int Box3DJoint3D::get_solver_priority() const {
	return JOINT_DEFAULT_SOLVER_PRIORITY;
}

void Box3DJoint3D::set_solver_priority(int p_priority) {
	if (p_priority != JOINT_DEFAULT_SOLVER_PRIORITY) {
		_report_dropped("Joint solver priority", vformat("requested_priority=%d default_priority=%d", p_priority, JOINT_DEFAULT_SOLVER_PRIORITY));
	}
}

void Box3DJoint3D::set_solver_velocity_iterations(int p_iterations) {
	if (velocity_iterations == p_iterations) {
		return;
	}

	velocity_iterations = p_iterations;

	if (p_iterations != 0) {
		_report_dropped("Joint velocity iterations", vformat("requested_iterations=%d", p_iterations));
	}
}

void Box3DJoint3D::set_solver_position_iterations(int p_iterations) {
	if (position_iterations == p_iterations) {
		return;
	}

	position_iterations = p_iterations;

	if (p_iterations != 0) {
		_report_dropped("Joint position iterations", vformat("requested_iterations=%d", p_iterations));
	}
}

void Box3DJoint3D::set_collision_disabled(bool p_disabled) {
	collision_disabled = p_disabled;

	if (body_a == nullptr || body_b == nullptr) {
		return;
	}

	Box3DPhysicsServer3D *physics_server = Box3DPhysicsServer3D::get_singleton();

	if (collision_disabled) {
		physics_server->body_add_collision_exception(body_a->get_rid(), body_b->get_rid());
		physics_server->body_add_collision_exception(body_b->get_rid(), body_a->get_rid());
	} else {
		physics_server->body_remove_collision_exception(body_a->get_rid(), body_b->get_rid());
		physics_server->body_remove_collision_exception(body_b->get_rid(), body_a->get_rid());
	}
}

float Box3DJoint3D::get_applied_force() const {
	if (!b3Joint_IsValid(joint_id)) {
		return 0.0f;
	}

	return (float)to_godot(b3Joint_GetConstraintForce(joint_id)).length();
}

float Box3DJoint3D::get_applied_torque() const {
	if (!b3Joint_IsValid(joint_id)) {
		return 0.0f;
	}

	return (float)to_godot(b3Joint_GetConstraintTorque(joint_id)).length();
}

void Box3DJoint3D::destroy() {
	if (B3_IS_NULL(joint_id)) {
		return;
	}

	if (b3Joint_IsValid(joint_id)) {
		b3DestroyJoint(joint_id, false);
	}

	joint_id = b3_nullJointId;
}

void Box3DJoint3D::rebuild() {
	destroy();

	if (!enabled || get_type() == PhysicsServer3D::JOINT_TYPE_MAX) {
		return;
	}

	Box3DSpace3D *space = get_space();
	if (space == nullptr) {
		return;
	}

	ERR_FAIL_COND(body_a == nullptr && body_b == nullptr);

	Frames frames;
	frames.body_a = body_a != nullptr ? body_a->get_body_id() : space->get_anchor_body();
	frames.body_b = body_b != nullptr ? body_b->get_body_id() : space->get_anchor_body();
	frames.frame_a = local_ref_a;
	frames.frame_b = local_ref_b;

	// Box3D measures joint frames from the body origin, and Godot's are expressed in unscaled body space.
	if (body_a != nullptr) {
		frames.frame_a.origin *= body_a->get_scale();
	}

	if (body_b != nullptr) {
		frames.frame_b.origin *= body_b->get_scale();
	}

	ERR_FAIL_COND(B3_IS_NULL(frames.body_a) || B3_IS_NULL(frames.body_b));

	joint_id = _create(space->get_world(), frames);

	if (B3_IS_NON_NULL(joint_id)) {
		_joint_created();
	}
}

/* Pin */

Box3DPinJoint3D::Box3DPinJoint3D(const Box3DJoint3D &p_old_joint, Box3DBody3D *p_body_a, Box3DBody3D *p_body_b, const Vector3 &p_local_a, const Vector3 &p_local_b) :
		Box3DJoint3D(p_old_joint, p_body_a, p_body_b, Transform3D({}, p_local_a), Transform3D({}, p_local_b)) {
	rebuild();
}

b3JointId Box3DPinJoint3D::_create(b3WorldId p_world, const Frames &p_frames) {
	b3SphericalJointDef def = b3DefaultSphericalJointDef();
	_fill_base(def.base, p_frames);
	return b3CreateSphericalJoint(p_world, &def);
}

void Box3DPinJoint3D::set_local_a(const Vector3 &p_local_a) {
	local_ref_a = Transform3D({}, p_local_a);
	rebuild();
	_wake_up_bodies();
}

void Box3DPinJoint3D::set_local_b(const Vector3 &p_local_b) {
	local_ref_b = Transform3D({}, p_local_b);
	rebuild();
	_wake_up_bodies();
}

double Box3DPinJoint3D::get_param(PhysicsServer3D::PinJointParam p_param) const {
	switch (p_param) {
		case PhysicsServer3D::PIN_JOINT_BIAS: {
			return PIN_DEFAULT_BIAS;
		}
		case PhysicsServer3D::PIN_JOINT_DAMPING: {
			return PIN_DEFAULT_DAMPING;
		}
		case PhysicsServer3D::PIN_JOINT_IMPULSE_CLAMP: {
			return PIN_DEFAULT_IMPULSE_CLAMP;
		}
		default: {
			ERR_FAIL_V_MSG(0.0, vformat("Unhandled pin joint parameter: '%d'. This should not happen. Please report this.", p_param));
		}
	}
}

void Box3DPinJoint3D::set_param(PhysicsServer3D::PinJointParam p_param, double p_value) {
	switch (p_param) {
		case PhysicsServer3D::PIN_JOINT_BIAS: {
			_report_default_only("Pin joint bias", p_value, PIN_DEFAULT_BIAS);
		} break;
		case PhysicsServer3D::PIN_JOINT_DAMPING: {
			_report_default_only("Pin joint damping", p_value, PIN_DEFAULT_DAMPING);
		} break;
		case PhysicsServer3D::PIN_JOINT_IMPULSE_CLAMP: {
			_report_default_only("Pin joint impulse clamp", p_value, PIN_DEFAULT_IMPULSE_CLAMP);
		} break;
		default: {
			ERR_FAIL_MSG(vformat("Unhandled pin joint parameter: '%d'. This should not happen. Please report this.", p_param));
		} break;
	}
}

/* Hinge */

Box3DHingeJoint3D::Box3DHingeJoint3D(const Box3DJoint3D &p_old_joint, Box3DBody3D *p_body_a, Box3DBody3D *p_body_b, const Transform3D &p_local_ref_a, const Transform3D &p_local_ref_b) :
		Box3DJoint3D(p_old_joint, p_body_a, p_body_b, p_local_ref_a, p_local_ref_b) {
	motor_max_torque = FLT_MAX;
	rebuild();
}

b3JointId Box3DHingeJoint3D::_create(b3WorldId p_world, const Frames &p_frames) {
	const bool has_limit = limits_enabled && limit_lower <= limit_upper;

	if (has_limit && limit_lower == limit_upper) {
		b3WeldJointDef def = b3DefaultWeldJointDef();
		_fill_base(def.base, p_frames);
		return b3CreateWeldJoint(p_world, &def);
	}

	b3RevoluteJointDef def = b3DefaultRevoluteJointDef();
	_fill_base(def.base, p_frames);

	if (has_limit) {
		def.enableLimit = true;
		def.lowerAngle = CLAMP((float)limit_lower, -MAX_LIMIT_ANGLE, MAX_LIMIT_ANGLE);
		def.upperAngle = CLAMP((float)limit_upper, -MAX_LIMIT_ANGLE, MAX_LIMIT_ANGLE);
	}

	if (motor_enabled) {
		def.enableMotor = true;
		// We flip the direction since Box3D is CCW but Godot is CW.
		def.motorSpeed = (float)-motor_target_speed;
		def.maxMotorTorque = (float)MIN(motor_max_torque, (double)FLT_MAX);
	}

	return b3CreateRevoluteJoint(p_world, &def);
}

double Box3DHingeJoint3D::get_param(PhysicsServer3D::HingeJointParam p_param) const {
	switch (p_param) {
		case PhysicsServer3D::HINGE_JOINT_BIAS: {
			return HINGE_DEFAULT_BIAS;
		}
		case PhysicsServer3D::HINGE_JOINT_LIMIT_UPPER: {
			return limit_upper;
		}
		case PhysicsServer3D::HINGE_JOINT_LIMIT_LOWER: {
			return limit_lower;
		}
		case PhysicsServer3D::HINGE_JOINT_LIMIT_BIAS: {
			return HINGE_DEFAULT_LIMIT_BIAS;
		}
		case PhysicsServer3D::HINGE_JOINT_LIMIT_SOFTNESS: {
			return HINGE_DEFAULT_SOFTNESS;
		}
		case PhysicsServer3D::HINGE_JOINT_LIMIT_RELAXATION: {
			return HINGE_DEFAULT_RELAXATION;
		}
		case PhysicsServer3D::HINGE_JOINT_MOTOR_TARGET_VELOCITY: {
			return motor_target_speed;
		}
		case PhysicsServer3D::HINGE_JOINT_MOTOR_MAX_IMPULSE: {
			// Godot uses a max impulse instead of a max torque, so this is estimated from the physics step.
			return motor_max_torque * estimate_physics_step();
		}
		default: {
			ERR_FAIL_V_MSG(0.0, vformat("Unhandled parameter: '%d'. This should not happen. Please report this.", p_param));
		}
	}
}

void Box3DHingeJoint3D::set_param(PhysicsServer3D::HingeJointParam p_param, double p_value) {
	switch (p_param) {
		case PhysicsServer3D::HINGE_JOINT_BIAS: {
			_report_default_only("Hinge joint bias", p_value, HINGE_DEFAULT_BIAS);
		} break;
		case PhysicsServer3D::HINGE_JOINT_LIMIT_UPPER: {
			limit_upper = p_value;
			rebuild();
			_wake_up_bodies();
		} break;
		case PhysicsServer3D::HINGE_JOINT_LIMIT_LOWER: {
			limit_lower = p_value;
			rebuild();
			_wake_up_bodies();
		} break;
		case PhysicsServer3D::HINGE_JOINT_LIMIT_BIAS: {
			_report_default_only("Hinge joint limit bias", p_value, HINGE_DEFAULT_LIMIT_BIAS);
		} break;
		case PhysicsServer3D::HINGE_JOINT_LIMIT_SOFTNESS: {
			_report_default_only("Hinge joint limit softness", p_value, HINGE_DEFAULT_SOFTNESS);
		} break;
		case PhysicsServer3D::HINGE_JOINT_LIMIT_RELAXATION: {
			_report_default_only("Hinge joint limit relaxation", p_value, HINGE_DEFAULT_RELAXATION);
		} break;
		case PhysicsServer3D::HINGE_JOINT_MOTOR_TARGET_VELOCITY: {
			motor_target_speed = p_value;
			if (B3_IS_NON_NULL(joint_id) && motor_enabled) {
				b3RevoluteJoint_SetMotorSpeed(joint_id, (float)-motor_target_speed);
			}
			_wake_up_bodies();
		} break;
		case PhysicsServer3D::HINGE_JOINT_MOTOR_MAX_IMPULSE: {
			// Godot uses a max impulse instead of a max torque, so this is estimated from the physics step.
			motor_max_torque = p_value / estimate_physics_step();
			if (B3_IS_NON_NULL(joint_id) && motor_enabled) {
				b3RevoluteJoint_SetMaxMotorTorque(joint_id, (float)MIN(motor_max_torque, (double)FLT_MAX));
			}
			_wake_up_bodies();
		} break;
		default: {
			ERR_FAIL_MSG(vformat("Unhandled parameter: '%d'. This should not happen. Please report this.", p_param));
		} break;
	}
}

bool Box3DHingeJoint3D::get_flag(PhysicsServer3D::HingeJointFlag p_flag) const {
	switch (p_flag) {
		case PhysicsServer3D::HINGE_JOINT_FLAG_USE_LIMIT: {
			return limits_enabled;
		}
		case PhysicsServer3D::HINGE_JOINT_FLAG_ENABLE_MOTOR: {
			return motor_enabled;
		}
		default: {
			ERR_FAIL_V_MSG(false, vformat("Unhandled flag: '%d'. This should not happen. Please report this.", p_flag));
		}
	}
}

void Box3DHingeJoint3D::set_flag(PhysicsServer3D::HingeJointFlag p_flag, bool p_enabled) {
	switch (p_flag) {
		case PhysicsServer3D::HINGE_JOINT_FLAG_USE_LIMIT: {
			limits_enabled = p_enabled;
			rebuild();
			_wake_up_bodies();
		} break;
		case PhysicsServer3D::HINGE_JOINT_FLAG_ENABLE_MOTOR: {
			motor_enabled = p_enabled;
			rebuild();
			_wake_up_bodies();
		} break;
		default: {
			ERR_FAIL_MSG(vformat("Unhandled flag: '%d'. This should not happen. Please report this.", p_flag));
		} break;
	}
}

/* Slider */

Box3DSliderJoint3D::Box3DSliderJoint3D(const Box3DJoint3D &p_old_joint, Box3DBody3D *p_body_a, Box3DBody3D *p_body_b, const Transform3D &p_local_ref_a, const Transform3D &p_local_ref_b) :
		Box3DJoint3D(p_old_joint, p_body_a, p_body_b, p_local_ref_a, p_local_ref_b) {
	rebuild();
}

b3JointId Box3DSliderJoint3D::_create(b3WorldId p_world, const Frames &p_frames) {
	const bool has_limit = limit_lower <= limit_upper;

	if (has_limit && limit_lower == limit_upper) {
		b3WeldJointDef def = b3DefaultWeldJointDef();
		_fill_base(def.base, p_frames);
		return b3CreateWeldJoint(p_world, &def);
	}

	b3PrismaticJointDef def = b3DefaultPrismaticJointDef();
	_fill_base(def.base, p_frames);

	if (has_limit) {
		def.enableLimit = true;
		def.lowerTranslation = (float)limit_lower;
		def.upperTranslation = (float)limit_upper;
	}

	return b3CreatePrismaticJoint(p_world, &def);
}

double Box3DSliderJoint3D::get_param(PhysicsServer3D::SliderJointParam p_param) const {
	ERR_FAIL_INDEX_V((int)p_param, (int)PhysicsServer3D::SLIDER_JOINT_MAX, 0.0);

	switch (p_param) {
		case PhysicsServer3D::SLIDER_JOINT_LINEAR_LIMIT_UPPER: {
			return limit_upper;
		}
		case PhysicsServer3D::SLIDER_JOINT_LINEAR_LIMIT_LOWER: {
			return limit_lower;
		}
		default: {
			return SLIDER_DEFAULTS[p_param];
		}
	}
}

void Box3DSliderJoint3D::set_param(PhysicsServer3D::SliderJointParam p_param, double p_value) {
	ERR_FAIL_INDEX((int)p_param, (int)PhysicsServer3D::SLIDER_JOINT_MAX);

	switch (p_param) {
		case PhysicsServer3D::SLIDER_JOINT_LINEAR_LIMIT_UPPER: {
			limit_upper = p_value;
			rebuild();
			_wake_up_bodies();
		} break;
		case PhysicsServer3D::SLIDER_JOINT_LINEAR_LIMIT_LOWER: {
			limit_lower = p_value;
			rebuild();
			_wake_up_bodies();
		} break;
		default: {
			_report_default_only("Slider joint parameter", p_value, SLIDER_DEFAULTS[p_param]);
		} break;
	}
}

/* Cone twist */

Box3DConeTwistJoint3D::Box3DConeTwistJoint3D(const Box3DJoint3D &p_old_joint, Box3DBody3D *p_body_a, Box3DBody3D *p_body_b, const Transform3D &p_local_ref_a, const Transform3D &p_local_ref_b) :
		Box3DJoint3D(p_old_joint, p_body_a, p_body_b, p_local_ref_a, p_local_ref_b) {
	rebuild();
}

b3JointId Box3DConeTwistJoint3D::_create(b3WorldId p_world, const Frames &p_frames) {
	Frames frames = p_frames;

	// Godot twists around the x-axis of the frame while Box3D uses the z-axis.
	frames.frame_a = _with_axis_as_z(frames.frame_a, 0);
	frames.frame_b = _with_axis_as_z(frames.frame_b, 0);

	b3SphericalJointDef def = b3DefaultSphericalJointDef();
	_fill_base(def.base, frames);

	const bool swing_valid = swing_span >= 0.0 && swing_span <= Math::PI;
	const bool twist_valid = twist_span >= 0.0 && twist_span <= Math::PI;

	if (swing_valid && swing_span < Math::PI) {
		def.enableConeLimit = true;
		def.coneAngle = (float)swing_span;
	}

	if (swing_valid && twist_valid && twist_span < Math::PI) {
		def.enableTwistLimit = true;
		def.lowerTwistAngle = -MIN((float)twist_span, MAX_LIMIT_ANGLE);
		def.upperTwistAngle = MIN((float)twist_span, MAX_LIMIT_ANGLE);
	}

	return b3CreateSphericalJoint(p_world, &def);
}

double Box3DConeTwistJoint3D::get_param(PhysicsServer3D::ConeTwistJointParam p_param) const {
	switch (p_param) {
		case PhysicsServer3D::CONE_TWIST_JOINT_SWING_SPAN: {
			return swing_span;
		}
		case PhysicsServer3D::CONE_TWIST_JOINT_TWIST_SPAN: {
			return twist_span;
		}
		case PhysicsServer3D::CONE_TWIST_JOINT_BIAS: {
			return CONE_TWIST_DEFAULT_BIAS;
		}
		case PhysicsServer3D::CONE_TWIST_JOINT_SOFTNESS: {
			return CONE_TWIST_DEFAULT_SOFTNESS;
		}
		case PhysicsServer3D::CONE_TWIST_JOINT_RELAXATION: {
			return CONE_TWIST_DEFAULT_RELAXATION;
		}
		default: {
			ERR_FAIL_V_MSG(0.0, vformat("Unhandled parameter: '%d'. This should not happen. Please report this.", p_param));
		}
	}
}

void Box3DConeTwistJoint3D::set_param(PhysicsServer3D::ConeTwistJointParam p_param, double p_value) {
	switch (p_param) {
		case PhysicsServer3D::CONE_TWIST_JOINT_SWING_SPAN: {
			swing_span = p_value;
			rebuild();
			_wake_up_bodies();
		} break;
		case PhysicsServer3D::CONE_TWIST_JOINT_TWIST_SPAN: {
			twist_span = p_value;
			rebuild();
			_wake_up_bodies();
		} break;
		case PhysicsServer3D::CONE_TWIST_JOINT_BIAS: {
			_report_default_only("Cone twist joint bias", p_value, CONE_TWIST_DEFAULT_BIAS);
		} break;
		case PhysicsServer3D::CONE_TWIST_JOINT_SOFTNESS: {
			_report_default_only("Cone twist joint softness", p_value, CONE_TWIST_DEFAULT_SOFTNESS);
		} break;
		case PhysicsServer3D::CONE_TWIST_JOINT_RELAXATION: {
			_report_default_only("Cone twist joint relaxation", p_value, CONE_TWIST_DEFAULT_RELAXATION);
		} break;
		default: {
			ERR_FAIL_MSG(vformat("Unhandled parameter: '%d'. This should not happen. Please report this.", p_param));
		} break;
	}
}

/* Generic 6DOF */

Box3DGeneric6DOFJoint3D::Box3DGeneric6DOFJoint3D(const Box3DJoint3D &p_old_joint, Box3DBody3D *p_body_a, Box3DBody3D *p_body_b, const Transform3D &p_local_ref_a, const Transform3D &p_local_ref_b) :
		Box3DJoint3D(p_old_joint, p_body_a, p_body_b, p_local_ref_a, p_local_ref_b) {
	for (int i = 0; i < AXIS_COUNT; i++) {
		motor_limit[i] = FLT_MAX;
	}

	rebuild();
}

b3JointId Box3DGeneric6DOFJoint3D::_create(b3WorldId p_world, const Frames &p_frames) {
	int locked_linear_count = 0;
	int translating_axis = -1;
	int translating_count = 0;

	for (int axis = AXIS_LINEAR_X; axis <= AXIS_LINEAR_Z; axis++) {
		if (_is_locked(axis)) {
			locked_linear_count++;
		} else {
			translating_count++;
			translating_axis = axis;
		}
	}

	int locked_angular_count = 0;
	for (int axis = AXIS_ANGULAR_X; axis <= AXIS_ANGULAR_Z; axis++) {
		if (_is_locked(axis)) {
			locked_angular_count++;
		}
	}

	String dropped;

	for (int axis = 0; axis < AXIS_COUNT; axis++) {
		const char *axis_name = axis < 3 ? "linear" : "angular";
		const char *axis_letter = axis % 3 == 0 ? "x" : (axis % 3 == 1 ? "y" : "z");

		if (motor_enabled[axis]) {
			dropped += vformat("[%s_%s motor: speed=%f limit=%f] ", axis_name, axis_letter, motor_speed[axis], motor_limit[axis]);
		}

		if (spring_enabled[axis]) {
			dropped += vformat("[%s_%s spring: stiffness=%f damping=%f equilibrium=%f] ", axis_name, axis_letter, spring_stiffness[axis], spring_damping[axis], spring_equilibrium[axis]);
		}
	}

	String settings;

	for (int axis = 0; axis < AXIS_COUNT; axis++) {
		settings += vformat("%s_%s(enabled=%s lower=%f upper=%f) ", axis < 3 ? "linear" : "angular", axis % 3 == 0 ? "x" : (axis % 3 == 1 ? "y" : "z"), limit_enabled[axis], limit_lower[axis], limit_upper[axis]);
	}

	if (translating_count == 0) {
		if (locked_angular_count == 3) {
			b3WeldJointDef def = b3DefaultWeldJointDef();
			_fill_base(def.base, p_frames);

			if (!dropped.is_empty()) {
				_report_dropped("6DOF motors and springs", vformat("approximation=weld_joint dropped=%s axes=%s", dropped, settings));
			}

			return b3CreateWeldJoint(p_world, &def);
		}

		b3SphericalJointDef def = b3DefaultSphericalJointDef();
		_fill_base(def.base, p_frames);

		bool angular_limited = false;
		for (int axis = AXIS_ANGULAR_X; axis <= AXIS_ANGULAR_Z; axis++) {
			if (!_is_free(axis)) {
				angular_limited = true;
			}
		}

		if (angular_limited || !dropped.is_empty()) {
			_report_dropped("6DOF angular limits", vformat("approximation=ball_joint_without_angular_limits dropped=%s axes=%s", dropped, settings));
		}

		return b3CreateSphericalJoint(p_world, &def);
	}

	if (translating_count == 1) {
		Frames frames = p_frames;
		frames.frame_a = _with_axis_as_x(frames.frame_a, translating_axis);
		frames.frame_b = _with_axis_as_x(frames.frame_b, translating_axis);

		b3PrismaticJointDef def = b3DefaultPrismaticJointDef();
		_fill_base(def.base, frames);

		if (!_is_free(translating_axis)) {
			def.enableLimit = true;
			def.lowerTranslation = (float)limit_lower[translating_axis];
			def.upperTranslation = (float)limit_upper[translating_axis];
		}

		if (locked_angular_count != 3 || !dropped.is_empty()) {
			_report_dropped("6DOF rotation with a sliding axis", vformat("approximation=prismatic_joint_with_locked_rotation dropped=%s axes=%s", dropped, settings));
		}

		return b3CreatePrismaticJoint(p_world, &def);
	}

	_report_dropped("6DOF joint configuration", vformat("no_joint_created=true locked_linear_axes=%d locked_angular_axes=%d axes=%s", locked_linear_count, locked_angular_count, settings));

	return b3_nullJointId;
}

double Box3DGeneric6DOFJoint3D::get_param(Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisParam p_param) const {
	const int axis = (int)p_axis;
	ERR_FAIL_INDEX_V(axis, 3, 0.0);

	switch (p_param) {
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_LOWER_LIMIT: {
			return limit_lower[AXIS_LINEAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_UPPER_LIMIT: {
			return limit_upper[AXIS_LINEAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_LIMIT_SOFTNESS: {
			return 0.7;
		}
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_RESTITUTION: {
			return 0.5;
		}
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_DAMPING: {
			return 1.0;
		}
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_MOTOR_TARGET_VELOCITY: {
			return motor_speed[AXIS_LINEAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_MOTOR_FORCE_LIMIT: {
			return motor_limit[AXIS_LINEAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_SPRING_STIFFNESS: {
			return spring_stiffness[AXIS_LINEAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_SPRING_DAMPING: {
			return spring_damping[AXIS_LINEAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_SPRING_EQUILIBRIUM_POINT: {
			return spring_equilibrium[AXIS_LINEAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_LOWER_LIMIT: {
			return limit_lower[AXIS_ANGULAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_UPPER_LIMIT: {
			return limit_upper[AXIS_ANGULAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_LIMIT_SOFTNESS: {
			return 0.5;
		}
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_DAMPING: {
			return 1.0;
		}
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_RESTITUTION: {
			return 0.0;
		}
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_FORCE_LIMIT: {
			return 0.0;
		}
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_ERP: {
			return 0.5;
		}
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_MOTOR_TARGET_VELOCITY: {
			return motor_speed[AXIS_ANGULAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_MOTOR_FORCE_LIMIT: {
			return motor_limit[AXIS_ANGULAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_SPRING_STIFFNESS: {
			return spring_stiffness[AXIS_ANGULAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_SPRING_DAMPING: {
			return spring_damping[AXIS_ANGULAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_SPRING_EQUILIBRIUM_POINT: {
			return spring_equilibrium[AXIS_ANGULAR_X + axis];
		}
		default: {
			ERR_FAIL_V_MSG(0.0, vformat("Unhandled parameter: '%d'. This should not happen. Please report this.", p_param));
		}
	}
}

void Box3DGeneric6DOFJoint3D::set_param(Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisParam p_param, double p_value) {
	const int axis = (int)p_axis;
	ERR_FAIL_INDEX(axis, 3);

	bool rebuild_needed = false;

	switch (p_param) {
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_LOWER_LIMIT: {
			limit_lower[AXIS_LINEAR_X + axis] = p_value;
			rebuild_needed = true;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_UPPER_LIMIT: {
			limit_upper[AXIS_LINEAR_X + axis] = p_value;
			rebuild_needed = true;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_LIMIT_SOFTNESS:
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_RESTITUTION:
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_DAMPING:
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_LIMIT_SOFTNESS:
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_DAMPING:
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_RESTITUTION:
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_FORCE_LIMIT:
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_ERP: {
			_report_default_only("6DOF joint solver parameter", p_value, get_param(p_axis, p_param));
		} break;
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_MOTOR_TARGET_VELOCITY: {
			motor_speed[AXIS_LINEAR_X + axis] = p_value;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_MOTOR_FORCE_LIMIT: {
			motor_limit[AXIS_LINEAR_X + axis] = p_value;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_SPRING_STIFFNESS: {
			spring_stiffness[AXIS_LINEAR_X + axis] = p_value;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_SPRING_DAMPING: {
			spring_damping[AXIS_LINEAR_X + axis] = p_value;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_SPRING_EQUILIBRIUM_POINT: {
			spring_equilibrium[AXIS_LINEAR_X + axis] = p_value;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_LOWER_LIMIT: {
			// Godot's angular axes run the opposite way of Box3D's, so the limits are mirrored.
			limit_upper[AXIS_ANGULAR_X + axis] = -p_value;
			rebuild_needed = true;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_UPPER_LIMIT: {
			limit_lower[AXIS_ANGULAR_X + axis] = -p_value;
			rebuild_needed = true;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_MOTOR_TARGET_VELOCITY: {
			motor_speed[AXIS_ANGULAR_X + axis] = p_value;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_MOTOR_FORCE_LIMIT: {
			motor_limit[AXIS_ANGULAR_X + axis] = p_value;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_SPRING_STIFFNESS: {
			spring_stiffness[AXIS_ANGULAR_X + axis] = p_value;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_SPRING_DAMPING: {
			spring_damping[AXIS_ANGULAR_X + axis] = p_value;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_SPRING_EQUILIBRIUM_POINT: {
			spring_equilibrium[AXIS_ANGULAR_X + axis] = p_value;
		} break;
		default: {
			ERR_FAIL_MSG(vformat("Unhandled parameter: '%d'. This should not happen. Please report this.", p_param));
		} break;
	}

	if (rebuild_needed) {
		rebuild();
		_wake_up_bodies();
	}
}

bool Box3DGeneric6DOFJoint3D::get_flag(Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisFlag p_flag) const {
	const int axis = (int)p_axis;
	ERR_FAIL_INDEX_V(axis, 3, false);

	switch (p_flag) {
		case PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_LINEAR_LIMIT: {
			return limit_enabled[AXIS_LINEAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_ANGULAR_LIMIT: {
			return limit_enabled[AXIS_ANGULAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_LINEAR_SPRING: {
			return spring_enabled[AXIS_LINEAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_ANGULAR_SPRING: {
			return spring_enabled[AXIS_ANGULAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_MOTOR: {
			return motor_enabled[AXIS_ANGULAR_X + axis];
		}
		case PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_LINEAR_MOTOR: {
			return motor_enabled[AXIS_LINEAR_X + axis];
		}
		default: {
			ERR_FAIL_V_MSG(false, vformat("Unhandled flag: '%d'. This should not happen. Please report this.", p_flag));
		}
	}
}

void Box3DGeneric6DOFJoint3D::set_flag(Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisFlag p_flag, bool p_enabled) {
	const int axis = (int)p_axis;
	ERR_FAIL_INDEX(axis, 3);

	bool rebuild_needed = false;

	switch (p_flag) {
		case PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_LINEAR_LIMIT: {
			limit_enabled[AXIS_LINEAR_X + axis] = p_enabled;
			rebuild_needed = true;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_ANGULAR_LIMIT: {
			limit_enabled[AXIS_ANGULAR_X + axis] = p_enabled;
			rebuild_needed = true;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_LINEAR_SPRING: {
			spring_enabled[AXIS_LINEAR_X + axis] = p_enabled;
			rebuild_needed = true;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_ANGULAR_SPRING: {
			spring_enabled[AXIS_ANGULAR_X + axis] = p_enabled;
			rebuild_needed = true;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_MOTOR: {
			motor_enabled[AXIS_ANGULAR_X + axis] = p_enabled;
			rebuild_needed = true;
		} break;
		case PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_LINEAR_MOTOR: {
			motor_enabled[AXIS_LINEAR_X + axis] = p_enabled;
			rebuild_needed = true;
		} break;
		default: {
			ERR_FAIL_MSG(vformat("Unhandled flag: '%d'. This should not happen. Please report this.", p_flag));
		} break;
	}

	if (rebuild_needed) {
		rebuild();
		_wake_up_bodies();
	}
}
