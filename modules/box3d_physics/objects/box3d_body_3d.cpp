/**************************************************************************/
/*  box3d_body_3d.cpp                                                     */
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

#include "box3d_body_3d.h"

#include "../box3d_physics_server_3d.h"
#include "../box3d_project_settings.h"
#include "../joints/box3d_joint_3d.h"
#include "../misc/box3d_diagnostics.h"
#include "../shapes/box3d_shape_3d.h"
#include "../spaces/box3d_space_3d.h"
#include "box3d_area_3d.h"
#include "box3d_physics_direct_body_state_3d.h"

b3BodyType Box3DBody3D::_get_body_type() const {
	switch (mode) {
		case PhysicsServer3D::BODY_MODE_STATIC: {
			return b3_staticBody;
		}
		case PhysicsServer3D::BODY_MODE_KINEMATIC: {
			return b3_kinematicBody;
		}
		case PhysicsServer3D::BODY_MODE_RIGID:
		case PhysicsServer3D::BODY_MODE_RIGID_LINEAR: {
			return b3_dynamicBody;
		}
		default: {
			ERR_FAIL_V_MSG(b3_staticBody, vformat("Unhandled body mode: '%d'. This should not happen. Please report this.", mode));
		}
	}
}

b3ShapeDef Box3DBody3D::_make_shape_def() const {
	b3ShapeDef def = b3DefaultShapeDef();
	def.density = 1000.0f;
	def.baseMaterial.friction = friction;
	def.baseMaterial.restitution = bounce;
	if (in_space() && linear_surface_velocity != Vector3()) {
		def.baseMaterial.tangentVelocity = b3Body_GetLocalVector(body_id, to_b3(linear_surface_velocity));
	}
	def.filter = Box3DFilter::make(collision_layer);
	def.enableCustomFiltering = true;
	def.enableSensorEvents = true;
	def.updateBodyMass = false;
	return def;
}

void Box3DBody3D::_fill_body_def(b3BodyDef &r_def) const {
	if (is_rigid()) {
		r_def.linearVelocity = to_b3(linear_velocity_cache);
		r_def.angularVelocity = to_b3(angular_velocity_cache);
	}

	r_def.linearDamping = total_linear_damp;
	r_def.angularDamping = total_angular_damp;
	r_def.gravityScale = 0.0f;
	r_def.enableSleep = is_sleep_actually_allowed();
	r_def.isAwake = !sleep_initially;
	r_def.isBullet = ccd_enabled;
	r_def.motionLocks = _calculate_motion_locks();
	r_def.enableContactRecycling = !reports_contacts();
}

b3MotionLocks Box3DBody3D::_calculate_motion_locks() const {
	if (is_static()) {
		return b3MotionLocks{};
	}

	// Ordered as the linear axes followed by the angular axes, each one x, y and z.
	return b3MotionLocks{
		is_axis_locked(PhysicsServer3D::BODY_AXIS_LINEAR_X),
		is_axis_locked(PhysicsServer3D::BODY_AXIS_LINEAR_Y),
		is_axis_locked(PhysicsServer3D::BODY_AXIS_LINEAR_Z),
		is_axis_locked(PhysicsServer3D::BODY_AXIS_ANGULAR_X) || is_rigid_linear(),
		is_axis_locked(PhysicsServer3D::BODY_AXIS_ANGULAR_Y) || is_rigid_linear(),
		is_axis_locked(PhysicsServer3D::BODY_AXIS_ANGULAR_Z) || is_rigid_linear(),
	};
}

void Box3DBody3D::_add_to_space() {
	Box3DObject3D::_add_to_space();

	if (!in_space()) {
		return;
	}

	space->register_body(this);

	if (reports_contacts()) {
		space->register_contact_reporter(this);
	}
}

void Box3DBody3D::_remove_from_space() {
	if (!in_space()) {
		return;
	}

	linear_velocity_cache = get_linear_velocity();
	angular_velocity_cache = get_angular_velocity();

	space->unregister_body(this);
	space->unregister_contact_reporter(this);
	space->dequeue_forces(&force_element);
	space->dequeue_kinematic(&kinematic_element);

	Box3DObject3D::_remove_from_space();
}

void Box3DBody3D::_enqueue_call_queries() {
	if (space != nullptr) {
		space->enqueue_call_queries(&call_queries_element);
	}
}

void Box3DBody3D::_dequeue_call_queries() {
	if (space != nullptr) {
		space->dequeue_call_queries(&call_queries_element);
	}
}

void Box3DBody3D::_update_force_registration() {
	if (!in_space()) {
		return;
	}

	const bool needs_forces = is_rigid() && !custom_integrator &&
			(has_point_gravity || constant_force != Vector3() || constant_torque != Vector3() || extra_gravity_acceleration != Vector3());

	if (needs_forces) {
		space->enqueue_forces(&force_element);
	} else {
		space->dequeue_forces(&force_element);
	}
}

