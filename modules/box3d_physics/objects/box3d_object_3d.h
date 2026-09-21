/**************************************************************************/
/*  box3d_object_3d.h                                                     */
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
#include "../shapes/box3d_shape_3d.h"

#include "core/object/object.h"
#include "core/string/ustring.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid.h"
#include "core/templates/self_list.h"

class Box3DArea3D;
class Box3DBody3D;
class Box3DSpace3D;

// Common base of everything that lives in a Box3D world as a body with shapes: rigid/kinematic/static bodies and areas.
class Box3DObject3D {
	friend class Box3DShape3D;

public:
	enum ObjectType : char {
		OBJECT_TYPE_INVALID,
		OBJECT_TYPE_BODY,
		OBJECT_TYPE_AREA,
	};

protected:
	SelfList<Box3DObject3D> shapes_changed_element;

	RID rid;
	ObjectID instance_id;
	Box3DSpace3D *space = nullptr;
	b3BodyId body_id = b3_nullBodyId;

	LocalVector<Box3DShapeInstance *> shapes;

	// Transform used while the object is not in a space. Once it is, Box3D owns the transform.
	Transform3D transform_unscaled;
	Vector3 scale = Vector3(1, 1, 1);

	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;

	ObjectType object_type = OBJECT_TYPE_INVALID;

	bool pickable = false;

	virtual b3BodyType _get_body_type() const = 0;
	virtual b3ShapeDef _make_shape_def() const = 0;
	virtual void _fill_body_def(b3BodyDef &r_def) const = 0;

	virtual void _add_to_space();
	virtual void _remove_from_space();

	void _build_shapes();
	void _update_shape_filters();

	virtual void _shapes_committed() {}
	virtual void _space_changing() {}
	virtual void _space_changed() {}
	virtual void _collision_layer_changed();
	virtual void _collision_mask_changed() {}

public:
	explicit Box3DObject3D(ObjectType p_object_type);
	virtual ~Box3DObject3D();

	ObjectType get_type() const { return object_type; }

	bool is_body() const { return object_type == OBJECT_TYPE_BODY; }
	bool is_area() const { return object_type == OBJECT_TYPE_AREA; }

	Box3DBody3D *as_body() { return is_body() ? reinterpret_cast<Box3DBody3D *>(this) : nullptr; }
	const Box3DBody3D *as_body() const { return is_body() ? reinterpret_cast<const Box3DBody3D *>(this) : nullptr; }

	Box3DArea3D *as_area() { return is_area() ? reinterpret_cast<Box3DArea3D *>(this) : nullptr; }
	const Box3DArea3D *as_area() const { return is_area() ? reinterpret_cast<const Box3DArea3D *>(this) : nullptr; }

	RID get_rid() const { return rid; }
	void set_rid(const RID &p_rid) { rid = p_rid; }

	ObjectID get_instance_id() const { return instance_id; }
	void set_instance_id(ObjectID p_id) { instance_id = p_id; }
	Object *get_instance() const;

	b3BodyId get_body_id() const { return body_id; }

	Box3DSpace3D *get_space() const { return space; }
	void set_space(Box3DSpace3D *p_space);
	bool in_space() const { return space != nullptr && B3_IS_NON_NULL(body_id); }

	uint32_t get_collision_layer() const { return collision_layer; }
	void set_collision_layer(uint32_t p_layer);

	uint32_t get_collision_mask() const { return collision_mask; }
	void set_collision_mask(uint32_t p_mask);

	bool is_pickable() const { return pickable; }
	void set_pickable(bool p_enabled) { pickable = p_enabled; }

	virtual Vector3 get_velocity_at_position(const Vector3 &p_position) const = 0;

	bool can_collide_with(const Box3DObject3D &p_other) const { return (collision_mask & p_other.get_collision_layer()) != 0; }
	bool can_interact_with(const Box3DObject3D &p_other) const;
	virtual bool can_interact_with(const Box3DBody3D &p_other) const = 0;
	virtual bool can_interact_with(const Box3DArea3D &p_other) const = 0;

	virtual bool reports_contacts() const = 0;

	virtual void pre_step(float p_step) {}

	// Shape management.

	Transform3D get_transform_unscaled() const;
	Transform3D get_transform_scaled() const;

	Vector3 get_scale() const { return scale; }
	Basis get_basis() const { return get_transform_unscaled().basis; }
	Vector3 get_position() const;

	Vector3 get_center_of_mass() const;
	Vector3 get_center_of_mass_relative() const;
	Vector3 get_center_of_mass_local() const;

	Vector3 get_linear_velocity() const;
	Vector3 get_angular_velocity() const;

	AABB get_aabb() const;

	virtual bool has_custom_center_of_mass() const = 0;
	virtual Vector3 get_center_of_mass_custom() const = 0;

	void shapes_changed();
	void commit_shapes();

	void add_shape(Box3DShape3D *p_shape, Transform3D p_transform, bool p_disabled);
	void remove_shape(const Box3DShape3D *p_shape);
	void remove_shape(int p_index);

	Box3DShape3D *get_shape(int p_index) const;
	void set_shape(int p_index, Box3DShape3D *p_shape);

	void clear_shapes();

	int get_shape_count() const { return shapes.size(); }

	int find_shape_index(uint32_t p_shape_instance_id) const;
	Box3DShapeInstance *find_shape_instance(uint32_t p_shape_instance_id) const;

	Box3DShapeInstance *get_shape_instance(int p_index) const { return shapes[p_index]; }
	const LocalVector<Box3DShapeInstance *> &get_shape_instances() const { return shapes; }

	Transform3D get_shape_transform_unscaled(int p_index) const;
	Transform3D get_shape_transform_scaled(int p_index) const;
	void set_shape_transform(int p_index, Transform3D p_transform);

	bool is_shape_disabled(int p_index) const;
	void set_shape_disabled(int p_index, bool p_disabled);

	String to_string() const;
};
