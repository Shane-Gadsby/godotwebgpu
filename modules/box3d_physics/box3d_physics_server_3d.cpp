/**************************************************************************/
/*  box3d_physics_server_3d.cpp                                           */
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

#include "box3d_physics_server_3d.h"

#include "box3d_project_settings.h"
#include "joints/box3d_joint_3d.h"
#include "misc/box3d_diagnostics.h"
#include "objects/box3d_area_3d.h"
#include "objects/box3d_body_3d.h"
#include "objects/box3d_physics_direct_body_state_3d.h"
#include "objects/box3d_soft_body_3d.h"
#include "shapes/box3d_shape_3d.h"
#include "spaces/box3d_physics_direct_space_state_3d.h"
#include "spaces/box3d_space_3d.h"

Box3DPhysicsServer3D::Box3DPhysicsServer3D(bool p_on_separate_thread) :
		on_separate_thread(p_on_separate_thread) {
	singleton = this;
}

Box3DPhysicsServer3D::~Box3DPhysicsServer3D() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

RID Box3DPhysicsServer3D::world_boundary_shape_create() {
	Box3DShape3D *shape = memnew(Box3DWorldBoundaryShape3D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box3DPhysicsServer3D::separation_ray_shape_create() {
	Box3DShape3D *shape = memnew(Box3DSeparationRayShape3D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box3DPhysicsServer3D::sphere_shape_create() {
	Box3DShape3D *shape = memnew(Box3DSphereShape3D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box3DPhysicsServer3D::box_shape_create() {
	Box3DShape3D *shape = memnew(Box3DBoxShape3D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box3DPhysicsServer3D::capsule_shape_create() {
	Box3DShape3D *shape = memnew(Box3DCapsuleShape3D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box3DPhysicsServer3D::cylinder_shape_create() {
	Box3DShape3D *shape = memnew(Box3DCylinderShape3D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box3DPhysicsServer3D::convex_polygon_shape_create() {
	Box3DShape3D *shape = memnew(Box3DConvexPolygonShape3D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box3DPhysicsServer3D::concave_polygon_shape_create() {
	Box3DShape3D *shape = memnew(Box3DConcavePolygonShape3D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box3DPhysicsServer3D::heightmap_shape_create() {
	Box3DShape3D *shape = memnew(Box3DHeightMapShape3D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box3DPhysicsServer3D::custom_shape_create() {
	ERR_FAIL_V_MSG(RID(), "Custom shapes are not supported.");
}

void Box3DPhysicsServer3D::shape_set_data(RID p_shape, const Variant &p_data) {
	Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);

	shape->set_data(p_data);
}

Variant Box3DPhysicsServer3D::shape_get_data(RID p_shape) const {
	const Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL_V(shape, Variant());

	return shape->get_data();
}

void Box3DPhysicsServer3D::shape_set_custom_solver_bias(RID p_shape, real_t p_bias) {
	Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);

	shape->set_solver_bias((float)p_bias);
}

PhysicsServer3D::ShapeType Box3DPhysicsServer3D::shape_get_type(RID p_shape) const {
	const Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL_V(shape, SHAPE_CUSTOM);

	return shape->get_type();
}

void Box3DPhysicsServer3D::shape_set_margin(RID p_shape, real_t p_margin) {
	Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);

	shape->set_margin((float)p_margin);
}

real_t Box3DPhysicsServer3D::shape_get_margin(RID p_shape) const {
	const Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL_V(shape, 0.0);

	return (real_t)shape->get_margin();
}

real_t Box3DPhysicsServer3D::shape_get_custom_solver_bias(RID p_shape) const {
	const Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL_V(shape, 0.0);

	return (real_t)shape->get_solver_bias();
}

RID Box3DPhysicsServer3D::space_create() {
	Box3DSpace3D *space = memnew(Box3DSpace3D);
	RID rid = space_owner.make_rid(space);
	space->set_rid(rid);

	const RID default_area_rid = area_create();
	Box3DArea3D *default_area = area_owner.get_or_null(default_area_rid);
	ERR_FAIL_NULL_V(default_area, RID());
	space->set_default_area(default_area);
	default_area->set_space(space);

	return rid;
}

void Box3DPhysicsServer3D::space_set_active(RID p_space, bool p_active) {
	Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL(space);

	if (p_active) {
		space->set_active(true);
		active_spaces.insert(space);
	} else {
		space->set_active(false);
		active_spaces.erase(space);
	}
}

bool Box3DPhysicsServer3D::space_is_active(RID p_space) const {
	Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, false);

	return active_spaces.has(space);
}

void Box3DPhysicsServer3D::space_set_param(RID p_space, SpaceParameter p_param, real_t p_value) {
	Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL(space);

	space->set_param(p_param, (double)p_value);
}

real_t Box3DPhysicsServer3D::space_get_param(RID p_space, SpaceParameter p_param) const {
	const Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, 0.0);

	return (real_t)space->get_param(p_param);
}

PhysicsDirectSpaceState3D *Box3DPhysicsServer3D::space_get_direct_state(RID p_space) {
	Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, nullptr);
	ERR_FAIL_COND_V_MSG((on_separate_thread && !doing_sync) || space->is_stepping(), nullptr, "Space state is inaccessible right now, wait for iteration or physics process notification.");

	return space->get_direct_state();
}

void Box3DPhysicsServer3D::space_set_debug_contacts(RID p_space, int p_max_contacts) {
	if (p_max_contacts > 0) {
		BOX3D_UNSUPPORTED_KEYED("Debug contact visualization",
				"Visible collision shapes/contacts debugging was enabled ('Visible Collision Shapes' or contact debugging).",
				"Box3D does not collect debug contacts for Godot to draw. Collision shapes themselves are still drawn by the debug shapes of the scene.",
				vformat("space=%s max_contacts=%d", p_space, p_max_contacts),
				"debug_contacts");
	}
}

PackedVector3Array Box3DPhysicsServer3D::space_get_contacts(RID p_space) const {
	return PackedVector3Array();
}

int Box3DPhysicsServer3D::space_get_contact_count(RID p_space) const {
	return 0;
}

RID Box3DPhysicsServer3D::area_create() {
	Box3DArea3D *area = memnew(Box3DArea3D);
	RID rid = area_owner.make_rid(area);
	area->set_rid(rid);
	return rid;
}

void Box3DPhysicsServer3D::area_set_space(RID p_area, RID p_space) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	Box3DSpace3D *space = nullptr;

	if (p_space.is_valid()) {
		space = space_owner.get_or_null(p_space);
		ERR_FAIL_NULL(space);
	}

	area->set_space(space);
}

void Box3DPhysicsServer3D::soft_body_apply_point_impulse(RID p_body, int p_point_index, const Vector3 &p_impulse) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->apply_vertex_impulse(p_point_index, p_impulse);
}

void Box3DPhysicsServer3D::soft_body_apply_point_force(RID p_body, int p_point_index, const Vector3 &p_force) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->apply_vertex_force(p_point_index, p_force);
}

void Box3DPhysicsServer3D::soft_body_apply_central_impulse(RID p_body, const Vector3 &p_impulse) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->apply_central_impulse(p_impulse);
}

void Box3DPhysicsServer3D::soft_body_apply_central_force(RID p_body, const Vector3 &p_force) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->apply_central_force(p_force);
}

RID Box3DPhysicsServer3D::area_get_space(RID p_area) const {
	const Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, RID());

	const Box3DSpace3D *space = area->get_space();

	if (space == nullptr) {
		return RID();
	}

	return space->get_rid();
}

void Box3DPhysicsServer3D::area_add_shape(RID p_area, RID p_shape, const Transform3D &p_transform, bool p_disabled) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);

	area->add_shape(shape, p_transform, p_disabled);
}

void Box3DPhysicsServer3D::area_set_shape(RID p_area, int p_shape_idx, RID p_shape) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);

	area->set_shape(p_shape_idx, shape);
}

RID Box3DPhysicsServer3D::area_get_shape(RID p_area, int p_shape_idx) const {
	const Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, RID());

	const Box3DShape3D *shape = area->get_shape(p_shape_idx);
	ERR_FAIL_NULL_V(shape, RID());

	return shape->get_rid();
}

void Box3DPhysicsServer3D::area_set_shape_transform(RID p_area, int p_shape_idx, const Transform3D &p_transform) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	area->set_shape_transform(p_shape_idx, p_transform);
}