void Box3DBody3D::_update_mass_properties() {
	if (!in_space() || !is_rigid()) {
		return;
	}

	b3Body_ApplyMassFromShapes(body_id);

	b3MassData data = b3Body_GetMassData(body_id);

	if (data.mass <= 0.0f) {
		// No shape contributes any mass, so fall back to something that keeps the solver stable.
		data.mass = MAX(mass, 1.0f);
		data.center = b3Vec3_zero;
		data.inertia = b3Mat3_identity;
		const float fallback_inertia = data.mass * 0.4f * 0.01f;
		data.inertia.cx.x = fallback_inertia;
		data.inertia.cy.y = fallback_inertia;
		data.inertia.cz.z = fallback_inertia;
	}

	if (custom_center_of_mass) {
		data.center = to_b3(center_of_mass_custom);
	}

	const bool calculate_mass = mass <= 0.0f;
	const bool calculate_inertia = inertia.x <= 0 || inertia.y <= 0 || inertia.z <= 0;

	if (!calculate_mass) {
		const float ratio = mass / data.mass;
		data.inertia.cx = b3MulSV(ratio, data.inertia.cx);
		data.inertia.cy = b3MulSV(ratio, data.inertia.cy);
		data.inertia.cz = b3MulSV(ratio, data.inertia.cz);
		data.mass = mass;
	}

	if (inertia.x > 0) {
		data.inertia.cx = b3Vec3{ (float)inertia.x, 0.0f, 0.0f };
		data.inertia.cy.x = 0.0f;
		data.inertia.cz.x = 0.0f;
	}

	if (inertia.y > 0) {
		data.inertia.cy = b3Vec3{ 0.0f, (float)inertia.y, 0.0f };
		data.inertia.cx.y = 0.0f;
		data.inertia.cz.y = 0.0f;
	}

	if (inertia.z > 0) {
		data.inertia.cz = b3Vec3{ 0.0f, 0.0f, (float)inertia.z };
		data.inertia.cx.z = 0.0f;
		data.inertia.cy.z = 0.0f;
	}

	(void)calculate_inertia;

	b3Body_SetMassData(body_id, data);
}

bool Box3DBody3D::_needs_update_environmental_properties() const {
	return in_space() && has_point_gravity;
}

void Box3DBody3D::_update_environmental_properties() {
	if (unlikely(!in_space())) {
		return;
	}

	total_gravity = Vector3();
	total_linear_damp = 0.0f;
	total_angular_damp = 0.0f;

	const Vector3 position = get_position();

	bool gravity_done = false;
	bool linear_damp_done = linear_damp_mode == PhysicsServer3D::BODY_DAMP_MODE_REPLACE;
	bool angular_damp_done = angular_damp_mode == PhysicsServer3D::BODY_DAMP_MODE_REPLACE;

	for (const Box3DArea3D *area : areas) {
		if (!gravity_done) {
			gravity_done = Box3DArea3D::apply_override(total_gravity, area->get_gravity_mode(), [&]() {
				return area->compute_gravity(position);
			});
		}

		if (!linear_damp_done) {
			linear_damp_done = Box3DArea3D::apply_override(total_linear_damp, area->get_linear_damp_mode(), [&]() {
				return area->get_linear_damp();
			});
		}

		if (!angular_damp_done) {
			angular_damp_done = Box3DArea3D::apply_override(total_angular_damp, area->get_angular_damp_mode(), [&]() {
				return area->get_angular_damp();
			});
		}

		if (gravity_done && linear_damp_done && angular_damp_done) {
			break;
		}
	}

	if (!gravity_done) {
		total_gravity += space->get_default_area()->compute_gravity(position);
	}

	if (!linear_damp_done) {
		total_linear_damp += space->get_default_area()->get_linear_damp();
	}

	if (!angular_damp_done) {
		total_angular_damp += space->get_default_area()->get_angular_damp();
	}

	total_gravity *= gravity_scale;

	switch (linear_damp_mode) {
		case PhysicsServer3D::BODY_DAMP_MODE_COMBINE: {
			total_linear_damp += linear_damp;
		} break;
		case PhysicsServer3D::BODY_DAMP_MODE_REPLACE: {
			total_linear_damp = linear_damp;
		} break;
	}

	switch (angular_damp_mode) {
		case PhysicsServer3D::BODY_DAMP_MODE_COMBINE: {
			total_angular_damp += angular_damp;
		} break;
		case PhysicsServer3D::BODY_DAMP_MODE_REPLACE: {
			total_angular_damp = angular_damp;
		} break;
	}

	if (is_rigid()) {
		if (custom_integrator) {
			b3Body_SetGravityScale(body_id, 0.0f);
			b3Body_SetLinearDamping(body_id, 0.0f);
			b3Body_SetAngularDamping(body_id, 0.0f);
			extra_gravity_acceleration = Vector3();
		} else {
			const bool world_gravity = space->uses_world_gravity();
			b3Body_SetGravityScale(body_id, world_gravity ? gravity_scale : 0.0f);
			b3Body_SetLinearDamping(body_id, total_linear_damp);
			b3Body_SetAngularDamping(body_id, total_angular_damp);
			extra_gravity_acceleration = total_gravity - (world_gravity ? space->get_world_gravity() * gravity_scale : Vector3());

			if (extra_gravity_acceleration.length_squared() < CMP_EPSILON2) {
				extra_gravity_acceleration = Vector3();
			}
		}
	}

	default_area_changed_count = space->get_default_area_changed_count();

	_update_force_registration();
}

void Box3DBody3D::_update_sleep_allowed() {
	if (in_space()) {
		b3Body_EnableSleep(body_id, is_sleep_actually_allowed());
	}
}

void Box3DBody3D::_update_material() {
	if (!in_space()) {
		return;
	}

	for (Box3DShapeInstance *instance : shapes) {
		for (const b3ShapeId &id : instance->built_shapes) {
			b3Shape_SetFriction(id, friction);
			b3Shape_SetRestitution(id, bounce);
		}
	}
}

