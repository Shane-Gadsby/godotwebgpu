/**************************************************************************/
/*  box3d_space_3d.cpp                                                    */
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

#include "box3d_space_3d.h"

#include "../box3d_physics_server_3d.h"
#include "../box3d_project_settings.h"
#include "../joints/box3d_joint_3d.h"
#include "../misc/box3d_diagnostics.h"
#include "../objects/box3d_area_3d.h"
#include "../objects/box3d_body_3d.h"
#include "box3d_physics_direct_space_state_3d.h"

#include "core/os/os.h"

namespace {

constexpr double SPACE_DEFAULT_CONTACT_RECYCLE_RADIUS = 0.01;
constexpr double SPACE_DEFAULT_CONTACT_MAX_SEPARATION = 0.05;
constexpr double SPACE_DEFAULT_CONTACT_MAX_ALLOWED_PENETRATION = 0.01;
constexpr double SPACE_DEFAULT_CONTACT_DEFAULT_BIAS = 0.8;
constexpr double SPACE_DEFAULT_SLEEP_THRESHOLD_LINEAR = 0.1;
constexpr double SPACE_DEFAULT_SLEEP_THRESHOLD_ANGULAR = 8.0 * Math::PI / 180;
constexpr double SPACE_DEFAULT_SOLVER_ITERATIONS = 8;

int _resolve_worker_count() {
#ifdef THREADS_ENABLED
	const int configured = Box3DProjectSettings::simulation_worker_threads;
	if (configured > 0) {
		return CLAMP(configured, 1, B3_MAX_WORKERS);
	}

	return CLAMP(OS::get_singleton()->get_processor_count() / 2, 1, 8);
#else
	return 1;
#endif
}

} // namespace

bool Box3DSpace3D::_custom_filter(b3ShapeId p_shape_a, b3ShapeId p_shape_b, void *p_context) {
	const Box3DShapeInstance *instance_a = (const Box3DShapeInstance *)b3Shape_GetUserData(p_shape_a);
	const Box3DShapeInstance *instance_b = (const Box3DShapeInstance *)b3Shape_GetUserData(p_shape_b);

	if (unlikely(instance_a == nullptr || instance_b == nullptr)) {
		return true;
	}

	const Box3DObject3D *object_a = instance_a->get_owner();
	const Box3DObject3D *object_b = instance_b->get_owner();

	// Sensor events pass the sensor first, so areas can be evaluated directionally.
	return object_a->can_interact_with(*object_b);
}

float Box3DSpace3D::_combine_friction(float p_friction_a, uint64_t p_material_a, float p_friction_b, uint64_t p_material_b) {
	return Math::abs(MIN(p_friction_a, p_friction_b));
}

float Box3DSpace3D::_combine_restitution(float p_restitution_a, uint64_t p_material_a, float p_restitution_b, uint64_t p_material_b) {
	return CLAMP(p_restitution_a + p_restitution_b, 0.0f, 1.0f);
}

Box3DSpace3D::Box3DSpace3D() {
	b3WorldDef def = b3DefaultWorldDef();
	def.gravity = b3Vec3_zero;
	def.enableSleep = Box3DProjectSettings::simulation_allow_sleep;
	def.enableContinuous = Box3DProjectSettings::simulation_continuous;
	def.contactHertz = Box3DProjectSettings::simulation_contact_hertz;
	def.contactDampingRatio = Box3DProjectSettings::simulation_contact_damping_ratio;
	def.contactSpeed = Box3DProjectSettings::simulation_contact_speed;
	def.restitutionThreshold = Box3DProjectSettings::simulation_restitution_threshold;
	def.maximumLinearSpeed = Box3DProjectSettings::max_linear_velocity;
	def.frictionCallback = _combine_friction;
	def.restitutionCallback = _combine_restitution;
	def.workerCount = _resolve_worker_count();
	def.userData = this;

	world = b3CreateWorld(&def);

	ERR_FAIL_COND_MSG(!b3World_IsValid(world), "Failed to create a Box3D world. Box3D only supports a limited number of simultaneous worlds.");

	b3World_SetCustomFilterCallback(world, _custom_filter, this);
}