Transform3D Box3DPhysicsServer3D::area_get_shape_transform(RID p_area, int p_shape_idx) const {
	const Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, Transform3D());

	return area->get_shape_transform_scaled(p_shape_idx);
}

int Box3DPhysicsServer3D::area_get_shape_count(RID p_area) const {
	const Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, 0);

	return area->get_shape_count();
}

void Box3DPhysicsServer3D::area_remove_shape(RID p_area, int p_shape_idx) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	area->remove_shape(p_shape_idx);
}

void Box3DPhysicsServer3D::area_clear_shapes(RID p_area) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	area->clear_shapes();
}

void Box3DPhysicsServer3D::area_set_shape_disabled(RID p_area, int p_shape_idx, bool p_disabled) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	area->set_shape_disabled(p_shape_idx, p_disabled);
}

void Box3DPhysicsServer3D::area_attach_object_instance_id(RID p_area, ObjectID p_id) {
	RID area_rid = p_area;

	if (space_owner.owns(area_rid)) {
		const Box3DSpace3D *space = space_owner.get_or_null(area_rid);
		area_rid = space->get_default_area()->get_rid();
	}

	Box3DArea3D *area = area_owner.get_or_null(area_rid);
	ERR_FAIL_NULL(area);

	area->set_instance_id(p_id);
}

ObjectID Box3DPhysicsServer3D::area_get_object_instance_id(RID p_area) const {
	RID area_rid = p_area;

	if (space_owner.owns(area_rid)) {
		const Box3DSpace3D *space = space_owner.get_or_null(area_rid);
		area_rid = space->get_default_area()->get_rid();
	}

	Box3DArea3D *area = area_owner.get_or_null(area_rid);
	ERR_FAIL_NULL_V(area, ObjectID());

	return area->get_instance_id();
}

void Box3DPhysicsServer3D::area_set_param(RID p_area, AreaParameter p_param, const Variant &p_value) {
	RID area_rid = p_area;

	if (space_owner.owns(area_rid)) {
		const Box3DSpace3D *space = space_owner.get_or_null(area_rid);
		area_rid = space->get_default_area()->get_rid();
	}

	Box3DArea3D *area = area_owner.get_or_null(area_rid);
	ERR_FAIL_NULL(area);

	area->set_param(p_param, p_value);
}

Variant Box3DPhysicsServer3D::area_get_param(RID p_area, AreaParameter p_param) const {
	RID area_rid = p_area;

	if (space_owner.owns(area_rid)) {
		const Box3DSpace3D *space = space_owner.get_or_null(area_rid);
		area_rid = space->get_default_area()->get_rid();
	}

	Box3DArea3D *area = area_owner.get_or_null(area_rid);
	ERR_FAIL_NULL_V(area, Variant());

	return area->get_param(p_param);
}

void Box3DPhysicsServer3D::area_set_transform(RID p_area, const Transform3D &p_transform) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	return area->set_transform(p_transform);
}

Transform3D Box3DPhysicsServer3D::area_get_transform(RID p_area) const {
	const Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, Transform3D());

	return area->get_transform_scaled();
}

void Box3DPhysicsServer3D::area_set_collision_mask(RID p_area, uint32_t p_mask) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	area->set_collision_mask(p_mask);
}

uint32_t Box3DPhysicsServer3D::area_get_collision_mask(RID p_area) const {
	const Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, 0);

	return area->get_collision_mask();
}

void Box3DPhysicsServer3D::area_set_collision_layer(RID p_area, uint32_t p_layer) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	area->set_collision_layer(p_layer);
}

uint32_t Box3DPhysicsServer3D::area_get_collision_layer(RID p_area) const {
	const Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, 0);

	return area->get_collision_layer();
}