void Box3DBody3D::_update_surface_velocity() {
	if (!in_space()) {
		return;
	}

	if (angular_surface_velocity != Vector3()) {
		BOX3D_UNSUPPORTED_KEYED("Constant angular velocity on static and kinematic bodies",
				"A StaticBody3D or AnimatableBody3D was given a constant angular velocity.",
				"Box3D can only make a surface behave like a conveyor belt (a constant linear velocity along the contact surface). A surface that turns, like a turntable, is not supported.",
				vformat("body=%s constant_angular_velocity=%v mode=%d", to_string(), angular_surface_velocity, (int)mode),
				"surface_angular_velocity");
	}

	// Box3D takes the velocity of the surface in the space of the shape, so it has to follow the orientation of the body.
	const b3Vec3 local_velocity = b3Body_GetLocalVector(body_id, to_b3(linear_surface_velocity));

	for (Box3DShapeInstance *instance : shapes) {
		for (const b3ShapeId &id : instance->built_shapes) {
			b3SurfaceMaterial material = b3Shape_GetSurfaceMaterial(id);
			material.tangentVelocity = local_velocity;
			b3Shape_SetSurfaceMaterial(id, material);
		}
	}
}

void Box3DBody3D::_update_joint_constraints() {
	for (Box3DJoint3D *joint : joints) {
		joint->request_rebuild();
	}
}

void Box3DBody3D::_destroy_joint_constraints() {
	for (Box3DJoint3D *joint : joints) {
		joint->destroy();
	}
}

void Box3DBody3D::_clear_areas() {
	if (!in_space()) {
		return;
	}

	areas.clear();
	_areas_changed();
}

void Box3DBody3D::_mode_changed() {
	_update_sleep_allowed();
	wake_up();
}

void Box3DBody3D::_shapes_committed() {
	_update_mass_properties();
	_update_joint_constraints();
	wake_up();
}

void Box3DBody3D::_space_changing() {
	Box3DObject3D::_space_changing();

	sleep_initially = is_sleeping();

	_destroy_joint_constraints();
	_clear_areas();
	_dequeue_call_queries();
}

void Box3DBody3D::_space_changed() {
	Box3DObject3D::_space_changed();

	kinematic_transform = get_transform_unscaled();

	_update_joint_constraints();
	_update_sleep_allowed();
	_update_environmental_properties();
}

void Box3DBody3D::_areas_changed() {
	has_point_gravity = false;
	for (Box3DArea3D *area : areas) {
		if (area->is_point_gravity()) {
			has_point_gravity = true;
			break;
		}
	}

	const Vector3 previous_total_gravity = total_gravity;
	_update_environmental_properties();

	if (!total_gravity.is_equal_approx(previous_total_gravity)) {
		wake_up();
	}
}

void Box3DBody3D::_joints_changed() {
	wake_up();
}

void Box3DBody3D::_motion_changed() {
	wake_up();
}

void Box3DBody3D::_axis_lock_changed() {
	if (in_space()) {
		b3Body_SetMotionLocks(body_id, _calculate_motion_locks());
	}

	wake_up();
}

void Box3DBody3D::_contact_reporting_changed() {
	if (in_space()) {
		if (reports_contacts()) {
			space->register_contact_reporter(this);
		} else {
			space->unregister_contact_reporter(this);
		}

		b3Body_EnableContactRecycling(body_id, !reports_contacts());
	}

	_update_sleep_allowed();
	wake_up();
}

void Box3DBody3D::_sleep_allowed_changed() {
	_update_sleep_allowed();
	wake_up();
}

Box3DBody3D::Box3DBody3D() :
		Box3DObject3D(OBJECT_TYPE_BODY),
		call_queries_element(this),
		force_element(this),
		kinematic_element(this) {
}

Box3DBody3D::~Box3DBody3D() {
	if (direct_state != nullptr) {
		memdelete(direct_state);
		direct_state = nullptr;
	}
}

void Box3DBody3D::set_transform(Transform3D p_transform) {
	ERR_FAIL_COND_MSG(!p_transform.is_finite(), vformat("A non-finite transform was passed to physics body '%s'. It will be ignored.", to_string()));

	Vector3 new_scale;
	if (unlikely(!Box3DMath::decompose(p_transform.basis, new_scale))) {
		WARN_PRINT(vformat("An invalid transform was passed to physics body '%s'. The basis was singular, which is not supported by Box3D. This is likely caused by one or more axes having a scale of zero. The basis (and thus its scale) will be treated as identity.", to_string()));
	}

	// Ideally we would do an exact comparison here, but due to floating-point precision this would be invalidated very often.
	if (!scale.is_equal_approx(new_scale)) {
		scale = new_scale;
		shapes_changed();
	}

	if (!in_space()) {
		transform_unscaled = p_transform;
	} else if (is_kinematic()) {
		kinematic_transform = p_transform;
		kinematic_move_pending = true;
		space->enqueue_kinematic(&kinematic_element);
	} else {
		b3Body_SetTransform(body_id, to_b3_pos(p_transform.origin), to_b3(p_transform.basis));
	}

	_motion_changed();
}

bool Box3DBody3D::apply_kinematic_move(float p_step) {
	if (!in_space() || !is_kinematic()) {
		kinematic_move_pending = false;
		return false;
	}

	if (kinematic_move_pending) {
		kinematic_move_pending = false;
		b3Body_SetTargetTransform(body_id, to_b3_world(kinematic_transform), p_step, true);
		return true;
	}

	b3Body_SetLinearVelocity(body_id, b3Vec3_zero);
	b3Body_SetAngularVelocity(body_id, b3Vec3_zero);
	return false;
}

