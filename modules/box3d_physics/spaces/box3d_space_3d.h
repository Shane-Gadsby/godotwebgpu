/**************************************************************************/
/*  box3d_space_3d.h                                                      */
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

#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid.h"
#include "core/templates/self_list.h"
#include "servers/physics_3d/physics_server_3d.h"

class Box3DArea3D;
class Box3DBody3D;
class Box3DObject3D;
class Box3DPhysicsDirectSpaceState3D;

class Box3DSpace3D {
	SelfList<Box3DBody3D>::List body_call_queries_list;
	SelfList<Box3DArea3D>::List area_call_queries_list;
	SelfList<Box3DObject3D>::List shapes_changed_list;
	SelfList<Box3DBody3D>::List force_bodies_list;
	SelfList<Box3DBody3D>::List kinematic_bodies_list;

	HashSet<Box3DBody3D *> bodies;
	HashSet<Box3DBody3D *> contact_reporters;

	RID rid;

	b3WorldId world = b3_nullWorldId;
	Box3DPhysicsDirectSpaceState3D *direct_state = nullptr;
	Box3DArea3D *default_area = nullptr;

	Vector3 world_gravity;

	b3BodyId anchor_body = b3_nullBodyId;

	float last_step = 0.0f;

	uint16_t default_area_changed_count = 0;

	bool active = false;
	bool stepping = false;
	bool uses_world_gravity_flag = true;
	bool environment_refresh_needed = false;

	static bool _custom_filter(b3ShapeId p_shape_a, b3ShapeId p_shape_b, void *p_context);
	static float _combine_friction(float p_friction_a, uint64_t p_material_a, float p_friction_b, uint64_t p_material_b);
	static float _combine_restitution(float p_restitution_a, uint64_t p_material_a, float p_restitution_b, uint64_t p_material_b);

	void _pre_step(float p_step);
	void _post_step(float p_step);
	void _process_sensor_events();
	void _update_world_gravity();

public:
	Box3DSpace3D();
	~Box3DSpace3D();

	void step(float p_step);

	void call_queries();

	RID get_rid() const { return rid; }
	void set_rid(const RID &p_rid) { rid = p_rid; }

	b3WorldId get_world() const { return world; }

	// A static body at the world origin that joints attach to in place of a missing body.
	b3BodyId get_anchor_body();

	bool is_active() const { return active; }
	void set_active(bool p_active) { active = p_active; }

	bool is_stepping() const { return stepping; }

	double get_param(PhysicsServer3D::SpaceParameter p_param) const;
	void set_param(PhysicsServer3D::SpaceParameter p_param, double p_value);

	Box3DPhysicsDirectSpaceState3D *get_direct_state();

	Box3DArea3D *get_default_area() const { return default_area; }
	void set_default_area(Box3DArea3D *p_area) { default_area = p_area; }

	void increment_default_area_changed_count();
	uint16_t get_default_area_changed_count() const { return default_area_changed_count; }

	// Whether gravity comes from Box3D's own world gravity, which is only possible without point gravity in the default area.
	bool uses_world_gravity() const { return uses_world_gravity_flag; }
	Vector3 get_world_gravity() const { return world_gravity; }

	float get_last_step() const { return last_step; }

	void register_body(Box3DBody3D *p_body);
	void unregister_body(Box3DBody3D *p_body);

	void register_contact_reporter(Box3DBody3D *p_body);
	void unregister_contact_reporter(Box3DBody3D *p_body);

	void enqueue_call_queries(SelfList<Box3DBody3D> *p_body);
	void enqueue_call_queries(SelfList<Box3DArea3D> *p_area);
	void dequeue_call_queries(SelfList<Box3DBody3D> *p_body);
	void dequeue_call_queries(SelfList<Box3DArea3D> *p_area);

	void enqueue_shapes_changed(SelfList<Box3DObject3D> *p_object);
	void dequeue_shapes_changed(SelfList<Box3DObject3D> *p_object);

	void enqueue_forces(SelfList<Box3DBody3D> *p_body);
	void dequeue_forces(SelfList<Box3DBody3D> *p_body);

	void enqueue_kinematic(SelfList<Box3DBody3D> *p_body);
	void dequeue_kinematic(SelfList<Box3DBody3D> *p_body);

	// Commits any deferred shape changes, so that queries see up to date geometry.
	void flush_pending_shapes();

	int get_awake_body_count() const;
};
