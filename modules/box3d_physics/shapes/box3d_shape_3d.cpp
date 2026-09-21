/**************************************************************************/
/*  box3d_shape_3d.cpp                                                    */
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

#include "box3d_shape_3d.h"

#include "../box3d_project_settings.h"
#include "../misc/box3d_diagnostics.h"
#include "../objects/box3d_object_3d.h"

namespace {

constexpr float DEFAULT_SOLVER_BIAS = 0.0f;

_FORCE_INLINE_ float _clamp_scale_component(float p_scale) {
	if (Math::abs(p_scale) < B3_MIN_SCALE) {
		return p_scale < 0.0f ? -B3_MIN_SCALE : B3_MIN_SCALE;
	}
	return p_scale;
}

// Godot's transforms can contain a different scale on every axis, but Box3D spheres and capsules only support one.
float _uniform_scale(const Basis &p_basis, const String &p_shape_desc) {
	const Vector3 scale = p_basis.get_scale_abs();
	const float average = (float)((scale.x + scale.y + scale.z) / 3.0);
	if (!Math::is_equal_approx(scale.x, scale.y, (real_t)0.01) || !Math::is_equal_approx(scale.y, scale.z, (real_t)0.01)) {
		BOX3D_UNSUPPORTED_KEYED("Non-uniform scale on round shape",
				"A sphere or capsule shape was built with a scale that differs between axes.",
				"Box3D spheres and capsules can only be scaled uniformly, so the average scale is used instead.",
				vformat("shape=%s combined_scale=%v", p_shape_desc, scale),
				"non_uniform_round_scale");
	}
	return average;
}

} // namespace

/* Shape instance */

Box3DShapeInstance::Box3DShapeInstance(Box3DObject3D *p_owner, Box3DShape3D *p_shape, const Transform3D &p_transform, bool p_disabled) :
		owner(p_owner), shape(p_shape), transform(p_transform), disabled(p_disabled) {
	shape->add_owner(owner);
}

Box3DShapeInstance::~Box3DShapeInstance() {
	if (shape != nullptr) {
		shape->remove_owner(owner);
	}
}

void Box3DShapeInstance::set_shape(Box3DShape3D *p_shape) {
	if (shape == p_shape) {
		return;
	}

	shape->remove_owner(owner);
	shape = p_shape;
	shape->add_owner(owner);
}

AABB Box3DShapeInstance::get_aabb(const Vector3 &p_scale) const {
	return get_transform_scaled(p_scale).xform(shape->get_aabb());
}

bool Box3DShapeInstance::build(b3BodyId p_body, b3ShapeDef p_def, const Vector3 &p_scale, bool p_body_is_static) {
	destroy_built();

	if (disabled) {
		return true;
	}

	p_def.userData = this;

	return shape->build(*this, p_body, p_def, get_transform_scaled(p_scale), p_body_is_static);
}

void Box3DShapeInstance::destroy_built() {
	for (const b3ShapeId &shape_id : built_shapes) {
		if (b3Shape_IsValid(shape_id)) {
			b3DestroyShape(shape_id, false);
		}
	}

	forget_built();
}

void Box3DShapeInstance::forget_built() {
	built_shapes.clear();

	for (const Box3DOwnedData &owned : owned_data) {
		switch (owned.type) {
			case Box3DOwnedData::MESH: {
				b3DestroyMesh((b3MeshData *)owned.data);
			} break;
			case Box3DOwnedData::HEIGHT_FIELD: {
				b3DestroyHeightField((b3HeightFieldData *)owned.data);
			} break;
		}
	}

	owned_data.clear();
}

/* Shape base */

Box3DShape3D::~Box3DShape3D() = default;

String Box3DShape3D::_owners_to_string() const {
	const int owner_count = ref_counts_by_owner.size();

	if (owner_count == 0) {
		return "'<unknown>' and 0 other object(s)";
	}

	const Box3DObject3D &random_owner = *ref_counts_by_owner.begin()->key;

	return vformat("'%s' and %d other object(s)", random_owner.to_string(), owner_count - 1);
}

void Box3DShape3D::add_owner(Box3DObject3D *p_owner) {
	ref_counts_by_owner[p_owner]++;
}

void Box3DShape3D::remove_owner(Box3DObject3D *p_owner) {
	if (--ref_counts_by_owner[p_owner] <= 0) {
		ref_counts_by_owner.erase(p_owner);
	}
}