Variant Box3DBody3D::get_state(PhysicsServer3D::BodyState p_state) const {
	switch (p_state) {
		case PhysicsServer3D::BODY_STATE_TRANSFORM: {
			return get_transform_scaled();
		}
		case PhysicsServer3D::BODY_STATE_LINEAR_VELOCITY: {
			return in_space() ? get_linear_velocity() : linear_velocity_cache;
		}
		case PhysicsServer3D::BODY_STATE_ANGULAR_VELOCITY: {
			return in_space() ? get_angular_velocity() : angular_velocity_cache;
		}
		case PhysicsServer3D::BODY_STATE_SLEEPING: {
			return is_sleeping();
		}
		case PhysicsServer3D::BODY_STATE_CAN_SLEEP: {
			return is_sleep_allowed();
		}
		default: {
			ERR_FAIL_V_MSG(Variant(), vformat("Unhandled body state: '%d'. This should not happen. Please report this.", p_state));
		}
	}
}

void Box3DBody3D::set_state(PhysicsServer3D::BodyState p_state, const Variant &p_value) {
	switch (p_state) {
		case PhysicsServer3D::BODY_STATE_TRANSFORM: {
			set_transform(p_value);
		} break;
		case PhysicsServer3D::BODY_STATE_LINEAR_VELOCITY: {
			set_linear_velocity(p_value);
		} break;
		case PhysicsServer3D::BODY_STATE_ANGULAR_VELOCITY: {
			set_angular_velocity(p_value);
		} break;
		case PhysicsServer3D::BODY_STATE_SLEEPING: {
			set_is_sleeping(p_value);
		} break;
		case PhysicsServer3D::BODY_STATE_CAN_SLEEP: {
			set_is_sleep_allowed(p_value);
		} break;
		default: {
			ERR_FAIL_MSG(vformat("Unhandled body state: '%d'. This should not happen. Please report this.", p_state));
		} break;
	}
}

Variant Box3DBody3D::get_param(PhysicsServer3D::BodyParameter p_param) const {
	switch (p_param) {
		case PhysicsServer3D::BODY_PARAM_BOUNCE: {
			return get_bounce();
		}
		case PhysicsServer3D::BODY_PARAM_FRICTION: {
			return get_friction();
		}
		case PhysicsServer3D::BODY_PARAM_MASS: {
			return get_mass();
		}
		case PhysicsServer3D::BODY_PARAM_INERTIA: {
			return get_inertia();
		}
		case PhysicsServer3D::BODY_PARAM_CENTER_OF_MASS: {
			return get_center_of_mass_custom();
		}
		case PhysicsServer3D::BODY_PARAM_GRAVITY_SCALE: {
			return get_gravity_scale();
		}
		case PhysicsServer3D::BODY_PARAM_LINEAR_DAMP_MODE: {
			return get_linear_damp_mode();
		}
		case PhysicsServer3D::BODY_PARAM_ANGULAR_DAMP_MODE: {
			return get_angular_damp_mode();
		}
		case PhysicsServer3D::BODY_PARAM_LINEAR_DAMP: {
			return get_linear_damp();
		}
		case PhysicsServer3D::BODY_PARAM_ANGULAR_DAMP: {
			return get_angular_damp();
		}
		default: {
			ERR_FAIL_V_MSG(Variant(), vformat("Unhandled body parameter: '%d'. This should not happen. Please report this.", p_param));
		}
	}
}

void Box3DBody3D::set_param(PhysicsServer3D::BodyParameter p_param, const Variant &p_value) {
	switch (p_param) {
		case PhysicsServer3D::BODY_PARAM_BOUNCE: {
			set_bounce(p_value);
		} break;
		case PhysicsServer3D::BODY_PARAM_FRICTION: {
			set_friction(p_value);
		} break;
		case PhysicsServer3D::BODY_PARAM_MASS: {
			set_mass(p_value);
		} break;
		case PhysicsServer3D::BODY_PARAM_INERTIA: {
			set_inertia(p_value);
		} break;
		case PhysicsServer3D::BODY_PARAM_CENTER_OF_MASS: {
			set_center_of_mass_custom(p_value);
		} break;
		case PhysicsServer3D::BODY_PARAM_GRAVITY_SCALE: {
			set_gravity_scale(p_value);
		} break;
		case PhysicsServer3D::BODY_PARAM_LINEAR_DAMP_MODE: {
			set_linear_damp_mode((DampMode)(int)p_value);
		} break;
		case PhysicsServer3D::BODY_PARAM_ANGULAR_DAMP_MODE: {
			set_angular_damp_mode((DampMode)(int)p_value);
		} break;
		case PhysicsServer3D::BODY_PARAM_LINEAR_DAMP: {
			set_linear_damp(p_value);
		} break;
		case PhysicsServer3D::BODY_PARAM_ANGULAR_DAMP: {
			set_angular_damp(p_value);
		} break;
		default: {
			ERR_FAIL_MSG(vformat("Unhandled body parameter: '%d'. This should not happen. Please report this.", p_param));
		} break;
	}
}

void Box3DBody3D::set_custom_integrator(bool p_enabled) {
	if (custom_integrator == p_enabled) {
		return;
	}

	custom_integrator = p_enabled;

	_update_environmental_properties();
	_motion_changed();
}

bool Box3DBody3D::is_sleeping() const {
	if (!in_space()) {
		return sleep_initially;
	}

	return !b3Body_IsAwake(body_id);
}

bool Box3DBody3D::is_sleep_actually_allowed() const {
	return sleep_allowed && !(is_kinematic() && reports_contacts());
}

void Box3DBody3D::set_is_sleeping(bool p_enabled) {
	if (!in_space()) {
		sleep_initially = p_enabled;
		return;
	}

	if (is_static()) {
		return;
	}

	if (p_enabled == !b3Body_IsAwake(body_id)) {
		return;
	}

	b3Body_SetAwake(body_id, !p_enabled);
}

void Box3DBody3D::set_is_sleep_allowed(bool p_enabled) {
	if (sleep_allowed == p_enabled) {
		return;
	}

	sleep_allowed = p_enabled;

	_sleep_allowed_changed();
}

