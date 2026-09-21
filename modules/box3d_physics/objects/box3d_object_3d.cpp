/**************************************************************************/
/*  box3d_object_3d.cpp                                                   */
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

#include "box3d_object_3d.h"

#include "../box3d_physics_server_3d.h"
#include "../spaces/box3d_space_3d.h"
#include "box3d_area_3d.h"
#include "box3d_body_3d.h"

Box3DObject3D::Box3DObject3D(ObjectType p_object_type) :
		shapes_changed_element(this), object_type(p_object_type) {
}

Box3DObject3D::~Box3DObject3D() {
	for (Box3DShapeInstance *instance : shapes) {
		memdelete(instance);
	}

	shapes.clear();
}

Object *Box3DObject3D::get_instance() const {
	return ObjectDB::get_instance(instance_id);
}

void Box3DObject3D::_add_to_space() {
	ERR_FAIL_NULL(space);

	b3BodyDef def = b3DefaultBodyDef();
	def.type = _get_body_type();
	def.position = to_b3_pos(transform_unscaled.origin);
	def.rotation = to_b3(transform_unscaled.basis);
	def.userData = this;
	_fill_body_def(def);

	body_id = b3CreateBody(space->get_world(), &def);
	ERR_FAIL_COND(B3_IS_NULL(body_id));

	_build_shapes();
}

void Box3DObject3D::_remove_from_space() {
	if (!in_space()) {
		return;
	}

	// Capture the transform so the object keeps its pose if it is put into another space.
	transform_unscaled = world_to_godot(b3Body_GetTransform(body_id));

	for (Box3DShapeInstance *instance : shapes) {
		instance->forget_built();
	}

	b3DestroyBody(body_id);
	body_id = b3_nullBodyId;

	space->dequeue_shapes_changed(&shapes_changed_element);
}

void Box3DObject3D::_build_shapes() {
	if (!in_space()) {
		return;
	}

	const b3ShapeDef def = _make_shape_def();
	const bool is_static_body = b3Body_GetType(body_id) == b3_staticBody;

	for (Box3DShapeInstance *instance : shapes) {
		instance->build(body_id, def, scale, is_static_body);
	}

	// Box3D requires the mass to be brought up to date regardless of the type of the body, since the shapes above
	// were created without updating it.
	b3Body_ApplyMassFromShapes(body_id);

	_shapes_committed();
}

void Box3DObject3D::_update_shape_filters() {
	if (!in_space()) {
		return;
	}

	const b3Filter filter = Box3DFilter::make(collision_layer);

	for (Box3DShapeInstance *instance : shapes) {
		for (const b3ShapeId &id : instance->built_shapes) {
			b3Shape_SetFilter(id, filter, true);
		}
	}
}

void Box3DObject3D::_collision_layer_changed() {
	_update_shape_filters();
}

void Box3DObject3D::set_space(Box3DSpace3D *p_space) {
	if (space == p_space) {
		return;
	}

	_space_changing();

	if (space != nullptr) {
		_remove_from_space();
	}

	space = p_space;

	if (space != nullptr) {
		_add_to_space();
	}

	_space_changed();
}

void Box3DObject3D::set_collision_layer(uint32_t p_layer) {
	if (p_layer == collision_layer) {
		return;
	}

	collision_layer = p_layer;

	_collision_layer_changed();
}

void Box3DObject3D::set_collision_mask(uint32_t p_mask) {
	if (p_mask == collision_mask) {
		return;
	}

	collision_mask = p_mask;

	_collision_mask_changed();
}

bool Box3DObject3D::can_interact_with(const Box3DObject3D &p_other) const {
	if (const Box3DBody3D *other_body = p_other.as_body()) {
		return can_interact_with(*other_body);
	} else if (const Box3DArea3D *other_area = p_other.as_area()) {
		return can_interact_with(*other_area);
	} else {
		ERR_FAIL_V_MSG(false, vformat("Unhandled object type: '%d'. This should not happen. Please report this.", p_other.get_type()));
	}
}

Transform3D Box3DObject3D::get_transform_unscaled() const {
	if (!in_space()) {
		return transform_unscaled;
	}

	return world_to_godot(b3Body_GetTransform(body_id));
}

Transform3D Box3DObject3D::get_transform_scaled() const {
	Transform3D result = get_transform_unscaled();
	result.basis.scale_local(scale);
	return result;
}

Vector3 Box3DObject3D::get_position() const {
	if (!in_space()) {
		return transform_unscaled.origin;
	}

	return pos_to_godot(b3Body_GetPosition(body_id));
}

Vector3 Box3DObject3D::get_center_of_mass() const {
	if (!in_space()) {
		return transform_unscaled.origin;
	}

	return pos_to_godot(b3Body_GetWorldCenter(body_id));
}

Vector3 Box3DObject3D::get_center_of_mass_relative() const {
	return get_center_of_mass() - get_position();
}