Box3DSpace3D::~Box3DSpace3D() {
	if (direct_state != nullptr) {
		memdelete(direct_state);
		direct_state = nullptr;
	}

	if (b3World_IsValid(world)) {
		b3DestroyWorld(world);
		world = b3_nullWorldId;
	}
}

b3BodyId Box3DSpace3D::get_anchor_body() {
	if (B3_IS_NULL(anchor_body)) {
		b3BodyDef def = b3DefaultBodyDef();
		def.type = b3_staticBody;
		def.enableSleep = false;
		anchor_body = b3CreateBody(world, &def);
	}

	return anchor_body;
}

void Box3DSpace3D::_update_world_gravity() {
	if (default_area == nullptr) {
		return;
	}

	uses_world_gravity_flag = !default_area->is_point_gravity();
	world_gravity = uses_world_gravity_flag ? default_area->get_gravity_vector() * default_area->get_gravity() : Vector3();

	b3World_SetGravity(world, to_b3(world_gravity));
}

void Box3DSpace3D::increment_default_area_changed_count() {
	default_area_changed_count++;
	environment_refresh_needed = true;
}

void Box3DSpace3D::_pre_step(float p_step) {
	flush_pending_shapes();

	if (environment_refresh_needed) {
		environment_refresh_needed = false;

		_update_world_gravity();

		for (Box3DBody3D *body : bodies) {
			body->notify_environment_changed();
		}
	}

	SelfList<Box3DBody3D> *kinematic = kinematic_bodies_list.first();
	while (kinematic) {
		SelfList<Box3DBody3D> *next = kinematic->next();

		if (!kinematic->self()->apply_kinematic_move(p_step)) {
			kinematic_bodies_list.remove(kinematic);
		}

		kinematic = next;
	}

	for (SelfList<Box3DBody3D> *force_body = force_bodies_list.first(); force_body; force_body = force_body->next()) {
		force_body->self()->pre_step(p_step);
	}

	for (Box3DBody3D *body : contact_reporters) {
		if (b3Body_IsAwake(body->get_body_id())) {
			body->reset_contacts();
		}
	}
}

void Box3DSpace3D::_process_sensor_events() {
	const b3SensorEvents events = b3World_GetSensorEvents(world);

	for (int i = 0; i < events.endCount; i++) {
		const b3SensorEndTouchEvent &event = events.endEvents[i];

		if (!b3Shape_IsValid(event.sensorShapeId)) {
			continue;
		}

		const Box3DShapeInstance *sensor_instance = (const Box3DShapeInstance *)b3Shape_GetUserData(event.sensorShapeId);
		if (sensor_instance == nullptr) {
			continue;
		}

		if (Box3DArea3D *area = sensor_instance->get_owner()->as_area()) {
			area->shape_exited(event.visitorShapeId);
		}
	}

	for (int i = 0; i < events.beginCount; i++) {
		const b3SensorBeginTouchEvent &event = events.beginEvents[i];

		if (!b3Shape_IsValid(event.sensorShapeId) || !b3Shape_IsValid(event.visitorShapeId)) {
			continue;
		}

		const Box3DShapeInstance *sensor_instance = (const Box3DShapeInstance *)b3Shape_GetUserData(event.sensorShapeId);
		const Box3DShapeInstance *visitor_instance = (const Box3DShapeInstance *)b3Shape_GetUserData(event.visitorShapeId);
		if (sensor_instance == nullptr || visitor_instance == nullptr) {
			continue;
		}

		if (Box3DArea3D *area = sensor_instance->get_owner()->as_area()) {
			area->shape_entered(event.visitorShapeId, visitor_instance->get_owner(), visitor_instance->get_id(), sensor_instance->get_id());
		}
	}
}