void Box3DShape3D::remove_self() {
	// `remove_owner` will be called when we `remove_shape`, so we need to copy the map since the iterator would be
	// invalidated from underneath us.
	const HashMap<Box3DObject3D *, int> ref_counts_by_owner_copy(ref_counts_by_owner);

	for (const KeyValue<Box3DObject3D *, int> &E : ref_counts_by_owner_copy) {
		E.key->remove_shape(this);
	}
}

float Box3DShape3D::get_solver_bias() const {
	return DEFAULT_SOLVER_BIAS;
}

void Box3DShape3D::set_solver_bias(float p_bias) {
	if (!Math::is_equal_approx(p_bias, DEFAULT_SOLVER_BIAS)) {
		BOX3D_UNSUPPORTED_KEYED("Custom shape solver bias",
				"shape_set_custom_solver_bias() was called with a non-default value.",
				"Box3D resolves contacts with its own soft-constraint solver and has no per-shape solver bias.",
				vformat("shape=%s requested_bias=%f owners=%s", to_string(), p_bias, _owners_to_string()),
				"shape_solver_bias");
	}
}

void Box3DShape3D::destroy() {
	const HashMap<Box3DObject3D *, int> ref_counts_by_owner_copy(ref_counts_by_owner);

	for (const KeyValue<Box3DObject3D *, int> &E : ref_counts_by_owner_copy) {
		E.key->shapes_changed();
		// Mesh shapes only reference their data, so the objects have to stop using the old data right away.
		E.key->commit_shapes();
	}
}

bool Box3DShape3D::_register_shape(Box3DShapeInstance &p_instance, b3ShapeId p_id) const {
	if (!b3Shape_IsValid(p_id)) {
		return false;
	}

	p_instance.built_shapes.push_back(p_id);
	return true;
}

b3HullData *Box3DShape3D::_bake_hull(const b3HullData *p_hull, const Transform3D &p_xform) {
	Quaternion rotation;
	Vector3 scale;

	if (Box3DMath::decompose_affine(p_xform, rotation, scale)) {
		const b3Transform transform = { to_b3(p_xform.origin), to_b3(rotation) };
		const b3Vec3 b3_scale = { _clamp_scale_component((float)scale.x), _clamp_scale_component((float)scale.y), _clamp_scale_component((float)scale.z) };

		return b3CloneAndTransformHull(p_hull, transform, b3_scale);
	}

	// The transform contains shear, which a hull can only represent by baking it into the points.
	const b3Vec3 *points = b3GetHullPoints(p_hull);
	LocalVector<b3Vec3> baked_points;
	baked_points.resize(p_hull->vertexCount);

	for (int i = 0; i < p_hull->vertexCount; i++) {
		baked_points[i] = to_b3(p_xform.xform(to_godot(points[i])));
	}

	return b3CreateHull(baked_points.ptr(), (int)baked_points.size(), B3_MAX_HULL_VERTICES);
}

bool Box3DShape3D::_make_hull_query_shape(const b3HullData *p_hull, const Transform3D &p_xform, Box3DQueryShape &r_shape) {
	b3HullData *baked = _bake_hull(p_hull, p_xform);

	if (baked == nullptr) {
		return false;
	}

	if (r_shape.hull != nullptr) {
		b3DestroyHull(r_shape.hull);
	}

	r_shape.type = Box3DQueryShape::HULL;
	r_shape.hull = baked;
	return true;
}

bool Box3DShape3D::_build_hull(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const b3HullData *p_hull, const Transform3D &p_xform) const {
	ERR_FAIL_NULL_V(p_hull, false);

	Quaternion rotation;
	Vector3 scale;

	if (Box3DMath::decompose_affine(p_xform, rotation, scale)) {
		const b3Transform transform = { to_b3(p_xform.origin), to_b3(rotation) };
		const b3Vec3 b3_scale = { _clamp_scale_component((float)scale.x), _clamp_scale_component((float)scale.y), _clamp_scale_component((float)scale.z) };

		return _register_shape(p_instance, b3CreateTransformedHullShape(p_body, &p_def, p_hull, transform, b3_scale));
	}

	// The transform contains shear, which a hull can only represent by baking it into the points.
	const b3Vec3 *points = b3GetHullPoints(p_hull);
	LocalVector<b3Vec3> baked_points;
	baked_points.resize(p_hull->vertexCount);

	for (int i = 0; i < p_hull->vertexCount; i++) {
		baked_points[i] = to_b3(p_xform.xform(to_godot(points[i])));
	}

	b3HullData *baked_hull = b3CreateHull(baked_points.ptr(), (int)baked_points.size(), B3_MAX_HULL_VERTICES);
	if (baked_hull == nullptr) {
		return false;
	}

	const bool result = _register_shape(p_instance, b3CreateHullShape(p_body, &p_def, baked_hull));
	b3DestroyHull(baked_hull);
	return result;
}

