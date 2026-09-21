/**************************************************************************/
/*  box3d_physics_direct_space_state_3d.h                                 */
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
#include "servers/physics_3d/physics_server_3d.h"

class Box3DBody3D;
class Box3DObject3D;
class Box3DQueryShape;
class Box3DShapeInstance;
class Box3DSpace3D;

class Box3DPhysicsDirectSpaceState3D final : public PhysicsDirectSpaceState3D {
	GDCLASS(Box3DPhysicsDirectSpaceState3D, PhysicsDirectSpaceState3D)

	Box3DSpace3D *space = nullptr;

	static void _bind_methods() {}

	struct Recovery;

	bool _body_motion_recover(const Box3DBody3D &p_body, const Transform3D &p_transform, float p_margin, const HashSet<RID> &p_excluded_bodies, const HashSet<ObjectID> &p_excluded_objects, Vector3 &r_recovery) const;
	bool _body_motion_cast(const Box3DBody3D &p_body, const Transform3D &p_transform, const Vector3 &p_motion, bool p_collide_separation_ray, const HashSet<RID> &p_excluded_bodies, const HashSet<ObjectID> &p_excluded_objects, real_t &r_safe_fraction, real_t &r_unsafe_fraction) const;
	bool _body_motion_collide(const Box3DBody3D &p_body, const Transform3D &p_transform, const Vector3 &p_motion, float p_margin, int p_max_collisions, bool p_collide_separation_ray, const HashSet<RID> &p_excluded_bodies, const HashSet<ObjectID> &p_excluded_objects, PhysicsServer3D::MotionResult *r_result) const;

public:
	Box3DPhysicsDirectSpaceState3D() = default;
	explicit Box3DPhysicsDirectSpaceState3D(Box3DSpace3D *p_space);

	virtual bool intersect_ray(const RayParameters &p_parameters, RayResult &r_result) override;
	virtual int intersect_point(const PointParameters &p_parameters, ShapeResult *r_results, int p_result_max) override;
	virtual int intersect_shape(const ShapeParameters &p_parameters, ShapeResult *r_results, int p_result_max) override;
	virtual bool cast_motion(const ShapeParameters &p_parameters, real_t &r_closest_safe, real_t &r_closest_unsafe, ShapeRestInfo *r_info = nullptr) override;
	virtual bool collide_shape(const ShapeParameters &p_parameters, Vector3 *r_results, int p_result_max, int &r_result_count) override;
	virtual bool rest_info(const ShapeParameters &p_parameters, ShapeRestInfo *r_info) override;
	virtual Vector3 get_closest_point_to_object_volume(RID p_object, Vector3 p_point) const override;

	bool body_test_motion(const Box3DBody3D &p_body, const PhysicsServer3D::MotionParameters &p_parameters, PhysicsServer3D::MotionResult *r_result) const;

	Box3DSpace3D &get_space() const { return *space; }
};