void Box3DPhysicsServer3D::area_set_monitorable(RID p_area, bool p_monitorable) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	area->set_monitorable(p_monitorable);
}

void Box3DPhysicsServer3D::area_set_monitor_callback(RID p_area, const Callable &p_callback) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	area->set_body_monitor_callback(p_callback);
}

void Box3DPhysicsServer3D::area_set_area_monitor_callback(RID p_area, const Callable &p_callback) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	area->set_area_monitor_callback(p_callback);
}

void Box3DPhysicsServer3D::area_set_ray_pickable(RID p_area, bool p_enable) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	area->set_pickable(p_enable);
}

RID Box3DPhysicsServer3D::body_create() {
	Box3DBody3D *body = memnew(Box3DBody3D);
	RID rid = body_owner.make_rid(body);
	body->set_rid(rid);
	return rid;
}

void Box3DPhysicsServer3D::body_set_space(RID p_body, RID p_space) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	Box3DSpace3D *space = nullptr;

	if (p_space.is_valid()) {
		space = space_owner.get_or_null(p_space);
		ERR_FAIL_NULL(space);
	}

	body->set_space(space);
}

RID Box3DPhysicsServer3D::body_get_space(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, RID());

	const Box3DSpace3D *space = body->get_space();

	if (space == nullptr) {
		return RID();
	}

	return space->get_rid();
}

void Box3DPhysicsServer3D::body_set_mode(RID p_body, BodyMode p_mode) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_mode(p_mode);
}

PhysicsServer3D::BodyMode Box3DPhysicsServer3D::body_get_mode(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, BODY_MODE_STATIC);

	return body->get_mode();
}

void Box3DPhysicsServer3D::body_add_shape(RID p_body, RID p_shape, const Transform3D &p_transform, bool p_disabled) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);

	body->add_shape(shape, p_transform, p_disabled);
}

void Box3DPhysicsServer3D::body_set_shape(RID p_body, int p_shape_idx, RID p_shape) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);

	body->set_shape(p_shape_idx, shape);
}

RID Box3DPhysicsServer3D::body_get_shape(RID p_body, int p_shape_idx) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, RID());

	const Box3DShape3D *shape = body->get_shape(p_shape_idx);
	ERR_FAIL_NULL_V(shape, RID());

	return shape->get_rid();
}

void Box3DPhysicsServer3D::body_set_shape_transform(RID p_body, int p_shape_idx, const Transform3D &p_transform) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_shape_transform(p_shape_idx, p_transform);
}

Transform3D Box3DPhysicsServer3D::body_get_shape_transform(RID p_body, int p_shape_idx) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Transform3D());

	return body->get_shape_transform_scaled(p_shape_idx);
}

int Box3DPhysicsServer3D::body_get_shape_count(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);

	return body->get_shape_count();
}

void Box3DPhysicsServer3D::body_remove_shape(RID p_body, int p_shape_idx) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->remove_shape(p_shape_idx);
}

void Box3DPhysicsServer3D::body_clear_shapes(RID p_body) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->clear_shapes();
}

void Box3DPhysicsServer3D::body_set_shape_disabled(RID p_body, int p_shape_idx, bool p_disabled) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_shape_disabled(p_shape_idx, p_disabled);
}

void Box3DPhysicsServer3D::body_attach_object_instance_id(RID p_body, ObjectID p_id) {
	if (Box3DBody3D *body = body_owner.get_or_null(p_body)) {
		body->set_instance_id(p_id);
	} else if (Box3DSoftBody3D *soft_body = soft_body_owner.get_or_null(p_body)) {
		soft_body->set_instance_id(p_id);
	} else {
		ERR_FAIL();
	}
}

ObjectID Box3DPhysicsServer3D::body_get_object_instance_id(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, ObjectID());

	return body->get_instance_id();
}

void Box3DPhysicsServer3D::body_set_enable_continuous_collision_detection(RID p_body, bool p_enable) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_ccd_enabled(p_enable);
}

bool Box3DPhysicsServer3D::body_is_continuous_collision_detection_enabled(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, false);

	return body->is_ccd_enabled();
}

void Box3DPhysicsServer3D::body_set_collision_layer(RID p_body, uint32_t p_layer) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_collision_layer(p_layer);
}

uint32_t Box3DPhysicsServer3D::body_get_collision_layer(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);

	return body->get_collision_layer();
}

void Box3DPhysicsServer3D::body_set_collision_mask(RID p_body, uint32_t p_mask) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_collision_mask(p_mask);
}

uint32_t Box3DPhysicsServer3D::body_get_collision_mask(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);

	return body->get_collision_mask();
}

void Box3DPhysicsServer3D::body_set_collision_priority(RID p_body, real_t p_priority) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_collision_priority((float)p_priority);
}

real_t Box3DPhysicsServer3D::body_get_collision_priority(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0.0);

	return (real_t)body->get_collision_priority();
}

void Box3DPhysicsServer3D::body_set_user_flags(RID p_body, uint32_t p_flags) {
	WARN_PRINT("Body user flags are not supported. Any such value will be ignored.");
}

uint32_t Box3DPhysicsServer3D::body_get_user_flags(RID p_body) const {
	return 0;
}

void Box3DPhysicsServer3D::body_set_param(RID p_body, BodyParameter p_param, const Variant &p_value) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_param(p_param, p_value);
}

Variant Box3DPhysicsServer3D::body_get_param(RID p_body, BodyParameter p_param) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Variant());

	return body->get_param(p_param);
}

void Box3DPhysicsServer3D::body_reset_mass_properties(RID p_body) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->reset_mass_properties();
}

void Box3DPhysicsServer3D::body_set_state(RID p_body, BodyState p_state, const Variant &p_value) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_state(p_state, p_value);
}

Variant Box3DPhysicsServer3D::body_get_state(RID p_body, BodyState p_state) const {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Variant());

	return body->get_state(p_state);
}

void Box3DPhysicsServer3D::body_apply_central_impulse(RID p_body, const Vector3 &p_impulse) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	return body->apply_central_impulse(p_impulse);
}