void Box3DSpace3D::_post_step(float p_step) {
	const b3BodyEvents body_events = b3World_GetBodyEvents(world);

	for (int i = 0; i < body_events.moveCount; i++) {
		Box3DBody3D *body = (Box3DBody3D *)body_events.moveEvents[i].userData;

		if (body != nullptr) {
			body->notify_moved();
		}
	}

	_process_sensor_events();

	for (Box3DBody3D *body : contact_reporters) {
		if (b3Body_IsAwake(body->get_body_id())) {
			body->collect_contacts();
		}
	}
}

void Box3DSpace3D::step(float p_step) {
	stepping = true;
	last_step = p_step;

	_pre_step(p_step);

	b3World_Step(world, p_step, Box3DProjectSettings::simulation_sub_steps);

	_post_step(p_step);

	stepping = false;
}

void Box3DSpace3D::call_queries() {
	while (body_call_queries_list.first()) {
		Box3DBody3D *body = body_call_queries_list.first()->self();
		body_call_queries_list.remove(body_call_queries_list.first());
		body->call_queries();
	}

	while (area_call_queries_list.first()) {
		Box3DArea3D *area = area_call_queries_list.first()->self();
		area_call_queries_list.remove(area_call_queries_list.first());
		area->call_queries();
	}
}

double Box3DSpace3D::get_param(PhysicsServer3D::SpaceParameter p_param) const {
	switch (p_param) {
		case PhysicsServer3D::SPACE_PARAM_CONTACT_RECYCLE_RADIUS: {
			return SPACE_DEFAULT_CONTACT_RECYCLE_RADIUS;
		}
		case PhysicsServer3D::SPACE_PARAM_CONTACT_MAX_SEPARATION: {
			return SPACE_DEFAULT_CONTACT_MAX_SEPARATION;
		}
		case PhysicsServer3D::SPACE_PARAM_CONTACT_MAX_ALLOWED_PENETRATION: {
			return SPACE_DEFAULT_CONTACT_MAX_ALLOWED_PENETRATION;
		}
		case PhysicsServer3D::SPACE_PARAM_CONTACT_DEFAULT_BIAS: {
			return SPACE_DEFAULT_CONTACT_DEFAULT_BIAS;
		}
		case PhysicsServer3D::SPACE_PARAM_BODY_LINEAR_VELOCITY_SLEEP_THRESHOLD: {
			return SPACE_DEFAULT_SLEEP_THRESHOLD_LINEAR;
		}
		case PhysicsServer3D::SPACE_PARAM_BODY_ANGULAR_VELOCITY_SLEEP_THRESHOLD: {
			return SPACE_DEFAULT_SLEEP_THRESHOLD_ANGULAR;
		}
		case PhysicsServer3D::SPACE_PARAM_BODY_TIME_TO_SLEEP: {
			return B3_TIME_TO_SLEEP;
		}
		case PhysicsServer3D::SPACE_PARAM_SOLVER_ITERATIONS: {
			return SPACE_DEFAULT_SOLVER_ITERATIONS;
		}
		default: {
			ERR_FAIL_V_MSG(0.0, vformat("Unhandled space parameter: '%d'. This should not happen. Please report this.", p_param));
		}
	}
}

void Box3DSpace3D::set_param(PhysicsServer3D::SpaceParameter p_param, double p_value) {
	static const char *const names[] = {
		"contact recycle radius",
		"contact max separation",
		"contact max allowed penetration",
		"contact default bias",
		"linear velocity sleep threshold",
		"angular velocity sleep threshold",
		"body sleep time",
		"solver iterations",
	};

	ERR_FAIL_INDEX((int)p_param, 8);

	// Only report values that differ from what is already reported, since Godot sets every parameter from project settings.
	if (Math::is_equal_approx(p_value, get_param(p_param))) {
		return;
	}

	BOX3D_UNSUPPORTED_KEYED(vformat("Space-specific %s", names[p_param]),
			vformat("PhysicsServer3D.space_set_param() was called for a %s value.", names[p_param]),
			"Box3D has no per-space equivalent of this setting. Use the settings under 'physics/box3d_physics_3d/simulation' instead.",
			vformat("space=%s parameter=%d requested_value=%f", rid, (int)p_param, p_value),
			vformat("space_param_%d", (int)p_param));
}

