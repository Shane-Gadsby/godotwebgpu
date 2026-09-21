/**************************************************************************/
/*  box3d_query_shape.h                                                   */
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

// A convex shape used to query the world. The shape is baked into a space where positions are relative to a chosen
// origin, so that no additional transform is needed once it has been built.
class Box3DQueryShape {
public:
	enum Type {
		SPHERE,
		CAPSULE,
		HULL,
	};

	Type type = SPHERE;
	b3Sphere sphere = {};
	b3Capsule capsule = {};
	b3HullData *hull = nullptr;

	Box3DQueryShape() = default;
	Box3DQueryShape(const Box3DQueryShape &) = delete;
	Box3DQueryShape &operator=(const Box3DQueryShape &) = delete;
	~Box3DQueryShape();

	// Fills `r_points` and `r_radius` with the point cloud describing this shape for GJK based queries.
	void make_proxy(LocalVector<b3Vec3> &r_points, float &r_radius) const;

	b3AABB compute_aabb() const;
};

struct Box3DContact {
	// Points are relative to the origin of the query.
	Vector3 point_on_query;
	Vector3 point_on_target;

	// Points from the target towards the query shape.
	Vector3 normal;

	// Positive when the shapes overlap and negative when they are apart.
	float depth = 0.0f;

	int triangle_index = -1;
};

namespace Box3DNarrowPhase {

// Collides `p_query`, which sits at `p_origin`, with the Box3D shape `p_target`, appending every contact that comes
// within `p_margin` to `r_contacts`.
void collide(const Box3DQueryShape &p_query, const b3Pos &p_origin, b3ShapeId p_target, float p_margin, LocalVector<Box3DContact> &r_contacts);

} // namespace Box3DNarrowPhase