Basis Box3DBody3D::get_principal_inertia_axes() const {
	ERR_FAIL_COND_V_MSG(!in_space(), Basis(), vformat("Failed to retrieve principal inertia axes of '%s'. Doing so without a physics space is not supported when using Box3D. If this relates to a node, try adding the node to a scene tree first.", to_string()));

	if (unlikely(!is_rigid())) {
		return Basis();
	}

	// Box3D keeps the inertia tensor in the frame of the body, so the principal axes are the body's own axes.
	return get_basis();
}

Vector3 Box3DBody3D::get_inverse_inertia() const {
	ERR_FAIL_COND_V_MSG(!in_space(), Vector3(), vformat("Failed to retrieve inverse inertia of '%s'. Doing so without a physics space is not supported when using Box3D. If this relates to a node, try adding the node to a scene tree first.", to_string()));

	if (unlikely(!is_rigid())) {
		return Vector3();
	}

	const b3Matrix3 local_inertia = b3Body_GetLocalRotationalInertia(body_id);
	return Vector3(
			local_inertia.cx.x > 0.0f ? 1.0f / local_inertia.cx.x : 0.0f,
			local_inertia.cy.y > 0.0f ? 1.0f / local_inertia.cy.y : 0.0f,
			local_inertia.cz.z > 0.0f ? 1.0f / local_inertia.cz.z : 0.0f);
}

Basis Box3DBody3D::get_inverse_inertia_tensor() const {
	ERR_FAIL_COND_V_MSG(!in_space(), Basis(), vformat("Failed to retrieve inverse inertia tensor of '%s'. Doing so without a physics space is not supported when using Box3D. If this relates to a node, try adding the node to a scene tree first.", to_string()));

	if (unlikely(!is_rigid())) {
		return Basis();
	}

	return to_godot(b3Body_GetWorldInverseRotationalInertia(body_id));
}

float Box3DBody3D::get_inverse_mass() const {
	if (!in_space() || !is_rigid()) {
		return 0.0f;
	}

	return b3Body_GetInverseMass(body_id);
}

void Box3DBody3D::set_linear_velocity(const Vector3 &p_velocity) {
	ERR_FAIL_COND_MSG(!p_velocity.is_finite(), vformat("A non-finite linear velocity was passed to physics body '%s'. It will be ignored.", to_string()));

	if (is_static() || is_kinematic()) {
		linear_surface_velocity = p_velocity;
		_update_surface_velocity();
	} else {
		if (!in_space()) {
			linear_velocity_cache = p_velocity;
		} else {
			b3Body_SetLinearVelocity(body_id, to_b3(p_velocity));
		}
	}

	_motion_changed();
}

void Box3DBody3D::set_angular_velocity(const Vector3 &p_velocity) {
	ERR_FAIL_COND_MSG(!p_velocity.is_finite(), vformat("A non-finite angular velocity was passed to physics body '%s'. It will be ignored.", to_string()));

	if (is_static() || is_kinematic()) {
		angular_surface_velocity = p_velocity;
		_update_surface_velocity();
	} else {
		if (!in_space()) {
			angular_velocity_cache = p_velocity;
		} else {
			b3Body_SetAngularVelocity(body_id, to_b3(p_velocity));
		}
	}

	_motion_changed();
}

void Box3DBody3D::set_axis_velocity(const Vector3 &p_axis_velocity) {
	const Vector3 axis = p_axis_velocity.normalized();

	Vector3 linear_velocity = in_space() ? get_linear_velocity() : linear_velocity_cache;
	linear_velocity -= axis * axis.dot(linear_velocity);
	linear_velocity += p_axis_velocity;

	set_linear_velocity(linear_velocity);
}

Vector3 Box3DBody3D::get_velocity_at_position(const Vector3 &p_position) const {
	if (unlikely(!in_space())) {
		return Vector3();
	}

	const Vector3 total_linear_velocity = get_linear_velocity() + linear_surface_velocity;
	const Vector3 total_angular_velocity = get_angular_velocity() + angular_surface_velocity;
	const Vector3 com_to_pos = p_position - get_center_of_mass();

	return total_linear_velocity + total_angular_velocity.cross(com_to_pos);
}

void Box3DBody3D::set_center_of_mass_custom(const Vector3 &p_center_of_mass) {
	if (custom_center_of_mass && p_center_of_mass == center_of_mass_custom) {
		return;
	}

	custom_center_of_mass = true;
	center_of_mass_custom = p_center_of_mass;

	_update_mass_properties();
}

void Box3DBody3D::set_max_contacts_reported(int p_count) {
	ERR_FAIL_INDEX(p_count, MAX_CONTACTS_REPORTED_3D_MAX);

	if (unlikely((int)contacts.size() == p_count)) {
		return;
	}

	contacts.resize(p_count);
	contact_count = MIN(contact_count, p_count);

	_contact_reporting_changed();
}