void Box3DPhysicsServer3D::body_apply_impulse(RID p_body, const Vector3 &p_impulse, const Vector3 &p_position) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	return body->apply_impulse(p_impulse, p_position);
}

void Box3DPhysicsServer3D::body_apply_torque_impulse(RID p_body, const Vector3 &p_impulse) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	return body->apply_torque_impulse(p_impulse);
}

void Box3DPhysicsServer3D::body_apply_central_force(RID p_body, const Vector3 &p_force) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	return body->apply_central_force(p_force);
}

void Box3DPhysicsServer3D::body_apply_force(RID p_body, const Vector3 &p_force, const Vector3 &p_position) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	return body->apply_force(p_force, p_position);
}

void Box3DPhysicsServer3D::body_apply_torque(RID p_body, const Vector3 &p_torque) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	return body->apply_torque(p_torque);
}

void Box3DPhysicsServer3D::body_add_constant_central_force(RID p_body, const Vector3 &p_force) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->add_constant_central_force(p_force);
}

void Box3DPhysicsServer3D::body_add_constant_force(RID p_body, const Vector3 &p_force, const Vector3 &p_position) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->add_constant_force(p_force, p_position);
}

void Box3DPhysicsServer3D::body_add_constant_torque(RID p_body, const Vector3 &p_torque) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->add_constant_torque(p_torque);
}

void Box3DPhysicsServer3D::body_set_constant_force(RID p_body, const Vector3 &p_force) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_constant_force(p_force);
}

Vector3 Box3DPhysicsServer3D::body_get_constant_force(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Vector3());

	return body->get_constant_force();
}

void Box3DPhysicsServer3D::body_set_constant_torque(RID p_body, const Vector3 &p_torque) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_constant_torque(p_torque);
}

Vector3 Box3DPhysicsServer3D::body_get_constant_torque(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Vector3());

	return body->get_constant_torque();
}

void Box3DPhysicsServer3D::body_set_axis_velocity(RID p_body, const Vector3 &p_axis_velocity) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_axis_velocity(p_axis_velocity);
}

void Box3DPhysicsServer3D::body_set_axis_lock(RID p_body, BodyAxis p_axis, bool p_lock) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_axis_lock(p_axis, p_lock);
}

bool Box3DPhysicsServer3D::body_is_axis_locked(RID p_body, BodyAxis p_axis) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, false);

	return body->is_axis_locked(p_axis);
}

void Box3DPhysicsServer3D::body_add_collision_exception(RID p_body, RID p_excepted_body) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->add_collision_exception(p_excepted_body);
}

void Box3DPhysicsServer3D::body_remove_collision_exception(RID p_body, RID p_excepted_body) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->remove_collision_exception(p_excepted_body);
}

void Box3DPhysicsServer3D::body_get_collision_exceptions(RID p_body, List<RID> *p_exceptions) {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	for (const RID &exception : body->get_collision_exceptions()) {
		p_exceptions->push_back(exception);
	}
}

void Box3DPhysicsServer3D::body_set_max_contacts_reported(RID p_body, int p_amount) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	return body->set_max_contacts_reported(p_amount);
}

int Box3DPhysicsServer3D::body_get_max_contacts_reported(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);

	return body->get_max_contacts_reported();
}

void Box3DPhysicsServer3D::body_set_contacts_reported_depth_threshold(RID p_body, real_t p_threshold) {
	WARN_PRINT("Per-body contact depth threshold is not supported. Any such value will be ignored.");
}

real_t Box3DPhysicsServer3D::body_get_contacts_reported_depth_threshold(RID p_body) const {
	return 0.0;
}

void Box3DPhysicsServer3D::body_set_omit_force_integration(RID p_body, bool p_enable) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_custom_integrator(p_enable);
}

bool Box3DPhysicsServer3D::body_is_omitting_force_integration(RID p_body) const {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, false);

	return body->has_custom_integrator();
}

void Box3DPhysicsServer3D::body_set_state_sync_callback(RID p_body, const Callable &p_callable) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_state_sync_callback(p_callable);
}

void Box3DPhysicsServer3D::body_set_force_integration_callback(RID p_body, const Callable &p_callable, const Variant &p_userdata) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_custom_integration_callback(p_callable, p_userdata);
}

void Box3DPhysicsServer3D::body_set_ray_pickable(RID p_body, bool p_enable) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_pickable(p_enable);
}

bool Box3DPhysicsServer3D::body_test_motion(RID p_body, const MotionParameters &p_parameters, MotionResult *r_result) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, false);

	Box3DSpace3D *space = body->get_space();
	ERR_FAIL_NULL_V(space, false);

	return space->get_direct_state()->body_test_motion(*body, p_parameters, r_result);
}

PhysicsDirectBodyState3D *Box3DPhysicsServer3D::body_get_direct_state(RID p_body) {
	ERR_FAIL_COND_V_MSG((on_separate_thread && !doing_sync), nullptr, "Body state is inaccessible right now, wait for iteration or physics process notification.");

	Box3DBody3D *body = body_owner.get_or_null(p_body);
	if (unlikely(body == nullptr || body->get_space() == nullptr)) {
		return nullptr;
	}

	ERR_FAIL_COND_V_MSG(body->get_space()->is_stepping(), nullptr, "Body state is inaccessible right now, wait for iteration or physics process notification.");

	return body->get_direct_state();
}

RID Box3DPhysicsServer3D::soft_body_create() {
	Box3DSoftBody3D *body = memnew(Box3DSoftBody3D);
	RID rid = soft_body_owner.make_rid(body);
	body->set_rid(rid);
	return rid;
}

void Box3DPhysicsServer3D::soft_body_update_rendering_server(RID p_body, RequiredParam<PhysicsServer3DRenderingServerHandler> rp_rendering_server_handler) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	EXTRACT_PARAM_OR_FAIL(p_rendering_server_handler, rp_rendering_server_handler);

	return body->update_rendering_server(p_rendering_server_handler);
}