/* World boundary */

void Box3DWorldBoundaryShape3D::set_data(const Variant &p_data) {
	ERR_FAIL_COND(p_data.get_type() != Variant::PLANE);

	const Plane new_plane = p_data;
	if (unlikely(new_plane == plane)) {
		return;
	}

	plane = new_plane;

	destroy();
}

AABB Box3DWorldBoundaryShape3D::get_aabb() const {
	const float size = Box3DProjectSettings::world_boundary_shape_size;
	const float half_size = size / 2.0f;
	return AABB(Vector3(-half_size, -half_size, -half_size), Vector3(size, half_size, size));
}

bool Box3DWorldBoundaryShape3D::build(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_xform, bool p_body_is_static) const {
	// Box3D has no infinite plane, so the boundary is approximated by a very large, thick box lying under the plane.
	const float half_size = Box3DProjectSettings::world_boundary_shape_size * 0.5f;
	const float half_thickness = MIN(half_size, 100.0f);

	const Vector3 normal = plane.normal.normalized();
	const Vector3 tangent = (Math::abs(normal.z) < 0.9f ? normal.cross(Vector3(0, 0, 1)) : normal.cross(Vector3(1, 0, 0))).normalized();
	const Vector3 bitangent = tangent.cross(normal);

	const Basis orientation(tangent, normal, bitangent);
	const Vector3 center = normal * (real_t)plane.d - normal * half_thickness;

	const b3BoxHull box = b3MakeBoxHull(half_size, half_thickness, half_size);

	return _build_hull(p_instance, p_body, p_def, &box.base, p_xform * Transform3D(orientation, center));
}

String Box3DWorldBoundaryShape3D::to_string() const {
	return vformat("{plane=%s}", plane);
}

/* Separation ray */

Variant Box3DSeparationRayShape3D::get_data() const {
	Dictionary data;
	data["length"] = length;
	data["slide_on_slope"] = slide_on_slope;
	return data;
}

void Box3DSeparationRayShape3D::set_data(const Variant &p_data) {
	ERR_FAIL_COND(p_data.get_type() != Variant::DICTIONARY);

	const Dictionary data = p_data;

	const Variant maybe_length = data.get("length", Variant());
	ERR_FAIL_COND(maybe_length.get_type() != Variant::FLOAT);

	const Variant maybe_slide_on_slope = data.get("slide_on_slope", Variant());
	ERR_FAIL_COND(maybe_slide_on_slope.get_type() != Variant::BOOL);

	const float new_length = maybe_length;
	const bool new_slide_on_slope = maybe_slide_on_slope;

	if (unlikely(new_length == length && new_slide_on_slope == slide_on_slope)) {
		return;
	}

	length = new_length;
	slide_on_slope = new_slide_on_slope;

	destroy();
}

AABB Box3DSeparationRayShape3D::get_aabb() const {
	constexpr float size_xy = 0.1f;
	constexpr float half_size_xy = size_xy / 2.0f;
	return AABB(Vector3(-half_size_xy, -half_size_xy, 0.0f), Vector3(size_xy, size_xy, length));
}

String Box3DSeparationRayShape3D::to_string() const {
	return vformat("{length=%f slide_on_slope=%s}", length, slide_on_slope);
}

/* Sphere */

void Box3DSphereShape3D::set_data(const Variant &p_data) {
	ERR_FAIL_COND(!p_data.is_num());

	const float new_radius = p_data;
	if (unlikely(new_radius == radius)) {
		return;
	}

	radius = new_radius;

	destroy();
}

AABB Box3DSphereShape3D::get_aabb() const {
	const Vector3 half_extents(radius, radius, radius);
	return AABB(-half_extents, half_extents * 2.0f);
}

bool Box3DSphereShape3D::build(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_xform, bool p_body_is_static) const {
	if (radius <= 0.0f) {
		return true;
	}

	const b3Sphere sphere = { to_b3(p_xform.origin), MAX(radius * _uniform_scale(p_xform.basis, to_string()), 0.001f) };

	return _register_shape(p_instance, b3CreateSphereShape(p_body, &p_def, &sphere));
}

bool Box3DSphereShape3D::make_query_shape(const Transform3D &p_xform, Box3DQueryShape &r_shape) const {
	r_shape.type = Box3DQueryShape::SPHERE;
	r_shape.sphere = { to_b3(p_xform.origin), MAX(radius * _uniform_scale(p_xform.basis, to_string()), 0.0f) };
	return true;
}

String Box3DSphereShape3D::to_string() const {
	return vformat("{radius=%f}", radius);
}