void Box3DBody3D::add_contact(const Box3DObject3D *p_collider, float p_depth, int p_shape_index, int p_collider_shape_index, const Vector3 &p_normal, const Vector3 &p_position, const Vector3 &p_collider_position, const Vector3 &p_velocity, const Vector3 &p_collider_velocity, const Vector3 &p_impulse) {
	const int max_contacts = get_max_contacts_reported();

	if (max_contacts == 0) {
		return;
	}

	Contact *contact = nullptr;

	if (contact_count < max_contacts) {
		contact = &contacts[contact_count++];
	} else {
		Contact *shallowest_contact = &contacts[0];

		for (int i = 1; i < (int)contacts.size(); i++) {
			Contact &other_contact = contacts[i];
			if (other_contact.depth < shallowest_contact->depth) {
				shallowest_contact = &other_contact;
			}
		}

		if (shallowest_contact->depth < p_depth) {
			contact = shallowest_contact;
		}
	}

	if (contact != nullptr) {
		contact->normal = p_normal;
		contact->position = p_position;
		contact->collider_position = p_collider_position;
		contact->velocity = p_velocity;
		contact->collider_velocity = p_collider_velocity;
		contact->impulse = p_impulse;
		contact->collider_id = p_collider->get_instance_id();
		contact->collider_rid = p_collider->get_rid();
		contact->shape_index = p_shape_index;
		contact->collider_shape_index = p_collider_shape_index;
		contact->depth = p_depth;
	}
}

void Box3DBody3D::collect_contacts() {
	if (!in_space() || contacts.is_empty()) {
		return;
	}

	const int capacity = b3Body_GetContactCapacity(body_id);
	if (capacity <= 0) {
		return;
	}

	LocalVector<b3ContactData> data;
	data.resize(capacity);

	const int count = b3Body_GetContactData(body_id, data.ptr(), capacity);

	for (int i = 0; i < count; i++) {
		const b3ContactData &contact = data[i];

		const b3BodyId body_id_a = b3Shape_GetBody(contact.shapeIdA);
		const bool self_is_a = B3_ID_EQUALS(body_id_a, body_id);

		const b3ShapeId self_shape = self_is_a ? contact.shapeIdA : contact.shapeIdB;
		const b3ShapeId other_shape = self_is_a ? contact.shapeIdB : contact.shapeIdA;

		const Box3DShapeInstance *self_instance = (const Box3DShapeInstance *)b3Shape_GetUserData(self_shape);
		const Box3DShapeInstance *other_instance = (const Box3DShapeInstance *)b3Shape_GetUserData(other_shape);

		if (self_instance == nullptr || other_instance == nullptr) {
			continue;
		}

		const Box3DObject3D *other = other_instance->get_owner();
		if (!other->is_body()) {
			continue;
		}

		const b3BodyId other_body_id = b3Shape_GetBody(other_shape);

		const int self_shape_index = find_shape_index(self_instance->get_id());
		const int other_shape_index = other->find_shape_index(other_instance->get_id());

		for (int m = 0; m < contact.manifoldCount; m++) {
			const b3Manifold &manifold = contact.manifolds[m];
			const Vector3 world_normal = to_godot(manifold.normal);
			const Vector3 normal = self_is_a ? -world_normal : world_normal;

			const Vector3 center_a = pos_to_godot(b3Body_GetWorldCenter(self_is_a ? body_id : other_body_id));
			const Vector3 center_b = pos_to_godot(b3Body_GetWorldCenter(self_is_a ? other_body_id : body_id));

			for (int p = 0; p < manifold.pointCount; p++) {
				const b3ManifoldPoint &point = manifold.points[p];

				if (point.totalNormalImpulse <= 0.0f && point.separation > B3_LINEAR_SLOP) {
					continue;
				}

				const Vector3 point_a = center_a + to_godot(point.anchorA);
				const Vector3 point_b = center_b + to_godot(point.anchorB);

				const Vector3 point_self = self_is_a ? point_a : point_b;
				const Vector3 point_other = self_is_a ? point_b : point_a;

				const Vector3 velocity_self = to_godot(b3Body_GetWorldPointVelocity(body_id, to_b3_pos(point_self)));
				const Vector3 velocity_other = to_godot(b3Body_GetWorldPointVelocity(other_body_id, to_b3_pos(point_other)));

				add_contact(other, MAX(-point.separation, 0.0f), self_shape_index, other_shape_index, normal, point_self, point_other, velocity_self, velocity_other, normal * point.totalNormalImpulse);
			}
		}
	}
}

void Box3DBody3D::reset_mass_properties() {
	if (custom_center_of_mass) {
		custom_center_of_mass = false;
		center_of_mass_custom.zero();
	}

	inertia.zero();

	_update_mass_properties();
}

void Box3DBody3D::apply_force(const Vector3 &p_force, const Vector3 &p_position) {
	ERR_FAIL_COND_MSG(!p_force.is_finite(), vformat("A non-finite force was passed to physics body '%s'. It will be ignored.", to_string()));

	ERR_FAIL_COND_MSG(!in_space(), vformat("Failed to apply force to '%s'. Doing so without a physics space is not supported when using Box3D. If this relates to a node, try adding the node to a scene tree first.", to_string()));

	if (unlikely(!is_rigid()) || custom_integrator || p_force == Vector3()) {
		return;
	}

	b3Body_ApplyForce(body_id, to_b3(p_force), to_b3_pos(get_position() + p_position), true);
}

void Box3DBody3D::apply_central_force(const Vector3 &p_force) {
	ERR_FAIL_COND_MSG(!p_force.is_finite(), vformat("A non-finite force was passed to physics body '%s'. It will be ignored.", to_string()));

	ERR_FAIL_COND_MSG(!in_space(), vformat("Failed to apply central force to '%s'. Doing so without a physics space is not supported when using Box3D. If this relates to a node, try adding the node to a scene tree first.", to_string()));

	if (unlikely(!is_rigid()) || custom_integrator || p_force == Vector3()) {
		return;
	}

	b3Body_ApplyForceToCenter(body_id, to_b3(p_force), true);
}