void Box3DPhysicsServer3D::soft_body_set_space(RID p_body, RID p_space) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	Box3DSpace3D *space = nullptr;

	if (p_space.is_valid()) {
		space = space_owner.get_or_null(p_space);
		ERR_FAIL_NULL(space);
	}

	body->set_space(space);
}

RID Box3DPhysicsServer3D::soft_body_get_space(RID p_body) const {
	const Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, RID());

	const Box3DSpace3D *space = body->get_space();

	if (space == nullptr) {
		return RID();
	}

	return space->get_rid();
}

void Box3DPhysicsServer3D::soft_body_set_mesh(RID p_body, RID p_mesh) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_mesh(p_mesh);
}

AABB Box3DPhysicsServer3D::soft_body_get_bounds(RID p_body) const {
	const Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, AABB());

	return body->get_bounds();
}

void Box3DPhysicsServer3D::soft_body_set_collision_layer(RID p_body, uint32_t p_layer) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_collision_layer(p_layer);
}

uint32_t Box3DPhysicsServer3D::soft_body_get_collision_layer(RID p_body) const {
	const Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);

	return body->get_collision_layer();
}

void Box3DPhysicsServer3D::soft_body_set_collision_mask(RID p_body, uint32_t p_mask) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_collision_mask(p_mask);
}

uint32_t Box3DPhysicsServer3D::soft_body_get_collision_mask(RID p_body) const {
	const Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);

	return body->get_collision_mask();
}

void Box3DPhysicsServer3D::soft_body_add_collision_exception(RID p_body, RID p_excepted_body) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->add_collision_exception(p_excepted_body);
}

void Box3DPhysicsServer3D::soft_body_remove_collision_exception(RID p_body, RID p_excepted_body) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->remove_collision_exception(p_excepted_body);
}

void Box3DPhysicsServer3D::soft_body_get_collision_exceptions(RID p_body, List<RID> *p_exceptions) {
	const Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	for (const RID &exception : body->get_collision_exceptions()) {
		p_exceptions->push_back(exception);
	}
}

void Box3DPhysicsServer3D::soft_body_set_state(RID p_body, BodyState p_state, const Variant &p_value) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_state(p_state, p_value);
}

Variant Box3DPhysicsServer3D::soft_body_get_state(RID p_body, BodyState p_state) const {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Variant());

	return body->get_state(p_state);
}

void Box3DPhysicsServer3D::soft_body_set_transform(RID p_body, const Transform3D &p_transform) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	return body->set_transform(p_transform);
}

void Box3DPhysicsServer3D::soft_body_set_ray_pickable(RID p_body, bool p_enable) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	return body->set_pickable(p_enable);
}

void Box3DPhysicsServer3D::soft_body_set_simulation_precision(RID p_body, int p_precision) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	return body->set_simulation_precision(p_precision);
}

int Box3DPhysicsServer3D::soft_body_get_simulation_precision(RID p_body) const {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);

	return body->get_simulation_precision();
}

void Box3DPhysicsServer3D::soft_body_set_total_mass(RID p_body, real_t p_total_mass) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	return body->set_mass((float)p_total_mass);
}

real_t Box3DPhysicsServer3D::soft_body_get_total_mass(RID p_body) const {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0.0);

	return (real_t)body->get_mass();
}

void Box3DPhysicsServer3D::soft_body_set_linear_stiffness(RID p_body, real_t p_coefficient) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	return body->set_stiffness_coefficient((float)p_coefficient);
}

real_t Box3DPhysicsServer3D::soft_body_get_linear_stiffness(RID p_body) const {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0.0);

	return (real_t)body->get_stiffness_coefficient();
}

void Box3DPhysicsServer3D::soft_body_set_shrinking_factor(RID p_body, real_t p_shrinking_factor) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	return body->set_shrinking_factor((float)p_shrinking_factor);
}

real_t Box3DPhysicsServer3D::soft_body_get_shrinking_factor(RID p_body) const {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0.0);

	return (real_t)body->get_shrinking_factor();
}

void Box3DPhysicsServer3D::soft_body_set_pressure_coefficient(RID p_body, real_t p_coefficient) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	return body->set_pressure((float)p_coefficient);
}

real_t Box3DPhysicsServer3D::soft_body_get_pressure_coefficient(RID p_body) const {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0.0);

	return (real_t)body->get_pressure();
}

void Box3DPhysicsServer3D::soft_body_set_damping_coefficient(RID p_body, real_t p_coefficient) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	return body->set_linear_damping((float)p_coefficient);
}

real_t Box3DPhysicsServer3D::soft_body_get_damping_coefficient(RID p_body) const {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0.0);

	return (real_t)body->get_linear_damping();
}

void Box3DPhysicsServer3D::soft_body_set_drag_coefficient(RID p_body, real_t p_coefficient) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	return body->set_drag((float)p_coefficient);
}

real_t Box3DPhysicsServer3D::soft_body_get_drag_coefficient(RID p_body) const {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0.0);

	return (real_t)body->get_drag();
}

void Box3DPhysicsServer3D::soft_body_move_point(RID p_body, int p_point_index, const Vector3 &p_global_position) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_vertex_position(p_point_index, p_global_position);
}

Vector3 Box3DPhysicsServer3D::soft_body_get_point_global_position(RID p_body, int p_point_index) const {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Vector3());

	return body->get_vertex_position(p_point_index);
}

void Box3DPhysicsServer3D::soft_body_remove_all_pinned_points(RID p_body) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->unpin_all_vertices();
}

void Box3DPhysicsServer3D::soft_body_pin_point(RID p_body, int p_point_index, bool p_pin) {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	if (p_pin) {
		body->pin_vertex(p_point_index);
	} else {
		body->unpin_vertex(p_point_index);
	}
}

bool Box3DPhysicsServer3D::soft_body_is_point_pinned(RID p_body, int p_point_index) const {
	Box3DSoftBody3D *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, false);

	return body->is_vertex_pinned(p_point_index);
}

RID Box3DPhysicsServer3D::joint_create() {
	Box3DJoint3D *joint = memnew(Box3DJoint3D);
	RID rid = joint_owner.make_rid(joint);
	joint->set_rid(rid);
	return rid;
}