/* Box */

void Box3DBoxShape3D::set_data(const Variant &p_data) {
	ERR_FAIL_COND(p_data.get_type() != Variant::VECTOR3);

	const Vector3 new_half_extents = p_data;
	if (unlikely(new_half_extents == half_extents)) {
		return;
	}

	half_extents = new_half_extents;

	destroy();
}

bool Box3DBoxShape3D::build(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_xform, bool p_body_is_static) const {
	if (half_extents.x <= 0.0f || half_extents.y <= 0.0f || half_extents.z <= 0.0f) {
		return true;
	}

	const b3BoxHull box = b3MakeBoxHull((float)half_extents.x, (float)half_extents.y, (float)half_extents.z);

	return _build_hull(p_instance, p_body, p_def, &box.base, p_xform);
}

bool Box3DBoxShape3D::make_query_shape(const Transform3D &p_xform, Box3DQueryShape &r_shape) const {
	if (half_extents.x <= 0.0f || half_extents.y <= 0.0f || half_extents.z <= 0.0f) {
		return false;
	}

	const b3BoxHull box = b3MakeBoxHull((float)half_extents.x, (float)half_extents.y, (float)half_extents.z);

	return _make_hull_query_shape(&box.base, p_xform, r_shape);
}

String Box3DBoxShape3D::to_string() const {
	return vformat("{half_extents=%v}", half_extents);
}

/* Capsule */

Variant Box3DCapsuleShape3D::get_data() const {
	Dictionary data;
	data["height"] = height;
	data["radius"] = radius;
	return data;
}

void Box3DCapsuleShape3D::set_data(const Variant &p_data) {
	ERR_FAIL_COND(p_data.get_type() != Variant::DICTIONARY);

	const Dictionary data = p_data;

	const Variant maybe_height = data.get("height", Variant());
	ERR_FAIL_COND(maybe_height.get_type() != Variant::FLOAT);

	const Variant maybe_radius = data.get("radius", Variant());
	ERR_FAIL_COND(maybe_radius.get_type() != Variant::FLOAT);

	const float new_height = maybe_height;
	const float new_radius = maybe_radius;

	if (unlikely(new_height == height && new_radius == radius)) {
		return;
	}

	height = new_height;
	radius = new_radius;

	destroy();
}

AABB Box3DCapsuleShape3D::get_aabb() const {
	const Vector3 half_extents(radius, height / 2.0f, radius);
	return AABB(-half_extents, half_extents * 2.0f);
}

bool Box3DCapsuleShape3D::build(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_xform, bool p_body_is_static) const {
	if (radius <= 0.0f) {
		return true;
	}

	const float half_segment = MAX(height * 0.5f - radius, 0.0f);
	const Vector3 top = p_xform.xform(Vector3(0, half_segment, 0));
	const Vector3 bottom = p_xform.xform(Vector3(0, -half_segment, 0));
	const float scaled_radius = MAX(radius * _uniform_scale(p_xform.basis, to_string()), 0.001f);

	if (top.distance_to(bottom) < 2.0f * B3_MIN_CAPSULE_LENGTH) {
		const b3Sphere sphere = { to_b3((top + bottom) * 0.5f), scaled_radius };
		return _register_shape(p_instance, b3CreateSphereShape(p_body, &p_def, &sphere));
	}

	const b3Capsule capsule = { to_b3(top), to_b3(bottom), scaled_radius };
	return _register_shape(p_instance, b3CreateCapsuleShape(p_body, &p_def, &capsule));
}

bool Box3DCapsuleShape3D::make_query_shape(const Transform3D &p_xform, Box3DQueryShape &r_shape) const {
	const float half_segment = MAX(height * 0.5f - radius, 0.0f);
	const float scaled_radius = MAX(radius * _uniform_scale(p_xform.basis, to_string()), 0.0f);

	r_shape.type = Box3DQueryShape::CAPSULE;
	r_shape.capsule = { to_b3(p_xform.xform(Vector3(0, half_segment, 0))), to_b3(p_xform.xform(Vector3(0, -half_segment, 0))), scaled_radius };
	return true;
}

String Box3DCapsuleShape3D::to_string() const {
	return vformat("{height=%f radius=%f}", height, radius);
}

/* Hull based shapes */

Box3DHullShape3D::~Box3DHullShape3D() {
	if (hull != nullptr) {
		b3DestroyHull(hull);
	}
}

b3HullData *Box3DHullShape3D::_get_hull() const {
	if (hull == nullptr) {
		hull = _create_hull();
	}

	return hull;
}