void Box3DBody3D::apply_impulse(const Vector3 &p_impulse, const Vector3 &p_position) {
	ERR_FAIL_COND_MSG(!p_impulse.is_finite(), vformat("A non-finite impulse was passed to physics body '%s'. It will be ignored.", to_string()));

	ERR_FAIL_COND_MSG(!in_space(), vformat("Failed to apply impulse to '%s'. Doing so without a physics space is not supported when using Box3D. If this relates to a node, try adding the node to a scene tree first.", to_string()));

	if (unlikely(!is_rigid()) || p_impulse == Vector3()) {
		return;
	}

	b3Body_ApplyLinearImpulse(body_id, to_b3(p_impulse), to_b3_pos(get_position() + p_position), true);
}

void Box3DBody3D::apply_central_impulse(const Vector3 &p_impulse) {
	ERR_FAIL_COND_MSG(!p_impulse.is_finite(), vformat("A non-finite impulse was passed to physics body '%s'. It will be ignored.", to_string()));

	ERR_FAIL_COND_MSG(!in_space(), vformat("Failed to apply central impulse to '%s'. Doing so without a physics space is not supported when using Box3D. If this relates to a node, try adding the node to a scene tree first.", to_string()));

	if (unlikely(!is_rigid()) || p_impulse == Vector3()) {
		return;
	}

	b3Body_ApplyLinearImpulseToCenter(body_id, to_b3(p_impulse), true);
}

void Box3DBody3D::apply_torque(const Vector3 &p_torque) {
	ERR_FAIL_COND_MSG(!p_torque.is_finite(), vformat("A non-finite torque was passed to physics body '%s'. It will be ignored.", to_string()));

	ERR_FAIL_COND_MSG(!in_space(), vformat("Failed to apply torque to '%s'. Doing so without a physics space is not supported when using Box3D. If this relates to a node, try adding the node to a scene tree first.", to_string()));

	if (unlikely(!is_rigid()) || custom_integrator || p_torque == Vector3()) {
		return;
	}

	b3Body_ApplyTorque(body_id, to_b3(p_torque), true);
}

void Box3DBody3D::apply_torque_impulse(const Vector3 &p_impulse) {
	ERR_FAIL_COND_MSG(!p_impulse.is_finite(), vformat("A non-finite torque impulse was passed to physics body '%s'. It will be ignored.", to_string()));

	ERR_FAIL_COND_MSG(!in_space(), vformat("Failed to apply torque impulse to '%s'. Doing so without a physics space is not supported when using Box3D. If this relates to a node, try adding the node to a scene tree first.", to_string()));

	if (unlikely(!is_rigid()) || p_impulse == Vector3()) {
		return;
	}

	b3Body_ApplyAngularImpulse(body_id, to_b3(p_impulse), true);
}

void Box3DBody3D::add_constant_central_force(const Vector3 &p_force) {
	if (p_force == Vector3()) {
		return;
	}

	constant_force += p_force;

	_update_force_registration();
	_motion_changed();
}

void Box3DBody3D::add_constant_force(const Vector3 &p_force, const Vector3 &p_position) {
	if (p_force == Vector3()) {
		return;
	}

	constant_force += p_force;
	constant_torque += (p_position - get_center_of_mass_relative()).cross(p_force);

	_update_force_registration();
	_motion_changed();
}

void Box3DBody3D::add_constant_torque(const Vector3 &p_torque) {
	if (p_torque == Vector3()) {
		return;
	}

	constant_torque += p_torque;

	_update_force_registration();
	_motion_changed();
}

void Box3DBody3D::set_constant_force(const Vector3 &p_force) {
	if (constant_force == p_force) {
		return;
	}

	constant_force = p_force;

	_update_force_registration();
	_motion_changed();
}

void Box3DBody3D::set_constant_torque(const Vector3 &p_torque) {
	if (constant_torque == p_torque) {
		return;
	}

	constant_torque = p_torque;

	_update_force_registration();
	_motion_changed();
}

void Box3DBody3D::add_collision_exception(const RID &p_excepted_body) {
	exceptions.push_back(p_excepted_body);
}

void Box3DBody3D::remove_collision_exception(const RID &p_excepted_body) {
	exceptions.erase(p_excepted_body);
}

bool Box3DBody3D::has_collision_exception(const RID &p_excepted_body) const {
	return exceptions.find(p_excepted_body) >= 0;
}

void Box3DBody3D::add_area(Box3DArea3D *p_area) {
	int i = 0;
	for (; i < (int)areas.size(); i++) {
		if (p_area->get_priority() > areas[i]->get_priority()) {
			break;
		}
	}

	areas.insert(i, p_area);

	_areas_changed();
}

void Box3DBody3D::remove_area(Box3DArea3D *p_area) {
	areas.erase(p_area);

	_areas_changed();
}

void Box3DBody3D::update_area(Box3DArea3D *p_area, bool p_priority_changed) {
	if (p_priority_changed) {
		areas.erase(p_area);
		add_area(p_area);
	} else {
		_areas_changed();
	}
}

void Box3DBody3D::add_joint(Box3DJoint3D *p_joint) {
	joints.push_back(p_joint);

	_joints_changed();
}

void Box3DBody3D::remove_joint(Box3DJoint3D *p_joint) {
	joints.erase(p_joint);

	_joints_changed();
}

void Box3DBody3D::notify_environment_changed() {
	_update_environmental_properties();
}