void Box3DPhysicsServer3D::joint_clear(RID p_joint) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);

	if (joint->get_type() != JOINT_TYPE_MAX) {
		Box3DJoint3D *empty_joint = memnew(Box3DJoint3D);
		empty_joint->set_rid(joint->get_rid());

		memdelete(joint);
		joint = nullptr;

		joint_owner.replace(p_joint, empty_joint);
	}
}

void Box3DPhysicsServer3D::joint_make_pin(RID p_joint, RID p_body_a, const Vector3 &p_local_a, RID p_body_b, const Vector3 &p_local_b) {
	Box3DJoint3D *old_joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(old_joint);

	Box3DBody3D *body_a = body_owner.get_or_null(p_body_a);
	ERR_FAIL_NULL(body_a);

	Box3DBody3D *body_b = body_owner.get_or_null(p_body_b);
	ERR_FAIL_COND(body_a == body_b);

	Box3DJoint3D *new_joint = memnew(Box3DPinJoint3D(*old_joint, body_a, body_b, p_local_a, p_local_b));

	memdelete(old_joint);
	old_joint = nullptr;

	joint_owner.replace(p_joint, new_joint);
}

void Box3DPhysicsServer3D::pin_joint_set_param(RID p_joint, PinJointParam p_param, real_t p_value) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);

	ERR_FAIL_COND(joint->get_type() != JOINT_TYPE_PIN);
	Box3DPinJoint3D *pin_joint = static_cast<Box3DPinJoint3D *>(joint);

	pin_joint->set_param(p_param, (double)p_value);
}

real_t Box3DPhysicsServer3D::pin_joint_get_param(RID p_joint, PinJointParam p_param) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, 0.0);

	ERR_FAIL_COND_V(joint->get_type() != JOINT_TYPE_PIN, 0.0);
	const Box3DPinJoint3D *pin_joint = static_cast<const Box3DPinJoint3D *>(joint);

	return (real_t)pin_joint->get_param(p_param);
}

void Box3DPhysicsServer3D::pin_joint_set_local_a(RID p_joint, const Vector3 &p_local_a) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);

	ERR_FAIL_COND(joint->get_type() != JOINT_TYPE_PIN);
	Box3DPinJoint3D *pin_joint = static_cast<Box3DPinJoint3D *>(joint);

	pin_joint->set_local_a(p_local_a);
}

Vector3 Box3DPhysicsServer3D::pin_joint_get_local_a(RID p_joint) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, Vector3());

	ERR_FAIL_COND_V(joint->get_type() != JOINT_TYPE_PIN, Vector3());
	const Box3DPinJoint3D *pin_joint = static_cast<const Box3DPinJoint3D *>(joint);

	return pin_joint->get_local_a();
}

void Box3DPhysicsServer3D::pin_joint_set_local_b(RID p_joint, const Vector3 &p_local_b) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);

	ERR_FAIL_COND(joint->get_type() != JOINT_TYPE_PIN);
	Box3DPinJoint3D *pin_joint = static_cast<Box3DPinJoint3D *>(joint);

	pin_joint->set_local_b(p_local_b);
}

Vector3 Box3DPhysicsServer3D::pin_joint_get_local_b(RID p_joint) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, Vector3());

	ERR_FAIL_COND_V(joint->get_type() != JOINT_TYPE_PIN, Vector3());
	const Box3DPinJoint3D *pin_joint = static_cast<const Box3DPinJoint3D *>(joint);

	return pin_joint->get_local_b();
}

void Box3DPhysicsServer3D::joint_make_hinge(RID p_joint, RID p_body_a, const Transform3D &p_hinge_a, RID p_body_b, const Transform3D &p_hinge_b) {
	Box3DJoint3D *old_joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(old_joint);

	Box3DBody3D *body_a = body_owner.get_or_null(p_body_a);
	ERR_FAIL_NULL(body_a);

	Box3DBody3D *body_b = body_owner.get_or_null(p_body_b);
	ERR_FAIL_COND(body_a == body_b);

	Box3DJoint3D *new_joint = memnew(Box3DHingeJoint3D(*old_joint, body_a, body_b, p_hinge_a, p_hinge_b));

	memdelete(old_joint);
	old_joint = nullptr;

	joint_owner.replace(p_joint, new_joint);
}

void Box3DPhysicsServer3D::joint_make_hinge_simple(RID p_joint, RID p_body_a, const Vector3 &p_pivot_a, const Vector3 &p_axis_a, RID p_body_b, const Vector3 &p_pivot_b, const Vector3 &p_axis_b) {
	// A hinge is described by a pivot and an axis in each body, so build frames whose z-axis is that axis.
	const auto make_frame = [](const Vector3 &p_pivot, const Vector3 &p_axis) {
		const Vector3 z = p_axis.normalized();
		const Vector3 helper = Math::abs(z.x) < 0.9 ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
		const Vector3 x = helper.cross(z).normalized();
		const Vector3 y = z.cross(x);
		return Transform3D(Basis(x, y, z), p_pivot);
	};

	joint_make_hinge(p_joint, p_body_a, make_frame(p_pivot_a, p_axis_a), p_body_b, make_frame(p_pivot_b, p_axis_b));
}

void Box3DPhysicsServer3D::hinge_joint_set_param(RID p_joint, HingeJointParam p_param, real_t p_value) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);

	ERR_FAIL_COND(joint->get_type() != JOINT_TYPE_HINGE);
	Box3DHingeJoint3D *hinge_joint = static_cast<Box3DHingeJoint3D *>(joint);

	return hinge_joint->set_param(p_param, (double)p_value);
}

real_t Box3DPhysicsServer3D::hinge_joint_get_param(RID p_joint, HingeJointParam p_param) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, 0.0);

	ERR_FAIL_COND_V(joint->get_type() != JOINT_TYPE_HINGE, 0.0);
	const Box3DHingeJoint3D *hinge_joint = static_cast<const Box3DHingeJoint3D *>(joint);

	return (real_t)hinge_joint->get_param(p_param);
}

