/**************************************************************************/
/*  box3d_soft_body_3d.h                                                  */
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

#include "core/templates/local_vector.h"
#include "core/templates/rid.h"
#include "servers/physics_3d/physics_server_3d.h"

class Box3DSpace3D;

// Box3D has no soft body simulation. This keeps the state that Godot hands to the physics server so that scenes using
// SoftBody3D keep loading, and reports what could not be simulated.
class Box3DSoftBody3D {
	RID rid;
	RID mesh;
	ObjectID instance_id;
	Box3DSpace3D *space = nullptr;

	LocalVector<RID> exceptions;

	Transform3D transform;

	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;

	float mass = 1.0f;
	float stiffness = 0.5f;
	float shrinking_factor = 0.0f;
	float pressure = 0.0f;
	float linear_damping = 0.01f;
	float drag = 0.0f;
	int simulation_precision = 5;

	bool pickable = false;

	void _report(const char *p_operation) const;

public:
	void set_rid(const RID &p_rid) { rid = p_rid; }
	RID get_rid() const { return rid; }

	ObjectID get_instance_id() const { return instance_id; }
	void set_instance_id(ObjectID p_id) { instance_id = p_id; }

	Box3DSpace3D *get_space() const { return space; }
	void set_space(Box3DSpace3D *p_space);

	void set_mesh(const RID &p_mesh);
	AABB get_bounds() const { return AABB(); }

	void update_rendering_server(PhysicsServer3DRenderingServerHandler *p_handler) {}

	uint32_t get_collision_layer() const { return collision_layer; }
	void set_collision_layer(uint32_t p_layer) { collision_layer = p_layer; }

	uint32_t get_collision_mask() const { return collision_mask; }
	void set_collision_mask(uint32_t p_mask) { collision_mask = p_mask; }

	void add_collision_exception(const RID &p_excepted_body) { exceptions.push_back(p_excepted_body); }
	void remove_collision_exception(const RID &p_excepted_body) { exceptions.erase(p_excepted_body); }
	const LocalVector<RID> &get_collision_exceptions() const { return exceptions; }

	Variant get_state(PhysicsServer3D::BodyState p_state) const;
	void set_state(PhysicsServer3D::BodyState p_state, const Variant &p_value);

	void set_transform(const Transform3D &p_transform) { transform = p_transform; }
	void set_pickable(bool p_enabled) { pickable = p_enabled; }

	void set_simulation_precision(int p_precision) { simulation_precision = p_precision; }
	int get_simulation_precision() const { return simulation_precision; }

	void set_mass(float p_mass) { mass = p_mass; }
	float get_mass() const { return mass; }

	void set_stiffness_coefficient(float p_coefficient) { stiffness = p_coefficient; }
	float get_stiffness_coefficient() const { return stiffness; }

	void set_shrinking_factor(float p_factor) { shrinking_factor = p_factor; }
	float get_shrinking_factor() const { return shrinking_factor; }

	void set_pressure(float p_pressure) { pressure = p_pressure; }
	float get_pressure() const { return pressure; }

	void set_linear_damping(float p_damping) { linear_damping = p_damping; }
	float get_linear_damping() const { return linear_damping; }

	void set_drag(float p_drag) { drag = p_drag; }
	float get_drag() const { return drag; }

	void set_vertex_position(int p_index, const Vector3 &p_position);
	Vector3 get_vertex_position(int p_index) const;

	void apply_vertex_impulse(int p_index, const Vector3 &p_impulse);
	void apply_vertex_force(int p_index, const Vector3 &p_force);
	void apply_central_impulse(const Vector3 &p_impulse);
	void apply_central_force(const Vector3 &p_force);

	void unpin_all_vertices();
	void pin_vertex(int p_index);
	void unpin_vertex(int p_index);
	bool is_vertex_pinned(int p_index) const { return false; }
};