void Box3DHullShape3D::_reset_hull() {
	if (hull != nullptr) {
		b3DestroyHull(hull);
		hull = nullptr;
	}
}

bool Box3DHullShape3D::build(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_xform, bool p_body_is_static) const {
	const b3HullData *shape_hull = _get_hull();

	if (shape_hull == nullptr) {
		return true;
	}

	return _build_hull(p_instance, p_body, p_def, shape_hull, p_xform);
}

bool Box3DHullShape3D::make_query_shape(const Transform3D &p_xform, Box3DQueryShape &r_shape) const {
	const b3HullData *shape_hull = _get_hull();

	if (shape_hull == nullptr) {
		return false;
	}

	return _make_hull_query_shape(shape_hull, p_xform, r_shape);
}

/* Cylinder */

Variant Box3DCylinderShape3D::get_data() const {
	Dictionary data;
	data["height"] = height;
	data["radius"] = radius;
	return data;
}

void Box3DCylinderShape3D::set_data(const Variant &p_data) {
	ERR_FAIL_COND(p_data.get_type() != Variant::DICTIONARY);

	const Dictionary data = p_data;

	const Variant maybe_height = data.get("height", Variant());
	ERR_FAIL_COND(maybe_height.get_type() != Variant::FLOAT);

	const Variant maybe_radius = data.get("radius", Variant());
	ERR_FAIL_COND(maybe_radius.get_type() != Variant::FLOAT);

	const float new_height = maybe_height;
	const float new_radius = maybe_radius;

	if (unlikely(new_height == height && new_radius == radius)) {
		return;
	}

	height = new_height;
	radius = new_radius;

	_reset_hull();
	destroy();
}

b3HullData *Box3DCylinderShape3D::_create_hull() const {
	if (height <= 0.0f || radius <= 0.0f) {
		return nullptr;
	}

	const int sides = CLAMP(Box3DProjectSettings::cylinder_hull_sides, 3, 32);

	return b3CreateCylinder(height, radius, -height * 0.5f, sides);
}

AABB Box3DCylinderShape3D::get_aabb() const {
	const Vector3 half_extents(radius, height / 2.0f, radius);
	return AABB(-half_extents, half_extents * 2.0f);
}

String Box3DCylinderShape3D::to_string() const {
	return vformat("{height=%f radius=%f}", height, radius);
}

/* Convex polygon */

b3HullData *Box3DConvexPolygonShape3D::_create_hull() const {
	const int vertex_count = vertices.size();

	if (unlikely(vertex_count == 0)) {
		return nullptr;
	}

	ERR_FAIL_COND_V_MSG(vertex_count < 3, nullptr, vformat("Failed to build Box3D convex polygon shape with %s. It must have a vertex count of at least 3. This shape belongs to %s.", to_string(), _owners_to_string()));

	LocalVector<b3Vec3> points;
	points.resize(vertex_count);

	for (int i = 0; i < vertex_count; i++) {
		points[i] = to_b3(vertices[i]);
	}

	b3HullData *new_hull = b3CreateHull(points.ptr(), vertex_count, B3_MAX_HULL_VERTICES);

	if (new_hull != nullptr) {
		return new_hull;
	}

	// Box3D hulls need some volume, but Godot accepts flat (or even straight) point sets, such as a single quad. Give
	// those a small thickness instead.
	constexpr real_t half_thickness = 0.01;

	const Vector3 origin = vertices[0];

	Vector3 farthest = origin;
	real_t farthest_distance = 0.0;
	for (int i = 1; i < vertex_count; i++) {
		const real_t distance = vertices[i].distance_to(origin);
		if (distance > farthest_distance) {
			farthest_distance = distance;
			farthest = vertices[i];
		}
	}

	String failure = "the points do not form a valid hull, or the hull needs more than the maximum number of vertices, faces or edges that Box3D supports";

	if (farthest_distance > CMP_EPSILON) {
		const Vector3 axis = (farthest - origin) / farthest_distance;

		Vector3 third = origin;
		real_t third_distance = 0.0;
		for (int i = 1; i < vertex_count; i++) {
			const real_t distance = axis.cross(vertices[i] - origin).length();
			if (distance > third_distance) {
				third_distance = distance;
				third = vertices[i];
			}
		}

		LocalVector<b3Vec3> thick_points;
		bool thickened = false;

		if (third_distance > farthest_distance * 1e-4) {
			const Vector3 normal = axis.cross(third - origin).normalized();

			bool coplanar = true;
			for (int i = 0; i < vertex_count; i++) {
				if (Math::abs(normal.dot(vertices[i] - origin)) > farthest_distance * 1e-3) {
					coplanar = false;
					break;
				}
			}

			if (coplanar) {
				for (int i = 0; i < vertex_count; i++) {
					thick_points.push_back(to_b3(vertices[i] + normal * half_thickness));
					thick_points.push_back(to_b3(vertices[i] - normal * half_thickness));
				}
				thickened = true;
			}
		} else {
			const Vector3 helper = Math::abs(axis.x) < 0.9 ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
			const Vector3 side_a = axis.cross(helper).normalized() * half_thickness;
			const Vector3 side_b = axis.cross(side_a).normalized() * half_thickness;

			for (int i = 0; i < vertex_count; i++) {
				thick_points.push_back(to_b3(vertices[i] + side_a + side_b));
				thick_points.push_back(to_b3(vertices[i] + side_a - side_b));
				thick_points.push_back(to_b3(vertices[i] - side_a + side_b));
				thick_points.push_back(to_b3(vertices[i] - side_a - side_b));
			}
			thickened = true;
		}

		if (thickened) {
			new_hull = b3CreateHull(thick_points.ptr(), (int)thick_points.size(), B3_MAX_HULL_VERTICES);

			if (new_hull != nullptr) {
				BOX3D_UNSUPPORTED_KEYED("Flat convex polygon shape",
						"A ConvexPolygonShape3D with points that all lie in one plane (or on one line) was attached to a physics object.",
						vformat("Box3D hulls need some volume, so the shape was given a thickness of %f m on each side of its points.", (double)half_thickness),
						vformat("shape=%s owners=%s", to_string(), _owners_to_string()),
						vformat("convex_flat_%d", get_rid().get_id()));
				return new_hull;
			}

			failure = "the points are flat or collinear and the thickened hull could not be built either";
		}
	}

	BOX3D_UNSUPPORTED_KEYED("Convex polygon shape hull",
			"A ConvexPolygonShape3D was attached to a physics object.",
			vformat("b3CreateHull() failed: %s.", failure),
			vformat("shape=%s max_hull_vertices=%d max_hull_faces=%d max_hull_edges=%d owners=%s", to_string(), B3_MAX_HULL_VERTICES, B3_MAX_HULL_FACES, B3_MAX_HULL_EDGES, _owners_to_string()),
			vformat("convex_hull_%d", get_rid().get_id()));

	return nullptr;
}