void Box3DPhysicsServer3D::hinge_joint_set_flag(RID p_joint, HingeJointFlag p_flag, bool p_enabled) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);

	ERR_FAIL_COND(joint->get_type() != JOINT_TYPE_HINGE);
	Box3DHingeJoint3D *hinge_joint = static_cast<Box3DHingeJoint3D *>(joint);

	return hinge_joint->set_flag(p_flag, p_enabled);
}

bool Box3DPhysicsServer3D::hinge_joint_get_flag(RID p_joint, HingeJointFlag p_flag) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, false);

	ERR_FAIL_COND_V(joint->get_type() != JOINT_TYPE_HINGE, false);
	const Box3DHingeJoint3D *hinge_joint = static_cast<const Box3DHingeJoint3D *>(joint);

	return hinge_joint->get_flag(p_flag);
}

void Box3DPhysicsServer3D::joint_make_slider(RID p_joint, RID p_body_a, const Transform3D &p_local_ref_a, RID p_body_b, const Transform3D &p_local_ref_b) {
	Box3DJoint3D *old_joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(old_joint);

	Box3DBody3D *body_a = body_owner.get_or_null(p_body_a);
	ERR_FAIL_NULL(body_a);

	Box3DBody3D *body_b = body_owner.get_or_null(p_body_b);
	ERR_FAIL_COND(body_a == body_b);

	Box3DJoint3D *new_joint = memnew(Box3DSliderJoint3D(*old_joint, body_a, body_b, p_local_ref_a, p_local_ref_b));

	memdelete(old_joint);
	old_joint = nullptr;

	joint_owner.replace(p_joint, new_joint);
}

void Box3DPhysicsServer3D::slider_joint_set_param(RID p_joint, SliderJointParam p_param, real_t p_value) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);

	ERR_FAIL_COND(joint->get_type() != JOINT_TYPE_SLIDER);
	Box3DSliderJoint3D *slider_joint = static_cast<Box3DSliderJoint3D *>(joint);

	return slider_joint->set_param(p_param, (real_t)p_value);
}

real_t Box3DPhysicsServer3D::slider_joint_get_param(RID p_joint, SliderJointParam p_param) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, 0.0);

	ERR_FAIL_COND_V(joint->get_type() != JOINT_TYPE_SLIDER, 0.0);
	const Box3DSliderJoint3D *slider_joint = static_cast<const Box3DSliderJoint3D *>(joint);

	return slider_joint->get_param(p_param);
}

void Box3DPhysicsServer3D::joint_make_cone_twist(RID p_joint, RID p_body_a, const Transform3D &p_local_ref_a, RID p_body_b, const Transform3D &p_local_ref_b) {
	Box3DJoint3D *old_joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(old_joint);

	Box3DBody3D *body_a = body_owner.get_or_null(p_body_a);
	ERR_FAIL_NULL(body_a);

	Box3DBody3D *body_b = body_owner.get_or_null(p_body_b);
	ERR_FAIL_COND(body_a == body_b);

	Box3DJoint3D *new_joint = memnew(Box3DConeTwistJoint3D(*old_joint, body_a, body_b, p_local_ref_a, p_local_ref_b));

	memdelete(old_joint);
	old_joint = nullptr;

	joint_owner.replace(p_joint, new_joint);
}

void Box3DPhysicsServer3D::cone_twist_joint_set_param(RID p_joint, ConeTwistJointParam p_param, real_t p_value) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);

	ERR_FAIL_COND(joint->get_type() != JOINT_TYPE_CONE_TWIST);
	Box3DConeTwistJoint3D *cone_twist_joint = static_cast<Box3DConeTwistJoint3D *>(joint);

	return cone_twist_joint->set_param(p_param, (double)p_value);
}

real_t Box3DPhysicsServer3D::cone_twist_joint_get_param(RID p_joint, ConeTwistJointParam p_param) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, 0.0);

	ERR_FAIL_COND_V(joint->get_type() != JOINT_TYPE_CONE_TWIST, 0.0);
	const Box3DConeTwistJoint3D *cone_twist_joint = static_cast<const Box3DConeTwistJoint3D *>(joint);

	return (real_t)cone_twist_joint->get_param(p_param);
}

void Box3DPhysicsServer3D::joint_make_generic_6dof(RID p_joint, RID p_body_a, const Transform3D &p_local_ref_a, RID p_body_b, const Transform3D &p_local_ref_b) {
	Box3DJoint3D *old_joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(old_joint);

	Box3DBody3D *body_a = body_owner.get_or_null(p_body_a);
	ERR_FAIL_NULL(body_a);

	Box3DBody3D *body_b = body_owner.get_or_null(p_body_b);
	ERR_FAIL_COND(body_a == body_b);

	Box3DJoint3D *new_joint = memnew(Box3DGeneric6DOFJoint3D(*old_joint, body_a, body_b, p_local_ref_a, p_local_ref_b));

	memdelete(old_joint);
	old_joint = nullptr;

	joint_owner.replace(p_joint, new_joint);
}

void Box3DPhysicsServer3D::generic_6dof_joint_set_param(RID p_joint, Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisParam p_param, real_t p_value) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);

	ERR_FAIL_COND(joint->get_type() != JOINT_TYPE_6DOF);
	Box3DGeneric6DOFJoint3D *g6dof_joint = static_cast<Box3DGeneric6DOFJoint3D *>(joint);

	return g6dof_joint->set_param(p_axis, p_param, (double)p_value);
}

real_t Box3DPhysicsServer3D::generic_6dof_joint_get_param(RID p_joint, Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisParam p_param) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, 0.0);

	ERR_FAIL_COND_V(joint->get_type() != JOINT_TYPE_6DOF, 0.0);
	const Box3DGeneric6DOFJoint3D *g6dof_joint = static_cast<const Box3DGeneric6DOFJoint3D *>(joint);

	return (real_t)g6dof_joint->get_param(p_axis, p_param);
}