void Box3DBody3D::call_queries() {
	if (custom_integration_callback.is_valid()) {
		const Variant direct_state_variant = get_direct_state();
		const Variant *args[2] = { &direct_state_variant, &custom_integration_userdata };
		const int argc = custom_integration_userdata.get_type() != Variant::NIL ? 2 : 1;

		Callable::CallError ce;
		Variant ret;
		custom_integration_callback.callp(args, argc, ret, ce);

		if (unlikely(ce.error != Callable::CallError::CALL_OK)) {
			ERR_PRINT_ONCE(vformat("Failed to call force integration callback for '%s'. It returned the following error: '%s'.", to_string(), Variant::get_callable_error_text(custom_integration_callback, args, argc, ce)));
		}
	}

	if (state_sync_callback.is_valid()) {
		const Variant direct_state_variant = get_direct_state();
		const Variant *args[1] = { &direct_state_variant };

		Callable::CallError ce;
		Variant ret;
		state_sync_callback.callp(args, 1, ret, ce);

		if (unlikely(ce.error != Callable::CallError::CALL_OK)) {
			ERR_PRINT_ONCE(vformat("Failed to call state synchronization callback for '%s'. It returned the following error: '%s'.", to_string(), Variant::get_callable_error_text(state_sync_callback, args, 1, ce)));
		}
	}
}

void Box3DBody3D::pre_step(float p_step) {
	if (!in_space() || !is_rigid()) {
		return;
	}

	if (_needs_update_environmental_properties()) {
		_update_environmental_properties();
	}

	if (custom_integrator || !b3Body_IsAwake(body_id)) {
		return;
	}

	const Vector3 force = constant_force + extra_gravity_acceleration * b3Body_GetMass(body_id);

	if (force != Vector3()) {
		b3Body_ApplyForceToCenter(body_id, to_b3(force), false);
	}

	if (constant_torque != Vector3()) {
		b3Body_ApplyTorque(body_id, to_b3(constant_torque), false);
	}
}

Box3DPhysicsDirectBodyState3D *Box3DBody3D::get_direct_state() {
	if (direct_state == nullptr) {
		direct_state = memnew(Box3DPhysicsDirectBodyState3D(this));
	}

	return direct_state;
}

void Box3DBody3D::set_mode(PhysicsServer3D::BodyMode p_mode) {
	if (p_mode == mode) {
		return;
	}

	const PhysicsServer3D::BodyMode previous_mode = mode;
	mode = p_mode;

	if (in_space()) {
		if (previous_mode == PhysicsServer3D::BODY_MODE_KINEMATIC) {
			space->dequeue_kinematic(&kinematic_element);
			kinematic_move_pending = false;
		}

		b3Body_SetType(body_id, _get_body_type());
		b3Body_SetMotionLocks(body_id, _calculate_motion_locks());

		if (is_kinematic()) {
			b3Body_SetLinearVelocity(body_id, b3Vec3_zero);
			b3Body_SetAngularVelocity(body_id, b3Vec3_zero);
			kinematic_transform = get_transform_unscaled();
		}

		linear_surface_velocity = Vector3();
		angular_surface_velocity = Vector3();

		// Changing the body type resets the mass properties, and the shapes hold the material.
		_update_mass_properties();
		_update_environmental_properties();
	}

	_mode_changed();
}

void Box3DBody3D::set_ccd_enabled(bool p_enabled) {
	ccd_enabled = p_enabled;

	if (in_space()) {
		b3Body_SetBullet(body_id, p_enabled);
	}
}

void Box3DBody3D::set_mass(float p_mass) {
	if (p_mass != mass) {
		mass = p_mass;
		_update_mass_properties();
	}
}

void Box3DBody3D::set_inertia(const Vector3 &p_inertia) {
	if (p_inertia != inertia) {
		inertia = p_inertia;
		_update_mass_properties();
	}
}

void Box3DBody3D::set_bounce(float p_bounce) {
	bounce = p_bounce;
	_update_material();
}

void Box3DBody3D::set_friction(float p_friction) {
	friction = p_friction;
	_update_material();
}

void Box3DBody3D::set_gravity_scale(float p_scale) {
	gravity_scale = p_scale;
	_update_environmental_properties();
}

void Box3DBody3D::set_linear_damp(float p_damp) {
	p_damp = MAX(0.0f, p_damp);

	if (p_damp == linear_damp) {
		return;
	}

	linear_damp = p_damp;

	_update_environmental_properties();
}

void Box3DBody3D::set_angular_damp(float p_damp) {
	p_damp = MAX(0.0f, p_damp);

	if (p_damp == angular_damp) {
		return;
	}

	angular_damp = p_damp;

	_update_environmental_properties();
}

void Box3DBody3D::set_linear_damp_mode(DampMode p_mode) {
	if (p_mode == linear_damp_mode) {
		return;
	}

	linear_damp_mode = p_mode;

	_update_environmental_properties();
}

void Box3DBody3D::set_angular_damp_mode(DampMode p_mode) {
	if (p_mode == angular_damp_mode) {
		return;
	}

	angular_damp_mode = p_mode;

	_update_environmental_properties();
}

bool Box3DBody3D::is_axis_locked(PhysicsServer3D::BodyAxis p_axis) const {
	return (locked_axes & (uint32_t)p_axis) != 0;
}

void Box3DBody3D::set_axis_lock(PhysicsServer3D::BodyAxis p_axis, bool p_enabled) {
	const uint32_t previous_locked_axes = locked_axes;

	if (p_enabled) {
		locked_axes |= (uint32_t)p_axis;
	} else {
		locked_axes &= ~(uint32_t)p_axis;
	}

	if (previous_locked_axes != locked_axes) {
		_axis_lock_changed();
	}
}

bool Box3DBody3D::can_interact_with(const Box3DBody3D &p_other) const {
	return (can_collide_with(p_other) || p_other.can_collide_with(*this)) && !has_collision_exception(p_other.get_rid()) && !p_other.has_collision_exception(rid);
}

bool Box3DBody3D::can_interact_with(const Box3DArea3D &p_other) const {
	return p_other.can_interact_with(*this);
}