void Box3DConvexPolygonShape3D::set_data(const Variant &p_data) {
	ERR_FAIL_COND(p_data.get_type() != Variant::PACKED_VECTOR3_ARRAY);

	vertices = p_data;

	aabb = AABB();
	for (int i = 0; i < vertices.size(); i++) {
		if (i == 0) {
			aabb.position = vertices[i];
		} else {
			aabb.expand_to(vertices[i]);
		}
	}

	_reset_hull();
	destroy();
}

String Box3DConvexPolygonShape3D::to_string() const {
	return vformat("{vertex_count=%d}", vertices.size());
}

/* Mesh based shapes */

Box3DMeshShape3D::~Box3DMeshShape3D() {
	if (shared_mesh != nullptr) {
		b3DestroyMesh(shared_mesh);
	}
}

void Box3DMeshShape3D::_reset_mesh() {
	b3MeshData *old_mesh = shared_mesh;
	shared_mesh = nullptr;

	// The owners have to rebuild their shapes before the old mesh goes away, as Box3D shapes only reference it.
	destroy();

	if (old_mesh != nullptr) {
		b3DestroyMesh(old_mesh);
	}
}

bool Box3DMeshShape3D::build(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_xform, bool p_body_is_static) const {
	if (!p_body_is_static) {
		BOX3D_UNSUPPORTED_KEYED("Triangle mesh collision on a moving body",
				"A concave (trimesh) or heightmap collision shape was attached to a kinematic or rigid body.",
				"Box3D only generates contacts for triangle meshes and height fields on static bodies.",
				vformat("shape=%s type=%s owners=%s", to_string(), get_type() == PhysicsServer3D::SHAPE_HEIGHTMAP ? "heightmap" : "concave_polygon", _owners_to_string()),
				"mesh_on_moving_body");
	}

	b3MeshData *mesh = nullptr;
	Vector3 mesh_scale(1, 1, 1);

	if (p_xform.is_equal_approx(Transform3D())) {
		if (shared_mesh == nullptr) {
			shared_mesh = _create_mesh(Transform3D());
		}

		mesh = shared_mesh;
	} else {
		mesh = _create_mesh(p_xform);
	}

	if (mesh == nullptr) {
		return true;
	}

	const bool is_shared = mesh == shared_mesh;
	const b3ShapeId id = b3CreateMeshShape(p_body, &p_def, mesh, to_b3(mesh_scale));

	if (!is_shared) {
		p_instance.owned_data.push_back({ Box3DOwnedData::MESH, mesh });
	}

	return _register_shape(p_instance, id);
}

