/**************************************************************************/
/*  box3d_query_shape.cpp                                                 */
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

#include "box3d_query_shape.h"

Box3DQueryShape::~Box3DQueryShape() {
	if (hull != nullptr) {
		b3DestroyHull(hull);
	}
}

void Box3DQueryShape::make_proxy(LocalVector<b3Vec3> &r_points, float &r_radius) const {
	r_points.clear();
	r_radius = 0.0f;

	switch (type) {
		case SPHERE: {
			r_points.push_back(sphere.center);
			r_radius = sphere.radius;
		} break;
		case CAPSULE: {
			r_points.push_back(capsule.center1);
			r_points.push_back(capsule.center2);
			r_radius = capsule.radius;
		} break;
		case HULL: {
			const b3Vec3 *points = b3GetHullPoints(hull);
			for (int i = 0; i < hull->vertexCount; i++) {
				r_points.push_back(points[i]);
			}
		} break;
	}
}

b3AABB Box3DQueryShape::compute_aabb() const {
	switch (type) {
		case SPHERE: {
			return b3ComputeSphereAABB(&sphere, b3Transform_identity);
		}
		case CAPSULE: {
			return b3ComputeCapsuleAABB(&capsule, b3Transform_identity);
		}
		default: {
			return b3ComputeHullAABB(hull, b3Transform_identity);
		}
	}
}

namespace {

constexpr int MANIFOLD_CAPACITY = 64;

struct TriangleContext {
	const Box3DQueryShape *query = nullptr;
	b3Transform body_to_query;
	float margin = 0.0f;
	LocalVector<Box3DContact> *contacts = nullptr;
};

b3Transform _make_relative(const b3WorldTransform &p_body, const b3Pos &p_origin) {
	b3Transform result;
	result.p = b3Vec3{ (float)(p_body.p.x - p_origin.x), (float)(p_body.p.y - p_origin.y), (float)(p_body.p.z - p_origin.z) };
	result.q = p_body.q;
	return result;
}

// Appends the points of `p_manifold` as contacts. The manifold lives in `p_frame` and its normal points from the
// first shape to the second one, which is the query shape if `p_first_is_target` is true.
void _append_manifold(const b3LocalManifold &p_manifold, const b3Transform &p_frame, bool p_first_is_target, float p_margin, LocalVector<Box3DContact> &r_contacts) {
	for (int i = 0; i < p_manifold.pointCount; i++) {
		const b3LocalManifoldPoint &point = p_manifold.points[i];

		if (point.separation > p_margin) {
			continue;
		}

		const b3Vec3 normal = b3RotateVector(p_frame.q, p_manifold.normal);
		const b3Vec3 mid = b3TransformPoint(p_frame, point.point);

		// The manifold point lies halfway between the two surfaces.
		const b3Vec3 on_first = b3MulSub(mid, 0.5f * point.separation, normal);
		const b3Vec3 on_second = b3MulAdd(mid, 0.5f * point.separation, normal);

		Box3DContact contact;
		contact.depth = -point.separation;
		contact.triangle_index = point.triangleIndex;

		if (p_first_is_target) {
			contact.normal = to_godot(normal);
			contact.point_on_target = to_godot(on_first);
			contact.point_on_query = to_godot(on_second);
		} else {
			contact.normal = -to_godot(normal);
			contact.point_on_query = to_godot(on_first);
			contact.point_on_target = to_godot(on_second);
		}

		r_contacts.push_back(contact);
	}
}

bool _triangle_callback(b3Vec3 p_a, b3Vec3 p_b, b3Vec3 p_c, int p_triangle_index, void *p_context) {
	TriangleContext &context = *(TriangleContext *)p_context;

	// Triangles are collided in the frame of the query shape.
	const b3Vec3 vertices[3] = {
		b3TransformPoint(context.body_to_query, p_a),
		b3TransformPoint(context.body_to_query, p_b),
		b3TransformPoint(context.body_to_query, p_c),
	};

	b3LocalManifoldPoint points[MANIFOLD_CAPACITY];
	b3LocalManifold manifold = {};
	manifold.points = points;

	switch (context.query->type) {
		case Box3DQueryShape::SPHERE: {
			b3CollideTriangleAndSphere(&manifold, MANIFOLD_CAPACITY, vertices, &context.query->sphere);
		} break;
		case Box3DQueryShape::CAPSULE: {
			b3SimplexCache cache = {};
			b3CollideTriangleAndCapsule(&manifold, MANIFOLD_CAPACITY, vertices, &context.query->capsule, &cache);
		} break;
		case Box3DQueryShape::HULL: {
			b3SATCache cache = {};
			b3CollideTriangleAndHull(&manifold, MANIFOLD_CAPACITY, vertices[0], vertices[1], vertices[2], 0, context.query->hull, &cache, true);
		} break;
	}

	for (int i = 0; i < manifold.pointCount; i++) {
		points[i].triangleIndex = p_triangle_index;
	}

	// The manifold normal points from the triangle to the query shape, in the frame of the query shape.
	_append_manifold(manifold, b3Transform_identity, true, context.margin, *context.contacts);

	return true;
}

} // namespace