Vector3 Box3DObject3D::get_center_of_mass_local() const {
	if (!in_space()) {
		return Vector3();
	}

	return to_godot(b3Body_GetLocalCenter(body_id));
}

Vector3 Box3DObject3D::get_linear_velocity() const {
	if (!in_space()) {
		return Vector3();
	}

	return to_godot(b3Body_GetLinearVelocity(body_id));
}

Vector3 Box3DObject3D::get_angular_velocity() const {
	if (!in_space()) {
		return Vector3();
	}

	return to_godot(b3Body_GetAngularVelocity(body_id));
}

AABB Box3DObject3D::get_aabb() const {
	if (in_space() && b3Body_GetShapeCount(body_id) > 0) {
		return to_godot(b3Body_ComputeAABB(body_id));
	}

	AABB result;
	bool first = true;

	for (const Box3DShapeInstance *instance : shapes) {
		if (instance->is_disabled()) {
			continue;
		}

		const AABB shape_aabb = instance->get_aabb(scale);
		if (first) {
			result = shape_aabb;
			first = false;
		} else {
			result.merge_with(shape_aabb);
		}
	}

	return get_transform_unscaled().xform(result);
}

void Box3DObject3D::shapes_changed() {
	if (in_space()) {
		space->enqueue_shapes_changed(&shapes_changed_element);
	}
}

void Box3DObject3D::commit_shapes() {
	if (space != nullptr) {
		space->dequeue_shapes_changed(&shapes_changed_element);
	}

	if (!in_space()) {
		return;
	}

	_build_shapes();
}

void Box3DObject3D::add_shape(Box3DShape3D *p_shape, Transform3D p_transform, bool p_disabled) {
	shapes.push_back(memnew(Box3DShapeInstance(this, p_shape, p_transform, p_disabled)));

	shapes_changed();
}

void Box3DObject3D::remove_shape(const Box3DShape3D *p_shape) {
	for (int i = (int)shapes.size() - 1; i >= 0; i--) {
		if (shapes[i]->get_shape() == p_shape) {
			remove_shape(i);
		}
	}
}

void Box3DObject3D::remove_shape(int p_index) {
	ERR_FAIL_INDEX(p_index, (int)shapes.size());

	Box3DShapeInstance *instance = shapes[p_index];
	shapes.remove_at(p_index);

	if (in_space()) {
		instance->destroy_built();
	} else {
		instance->forget_built();
	}

	memdelete(instance);

	shapes_changed();
}

Box3DShape3D *Box3DObject3D::get_shape(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, (int)shapes.size(), nullptr);

	return shapes[p_index]->get_shape();
}

void Box3DObject3D::set_shape(int p_index, Box3DShape3D *p_shape) {
	ERR_FAIL_INDEX(p_index, (int)shapes.size());

	shapes[p_index]->set_shape(p_shape);

	shapes_changed();
}

void Box3DObject3D::clear_shapes() {
	for (Box3DShapeInstance *instance : shapes) {
		if (in_space()) {
			instance->destroy_built();
		} else {
			instance->forget_built();
		}

		memdelete(instance);
	}

	shapes.clear();

	shapes_changed();
}

int Box3DObject3D::find_shape_index(uint32_t p_shape_instance_id) const {
	for (int i = 0; i < (int)shapes.size(); i++) {
		if (shapes[i]->get_id() == p_shape_instance_id) {
			return i;
		}
	}

	return -1;
}

Box3DShapeInstance *Box3DObject3D::find_shape_instance(uint32_t p_shape_instance_id) const {
	const int index = find_shape_index(p_shape_instance_id);
	return index >= 0 ? shapes[index] : nullptr;
}

Transform3D Box3DObject3D::get_shape_transform_unscaled(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, (int)shapes.size(), Transform3D());

	return shapes[p_index]->get_transform_unscaled();
}

Transform3D Box3DObject3D::get_shape_transform_scaled(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, (int)shapes.size(), Transform3D());

	return shapes[p_index]->get_transform_scaled(scale);
}

void Box3DObject3D::set_shape_transform(int p_index, Transform3D p_transform) {
	ERR_FAIL_INDEX(p_index, (int)shapes.size());

	shapes[p_index]->set_transform(p_transform);

	shapes_changed();
}

bool Box3DObject3D::is_shape_disabled(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, (int)shapes.size(), false);

	return shapes[p_index]->is_disabled();
}

void Box3DObject3D::set_shape_disabled(int p_index, bool p_disabled) {
	ERR_FAIL_INDEX(p_index, (int)shapes.size());

	shapes[p_index]->set_disabled(p_disabled);

	shapes_changed();
}

String Box3DObject3D::to_string() const {
	static const String fallback_name = "<unknown>";

	if (Box3DPhysicsServer3D::get_singleton()->is_on_separate_thread()) {
		return fallback_name; // Calling `Object::to_string` is not thread-safe.
	}

	Object *instance = get_instance();
	return instance != nullptr ? instance->to_string() : fallback_name;
}