/* Concave polygon */

b3MeshData *Box3DConcavePolygonShape3D::_create_mesh(const Transform3D &p_xform) const {
	const int vertex_count = faces.size();

	if (unlikely(vertex_count == 0)) {
		return nullptr;
	}

	ERR_FAIL_COND_V_MSG(vertex_count < 3, nullptr, vformat("Failed to build Box3D concave polygon shape with %s. It must have a vertex count of at least 3. This shape belongs to %s.", to_string(), _owners_to_string()));
	ERR_FAIL_COND_V_MSG(vertex_count % 3 != 0, nullptr, vformat("Failed to build Box3D concave polygon shape with %s. It must have a vertex count that is divisible by 3. This shape belongs to %s.", to_string(), _owners_to_string()));

	LocalVector<b3Vec3> vertices;
	LocalVector<int32_t> indices;
	vertices.resize(vertex_count);
	indices.resize(vertex_count);

	for (int i = 0; i < vertex_count; i++) {
		vertices[i] = to_b3(p_xform.xform(faces[i]));
		indices[i] = i;
	}

	b3MeshDef def = {};
	def.vertices = vertices.ptr();
	def.indices = indices.ptr();
	def.vertexCount = vertex_count;
	def.triangleCount = vertex_count / 3;
	def.weldVertices = true;
	def.weldTolerance = 0.0001f;
	def.identifyEdges = true;
	// Godot's front faces wind clockwise, and mirroring flips that.
	def.clockWiseWinding = p_xform.basis.determinant() >= 0.0f;

	b3MeshData *mesh = b3CreateMesh(&def, nullptr, 0);

	if (mesh == nullptr) {
		BOX3D_UNSUPPORTED_KEYED("Concave polygon shape mesh",
				"A ConcavePolygonShape3D was attached to a physics object.",
				"b3CreateMesh() failed. The triangles are probably all degenerate.",
				vformat("shape=%s owners=%s", to_string(), _owners_to_string()),
				vformat("concave_mesh_%d", get_rid().get_id()));
	}

	return mesh;
}

Variant Box3DConcavePolygonShape3D::get_data() const {
	Dictionary data;
	data["faces"] = faces;
	data["backface_collision"] = back_face_collision;
	return data;
}

void Box3DConcavePolygonShape3D::set_data(const Variant &p_data) {
	ERR_FAIL_COND(p_data.get_type() != Variant::DICTIONARY);

	const Dictionary data = p_data;

	const Variant maybe_faces = data.get("faces", Variant());
	ERR_FAIL_COND(maybe_faces.get_type() != Variant::PACKED_VECTOR3_ARRAY);

	const Variant maybe_back_face_collision = data.get("backface_collision", Variant());
	ERR_FAIL_COND(maybe_back_face_collision.get_type() != Variant::BOOL);

	faces = maybe_faces;
	back_face_collision = maybe_back_face_collision;

	aabb = AABB();
	for (int i = 0; i < faces.size(); i++) {
		if (i == 0) {
			aabb.position = faces[i];
		} else {
			aabb.expand_to(faces[i]);
		}
	}

	if (back_face_collision) {
		BOX3D_UNSUPPORTED_KEYED("Back-face collision on concave polygon shapes",
				"A ConcavePolygonShape3D was created with backface_collision enabled.",
				"Box3D triangle meshes are one-sided and have no way to collide with the back of a triangle. Only the front faces will collide.",
				vformat("shape=%s", to_string()),
				"concave_backface_collision");
	}

	_reset_mesh();
}

String Box3DConcavePolygonShape3D::to_string() const {
	return vformat("{vertex_count=%d}", faces.size());
}

/* Height map */

AABB Box3DHeightMapShape3D::_calculate_aabb() const {
	AABB result;

	const float offset_x = (float)-(width - 1) / 2.0f;
	const float offset_z = (float)-(depth - 1) / 2.0f;

	bool first = true;

	for (int z = 0; z < depth; ++z) {
		for (int x = 0; x < width; ++x) {
			const real_t height = heights[z * width + x];
			if (Math::is_nan(height)) {
				continue;
			}

			const Vector3 vertex(offset_x + (float)x, (float)height, offset_z + (float)z);

			if (first) {
				result.position = vertex;
				first = false;
			} else {
				result.expand_to(vertex);
			}
		}
	}

	return result;
}