Box3DPhysicsDirectSpaceState3D *Box3DSpace3D::get_direct_state() {
	if (direct_state == nullptr) {
		direct_state = memnew(Box3DPhysicsDirectSpaceState3D(this));
	}

	return direct_state;
}

void Box3DSpace3D::register_body(Box3DBody3D *p_body) {
	bodies.insert(p_body);
}

void Box3DSpace3D::unregister_body(Box3DBody3D *p_body) {
	bodies.erase(p_body);
}

void Box3DSpace3D::register_contact_reporter(Box3DBody3D *p_body) {
	contact_reporters.insert(p_body);
}

void Box3DSpace3D::unregister_contact_reporter(Box3DBody3D *p_body) {
	contact_reporters.erase(p_body);
}

void Box3DSpace3D::enqueue_call_queries(SelfList<Box3DBody3D> *p_body) {
	if (!p_body->in_list()) {
		body_call_queries_list.add(p_body);
	}
}

void Box3DSpace3D::enqueue_call_queries(SelfList<Box3DArea3D> *p_area) {
	if (!p_area->in_list()) {
		area_call_queries_list.add(p_area);
	}
}

void Box3DSpace3D::dequeue_call_queries(SelfList<Box3DBody3D> *p_body) {
	if (p_body->in_list()) {
		body_call_queries_list.remove(p_body);
	}
}

void Box3DSpace3D::dequeue_call_queries(SelfList<Box3DArea3D> *p_area) {
	if (p_area->in_list()) {
		area_call_queries_list.remove(p_area);
	}
}

void Box3DSpace3D::enqueue_shapes_changed(SelfList<Box3DObject3D> *p_object) {
	if (!p_object->in_list()) {
		shapes_changed_list.add(p_object);
	}
}

void Box3DSpace3D::dequeue_shapes_changed(SelfList<Box3DObject3D> *p_object) {
	if (p_object->in_list()) {
		shapes_changed_list.remove(p_object);
	}
}

void Box3DSpace3D::enqueue_forces(SelfList<Box3DBody3D> *p_body) {
	if (!p_body->in_list()) {
		force_bodies_list.add(p_body);
	}
}

void Box3DSpace3D::dequeue_forces(SelfList<Box3DBody3D> *p_body) {
	if (p_body->in_list()) {
		force_bodies_list.remove(p_body);
	}
}

void Box3DSpace3D::enqueue_kinematic(SelfList<Box3DBody3D> *p_body) {
	if (!p_body->in_list()) {
		kinematic_bodies_list.add(p_body);
	}
}

void Box3DSpace3D::dequeue_kinematic(SelfList<Box3DBody3D> *p_body) {
	if (p_body->in_list()) {
		kinematic_bodies_list.remove(p_body);
	}
}

void Box3DSpace3D::enqueue_joints_changed(SelfList<Box3DJoint3D> *p_joint) {
	if (!p_joint->in_list()) {
		joints_changed_list.add(p_joint);
	}
}

void Box3DSpace3D::dequeue_joints_changed(SelfList<Box3DJoint3D> *p_joint) {
	if (p_joint->in_list()) {
		joints_changed_list.remove(p_joint);
	}
}

void Box3DSpace3D::flush_pending_shapes() {
	while (shapes_changed_list.first()) {
		Box3DObject3D *object = shapes_changed_list.first()->self();
		object->commit_shapes();
	}

	while (joints_changed_list.first()) {
		Box3DJoint3D *joint = joints_changed_list.first()->self();
		joint->rebuild();
	}
}

int Box3DSpace3D::get_awake_body_count() const {
	return b3World_GetAwakeBodyCount(world);
}