void Box3DNarrowPhase::collide(const Box3DQueryShape &p_query, const b3Pos &p_origin, b3ShapeId p_target, float p_margin, LocalVector<Box3DContact> &r_contacts) {
	const b3BodyId body = b3Shape_GetBody(p_target);
	const b3Transform body_xf = _make_relative(b3Body_GetTransform(body), p_origin);
	const b3Transform query_to_body = b3InvertTransform(body_xf);

	const b3ShapeType target_type = b3Shape_GetType(p_target);

	b3LocalManifoldPoint points[MANIFOLD_CAPACITY];
	b3LocalManifold manifold = {};
	manifold.points = points;

	switch (target_type) {
		case b3_sphereShape: {
			const b3Sphere target = b3Shape_GetSphere(p_target);

			switch (p_query.type) {
				case Box3DQueryShape::SPHERE: {
					b3CollideSpheres(&manifold, MANIFOLD_CAPACITY, &target, &p_query.sphere, query_to_body);
					_append_manifold(manifold, body_xf, true, p_margin, r_contacts);
				} break;
				case Box3DQueryShape::CAPSULE: {
					// The capsule takes the first role, so the frame is the query frame and the normal points at the target.
					b3CollideCapsuleAndSphere(&manifold, MANIFOLD_CAPACITY, &p_query.capsule, &target, body_xf);
					_append_manifold(manifold, b3Transform_identity, false, p_margin, r_contacts);
				} break;
				case Box3DQueryShape::HULL: {
					b3SimplexCache cache = {};
					b3CollideHullAndSphere(&manifold, MANIFOLD_CAPACITY, p_query.hull, &target, body_xf, &cache);
					_append_manifold(manifold, b3Transform_identity, false, p_margin, r_contacts);
				} break;
			}
		} break;
		case b3_capsuleShape: {
			const b3Capsule target = b3Shape_GetCapsule(p_target);

			switch (p_query.type) {
				case Box3DQueryShape::SPHERE: {
					b3CollideCapsuleAndSphere(&manifold, MANIFOLD_CAPACITY, &target, &p_query.sphere, query_to_body);
					_append_manifold(manifold, body_xf, true, p_margin, r_contacts);
				} break;
				case Box3DQueryShape::CAPSULE: {
					b3CollideCapsules(&manifold, MANIFOLD_CAPACITY, &target, &p_query.capsule, query_to_body);
					_append_manifold(manifold, body_xf, true, p_margin, r_contacts);
				} break;
				case Box3DQueryShape::HULL: {
					b3SimplexCache cache = {};
					b3CollideHullAndCapsule(&manifold, MANIFOLD_CAPACITY, p_query.hull, &target, body_xf, &cache);
					_append_manifold(manifold, b3Transform_identity, false, p_margin, r_contacts);
				} break;
			}
		} break;
		case b3_hullShape: {
			const b3HullData *target = b3Shape_GetHull(p_target);

			switch (p_query.type) {
				case Box3DQueryShape::SPHERE: {
					b3SimplexCache cache = {};
					b3CollideHullAndSphere(&manifold, MANIFOLD_CAPACITY, target, &p_query.sphere, query_to_body, &cache);
					_append_manifold(manifold, body_xf, true, p_margin, r_contacts);
				} break;
				case Box3DQueryShape::CAPSULE: {
					b3SimplexCache cache = {};
					b3CollideHullAndCapsule(&manifold, MANIFOLD_CAPACITY, target, &p_query.capsule, query_to_body, &cache);
					_append_manifold(manifold, body_xf, true, p_margin, r_contacts);
				} break;
				case Box3DQueryShape::HULL: {
					b3SATCache cache = {};
					b3CollideHulls(&manifold, MANIFOLD_CAPACITY, target, p_query.hull, query_to_body, &cache);
					_append_manifold(manifold, body_xf, true, p_margin, r_contacts);
				} break;
			}
		} break;
		case b3_meshShape: {
			const b3Mesh mesh = b3Shape_GetMesh(p_target);

			// Look for triangles in the neighborhood of the query shape, in the space of the mesh.
			const b3AABB query_bounds = p_query.compute_aabb();
			const b3Vec3 extent = { p_margin, p_margin, p_margin };

			b3AABB bounds = b3AABB{ b3Sub(query_bounds.lowerBound, extent), b3Add(query_bounds.upperBound, extent) };

			// Transform the bounds of the query shape into the space of the mesh.
			const b3Vec3 corners[8] = {
				{ bounds.lowerBound.x, bounds.lowerBound.y, bounds.lowerBound.z },
				{ bounds.upperBound.x, bounds.lowerBound.y, bounds.lowerBound.z },
				{ bounds.lowerBound.x, bounds.upperBound.y, bounds.lowerBound.z },
				{ bounds.upperBound.x, bounds.upperBound.y, bounds.lowerBound.z },
				{ bounds.lowerBound.x, bounds.lowerBound.y, bounds.upperBound.z },
				{ bounds.upperBound.x, bounds.lowerBound.y, bounds.upperBound.z },
				{ bounds.lowerBound.x, bounds.upperBound.y, bounds.upperBound.z },
				{ bounds.upperBound.x, bounds.upperBound.y, bounds.upperBound.z },
			};

			b3AABB mesh_bounds = b3MakeAABB(corners, 8, 0.0f);
			b3Vec3 transformed[8];
			for (int i = 0; i < 8; i++) {
				transformed[i] = b3TransformPoint(query_to_body, corners[i]);
			}
			mesh_bounds = b3MakeAABB(transformed, 8, 0.0f);

			TriangleContext context;
			context.query = &p_query;
			context.body_to_query = body_xf;
			context.margin = p_margin;
			context.contacts = &r_contacts;

			b3QueryMesh(&mesh, mesh_bounds, _triangle_callback, &context);
		} break;
		default: {
			// Height fields are turned into meshes by the shapes, and compounds are never created.
		} break;
	}
}
