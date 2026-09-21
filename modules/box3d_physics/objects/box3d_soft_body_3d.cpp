/**************************************************************************/
/*  box3d_soft_body_3d.cpp                                                */
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

#include "box3d_soft_body_3d.h"

#include "../misc/box3d_diagnostics.h"
#include "../spaces/box3d_space_3d.h"

void Box3DSoftBody3D::_report(const char *p_operation) const {
	BOX3D_UNSUPPORTED_KEYED("Soft body simulation",
			vformat("%s was called on a SoftBody3D.", p_operation),
			"Box3D is a rigid-body engine and has no cloth, rope or soft body solver. The soft body will not simulate or collide.",
			vformat("soft_body=%s mesh=%s in_space=%s mass=%f stiffness=%f shrinking_factor=%f pressure=%f linear_damping=%f drag=%f simulation_precision=%d collision_layer=%d collision_mask=%d transform=%s",
					rid, mesh, space != nullptr, mass, stiffness, shrinking_factor, pressure, linear_damping, drag, simulation_precision, collision_layer, collision_mask, transform),
			"soft_body_simulation");
}

void Box3DSoftBody3D::set_space(Box3DSpace3D *p_space) {
	space = p_space;

	if (space != nullptr) {
		_report("soft_body_set_space");
	}
}

void Box3DSoftBody3D::set_mesh(const RID &p_mesh) {
	mesh = p_mesh;
}

Variant Box3DSoftBody3D::get_state(PhysicsServer3D::BodyState p_state) const {
	switch (p_state) {
		case PhysicsServer3D::BODY_STATE_TRANSFORM: {
			return transform;
		}
		case PhysicsServer3D::BODY_STATE_SLEEPING: {
			return false;
		}
		case PhysicsServer3D::BODY_STATE_CAN_SLEEP: {
			return true;
		}
		default: {
			return Variant();
		}
	}
}

void Box3DSoftBody3D::set_state(PhysicsServer3D::BodyState p_state, const Variant &p_value) {
	if (p_state == PhysicsServer3D::BODY_STATE_TRANSFORM) {
		transform = p_value;
	}
}

void Box3DSoftBody3D::set_vertex_position(int p_index, const Vector3 &p_position) {
	_report("soft_body_move_point");
}

Vector3 Box3DSoftBody3D::get_vertex_position(int p_index) const {
	_report("soft_body_get_point_global_position");
	return Vector3();
}

void Box3DSoftBody3D::apply_vertex_impulse(int p_index, const Vector3 &p_impulse) {
	_report("soft_body_apply_point_impulse");
}

void Box3DSoftBody3D::apply_vertex_force(int p_index, const Vector3 &p_force) {
	_report("soft_body_apply_point_force");
}

void Box3DSoftBody3D::apply_central_impulse(const Vector3 &p_impulse) {
	_report("soft_body_apply_central_impulse");
}

void Box3DSoftBody3D::apply_central_force(const Vector3 &p_force) {
	_report("soft_body_apply_central_force");
}

void Box3DSoftBody3D::unpin_all_vertices() {
	_report("soft_body_remove_all_pinned_points");
}

void Box3DSoftBody3D::pin_vertex(int p_index) {
	_report("soft_body_pin_point");
}

void Box3DSoftBody3D::unpin_vertex(int p_index) {
	_report("soft_body_pin_point");
}