b3MeshData *Box3DHeightMapShape3D::_create_mesh(const Transform3D &p_xform) const {
	const int height_count = heights.size();
	if (unlikely(height_count == 0)) {
		return nullptr;
	}

	ERR_FAIL_COND_V_MSG(height_count != width * depth, nullptr, vformat("Failed to build Box3D height map shape with %s. Height count must be the product of width and depth. This shape belongs to %s.", to_string(), _owners_to_string()));
	ERR_FAIL_COND_V_MSG(width < 2 || depth < 2, nullptr, vformat("Failed to build Box3D height map shape with %s. The height map must be at least 2x2. This shape belongs to %s.", to_string(), _owners_to_string()));

	const int quad_count_x = width - 1;
	const int quad_count_z = depth - 1;

	const float offset_x = (float)-quad_count_x / 2.0f;
	const float offset_z = (float)-quad_count_z / 2.0f;

	LocalVector<b3Vec3> vertices;
	vertices.resize(height_count);

	for (int z = 0; z < depth; ++z) {
		for (int x = 0; x < width; ++x) {
			const real_t height = heights[z * width + x];
			// Godot has undocumented support for holes by passing NaN as the height value.
			const Vector3 vertex(offset_x + (float)x, Math::is_nan(height) ? 0.0f : (float)height, offset_z + (float)z);
			vertices[z * width + x] = to_b3(p_xform.xform(vertex));
		}
	}

	LocalVector<int32_t> indices;
	indices.reserve(quad_count_x * quad_count_z * 6);

	for (int z = 0; z < quad_count_z; ++z) {
		for (int x = 0; x < quad_count_x; ++x) {
			const int lower_right = z * width + x;
			const int lower_left = z * width + (x + 1);
			const int upper_right = (z + 1) * width + x;
			const int upper_left = (z + 1) * width + (x + 1);

			const bool has_hole = Math::is_nan(heights[lower_right]) || Math::is_nan(heights[lower_left]) || Math::is_nan(heights[upper_right]) || Math::is_nan(heights[upper_left]);
			if (has_hole) {
				continue;
			}

			indices.push_back(lower_right);
			indices.push_back(upper_right);
			indices.push_back(lower_left);

			indices.push_back(lower_left);
			indices.push_back(upper_right);
			indices.push_back(upper_left);
		}
	}

	if (indices.is_empty()) {
		return nullptr;
	}

	b3MeshDef def = {};
	def.vertices = vertices.ptr();
	def.indices = indices.ptr();
	def.vertexCount = height_count;
	def.triangleCount = (int)indices.size() / 3;
	def.weldVertices = false;
	def.useMedianSplit = true;
	def.identifyEdges = true;
	def.clockWiseWinding = p_xform.basis.determinant() < 0.0f;

	b3MeshData *mesh = b3CreateMesh(&def, nullptr, 0);

	if (mesh == nullptr) {
		BOX3D_UNSUPPORTED_KEYED("Height map shape mesh",
				"A HeightMapShape3D was attached to a physics object.",
				"b3CreateMesh() failed for the triangulated height map.",
				vformat("shape=%s owners=%s", to_string(), _owners_to_string()),
				vformat("heightmap_mesh_%d", get_rid().get_id()));
	}

	return mesh;
}

Variant Box3DHeightMapShape3D::get_data() const {
	Dictionary data;
	data["width"] = width;
	data["depth"] = depth;
	data["heights"] = heights;
	return data;
}

void Box3DHeightMapShape3D::set_data(const Variant &p_data) {
	ERR_FAIL_COND(p_data.get_type() != Variant::DICTIONARY);

	const Dictionary data = p_data;

	const Variant maybe_heights = data.get("heights", Variant());

#ifdef REAL_T_IS_DOUBLE
	ERR_FAIL_COND(maybe_heights.get_type() != Variant::PACKED_FLOAT64_ARRAY);
#else
	ERR_FAIL_COND(maybe_heights.get_type() != Variant::PACKED_FLOAT32_ARRAY);
#endif

	const Variant maybe_width = data.get("width", Variant());
	ERR_FAIL_COND(maybe_width.get_type() != Variant::INT);

	const Variant maybe_depth = data.get("depth", Variant());
	ERR_FAIL_COND(maybe_depth.get_type() != Variant::INT);

	heights = maybe_heights;
	width = maybe_width;
	depth = maybe_depth;

	if (heights.size() == width * depth) {
		aabb = _calculate_aabb();
	} else {
		aabb = AABB();
	}

	_reset_mesh();
}

String Box3DHeightMapShape3D::to_string() const {
	return vformat("{height_count=%d width=%d depth=%d}", heights.size(), width, depth);
}