void Box3DPhysicsServer3D::generic_6dof_joint_set_flag(RID p_joint, Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisFlag p_flag, bool p_enable) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);

	ERR_FAIL_COND(joint->get_type() != JOINT_TYPE_6DOF);
	Box3DGeneric6DOFJoint3D *g6dof_joint = static_cast<Box3DGeneric6DOFJoint3D *>(joint);

	return g6dof_joint->set_flag(p_axis, p_flag, p_enable);
}

bool Box3DPhysicsServer3D::generic_6dof_joint_get_flag(RID p_joint, Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisFlag p_flag) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, false);

	ERR_FAIL_COND_V(joint->get_type() != JOINT_TYPE_6DOF, false);
	const Box3DGeneric6DOFJoint3D *g6dof_joint = static_cast<const Box3DGeneric6DOFJoint3D *>(joint);

	return g6dof_joint->get_flag(p_axis, p_flag);
}

PhysicsServer3D::JointType Box3DPhysicsServer3D::joint_get_type(RID p_joint) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, JOINT_TYPE_PIN);

	return joint->get_type();
}

void Box3DPhysicsServer3D::joint_set_solver_priority(RID p_joint, int p_priority) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);

	joint->set_solver_priority(p_priority);
}

int Box3DPhysicsServer3D::joint_get_solver_priority(RID p_joint) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, 0);

	return joint->get_solver_priority();
}

void Box3DPhysicsServer3D::joint_disable_collisions_between_bodies(RID p_joint, bool p_disable) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);

	joint->set_collision_disabled(p_disable);
}

bool Box3DPhysicsServer3D::joint_is_disabled_collisions_between_bodies(RID p_joint) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, false);

	return joint->is_collision_disabled();
}

void Box3DPhysicsServer3D::free_rid(RID p_rid) {
	if (Box3DShape3D *shape = shape_owner.get_or_null(p_rid)) {
		free_shape(shape);
	} else if (Box3DBody3D *body = body_owner.get_or_null(p_rid)) {
		free_body(body);
	} else if (Box3DJoint3D *joint = joint_owner.get_or_null(p_rid)) {
		free_joint(joint);
	} else if (Box3DArea3D *area = area_owner.get_or_null(p_rid)) {
		free_area(area);
	} else if (Box3DSoftBody3D *soft_body = soft_body_owner.get_or_null(p_rid)) {
		free_soft_body(soft_body);
	} else if (Box3DSpace3D *space = space_owner.get_or_null(p_rid)) {
		free_space(space);
	} else {
		ERR_FAIL_MSG("Failed to free RID: The specified RID has no owner.");
	}
}

void Box3DPhysicsServer3D::set_active(bool p_active) {
	active = p_active;
}

void Box3DPhysicsServer3D::init() {
	const b3Version version = b3GetVersion();
	print_verbose(vformat("Box3D Physics %d.%d.%d initialized (%s precision, %d simulation sub-steps).", version.major, version.minor, version.revision, b3IsDoublePrecision() ? "double" : "single", Box3DProjectSettings::simulation_sub_steps));
}

void Box3DPhysicsServer3D::finish() {
}

void Box3DPhysicsServer3D::step(real_t p_step) {
	if (!active) {
		return;
	}

	for (Box3DSpace3D *active_space : active_spaces) {
		active_space->step((float)p_step);
	}
}

void Box3DPhysicsServer3D::sync() {
	doing_sync = true;
}

void Box3DPhysicsServer3D::end_sync() {
	doing_sync = false;
}

void Box3DPhysicsServer3D::flush_queries() {
	if (!active) {
		return;
	}

	flushing_queries = true;

	for (Box3DSpace3D *space : active_spaces) {
		space->call_queries();
	}

	flushing_queries = false;
}

bool Box3DPhysicsServer3D::is_flushing_queries() const {
	return flushing_queries;
}

int Box3DPhysicsServer3D::get_process_info(ProcessInfo p_process_info) {
	int count = 0;

	switch (p_process_info) {
		case INFO_ACTIVE_OBJECTS: {
			for (const Box3DSpace3D *space : active_spaces) {
				count += space->get_awake_body_count();
			}
		} break;
		case INFO_COLLISION_PAIRS: {
			for (const Box3DSpace3D *space : active_spaces) {
				count += b3World_GetCounters(space->get_world()).contactCount;
			}
		} break;
		case INFO_ISLAND_COUNT: {
			for (const Box3DSpace3D *space : active_spaces) {
				count += b3World_GetCounters(space->get_world()).islandCount;
			}
		} break;
	}

	return count;
}

void Box3DPhysicsServer3D::free_space(Box3DSpace3D *p_space) {
	ERR_FAIL_NULL(p_space);

	free_area(p_space->get_default_area());
	space_set_active(p_space->get_rid(), false);
	space_owner.free(p_space->get_rid());
	memdelete(p_space);
}

void Box3DPhysicsServer3D::free_area(Box3DArea3D *p_area) {
	ERR_FAIL_NULL(p_area);

	p_area->set_space(nullptr);
	area_owner.free(p_area->get_rid());
	memdelete(p_area);
}

void Box3DPhysicsServer3D::free_body(Box3DBody3D *p_body) {
	ERR_FAIL_NULL(p_body);

	p_body->set_space(nullptr);
	body_owner.free(p_body->get_rid());
	memdelete(p_body);
}

void Box3DPhysicsServer3D::free_soft_body(Box3DSoftBody3D *p_body) {
	ERR_FAIL_NULL(p_body);

	p_body->set_space(nullptr);
	soft_body_owner.free(p_body->get_rid());
	memdelete(p_body);
}

void Box3DPhysicsServer3D::free_shape(Box3DShape3D *p_shape) {
	ERR_FAIL_NULL(p_shape);

	p_shape->remove_self();
	shape_owner.free(p_shape->get_rid());
	memdelete(p_shape);
}

void Box3DPhysicsServer3D::free_joint(Box3DJoint3D *p_joint) {
	ERR_FAIL_NULL(p_joint);

	joint_owner.free(p_joint->get_rid());
	memdelete(p_joint);
}
