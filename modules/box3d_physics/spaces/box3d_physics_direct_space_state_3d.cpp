/**************************************************************************/
/*  box3d_physics_direct_space_state_3d.cpp                               */
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

#include "box3d_physics_direct_space_state_3d.h"

#include "../box3d_physics_server_3d.h"
#include "../box3d_project_settings.h"
#include "../misc/box3d_diagnostics.h"
#include "../objects/box3d_area_3d.h"
#include "../objects/box3d_body_3d.h"
#include "../shapes/box3d_shape_3d.h"
#include "box3d_query_shape.h"
#include "box3d_space_3d.h"

namespace {

// Decides which objects take part in a query.
struct QueryFilter {
	const HashSet<RID> *exclude_rids = nullptr;
	const HashSet<ObjectID> *exclude_objects = nullptr;

	// The body doing a motion query, if any. It never collides with itself, and its mask and exceptions apply.
	const Box3DBody3D *self = nullptr;

	bool bodies = true;
	bool areas = false;
	bool pickable_only = false;

	bool accept(const Box3DObject3D *p_object) const {
		if (p_object == self && self != nullptr) {
			return false;
		}

		if (exclude_rids != nullptr && exclude_rids->has(p_object->get_rid())) {
			return false;
		}

		if (exclude_objects != nullptr && exclude_objects->has(p_object->get_instance_id())) {
			return false;
		}

		if (pickable_only && !p_object->is_pickable()) {
			return false;
		}

		if (p_object->is_body()) {
			if (!bodies) {
				return false;
			}

			if (self != nullptr) {
				return self->can_collide_with(*p_object) && !self->has_collision_exception(p_object->get_rid());
			}

			return true;
		}

		return areas && self == nullptr;
	}
};

const Box3DShapeInstance *_get_instance(b3ShapeId p_shape) {
	return (const Box3DShapeInstance *)b3Shape_GetUserData(p_shape);
}

struct GatherContext {
	const QueryFilter *filter = nullptr;
	LocalVector<b3ShapeId> *shapes = nullptr;
};

bool _gather_callback(b3ShapeId p_shape, void *p_context) {
	GatherContext &context = *(GatherContext *)p_context;

	const Box3DShapeInstance *instance = _get_instance(p_shape);
	if (instance != nullptr && context.filter->accept(instance->get_owner())) {
		context.shapes->push_back(p_shape);
	}

	return true;
}

// Collects all shapes that may touch the world space box `p_bounds` (relative to `p_origin`).
void _gather_shapes(b3WorldId p_world, const b3Pos &p_origin, const b3AABB &p_bounds, uint32_t p_mask, const QueryFilter &p_filter, LocalVector<b3ShapeId> &r_shapes) {
	b3AABB world_bounds = p_bounds;
	const b3Vec3 offset = { (float)p_origin.x, (float)p_origin.y, (float)p_origin.z };
	world_bounds.lowerBound = b3Add(world_bounds.lowerBound, offset);
	world_bounds.upperBound = b3Add(world_bounds.upperBound, offset);

	GatherContext context;
	context.filter = &p_filter;
	context.shapes = &r_shapes;

	b3World_OverlapAABB(p_world, world_bounds, Box3DFilter::make_query(p_mask), _gather_callback, &context);
}

struct RayContext {
	const QueryFilter *filter = nullptr;
	bool hit_from_inside = false;

	bool hit = false;
	float fraction = 2.0f;
	b3ShapeId shape = b3_nullShapeId;
	b3Pos point = b3Pos_zero;
	b3Vec3 normal = b3Vec3_zero;
	int triangle_index = -1;
};

float _ray_callback(b3ShapeId p_shape, b3Pos p_point, b3Vec3 p_normal, float p_fraction, uint64_t p_material, int p_triangle, int p_child, void *p_context) {
	RayContext &context = *(RayContext *)p_context;

	const Box3DShapeInstance *instance = _get_instance(p_shape);
	if (instance == nullptr || !context.filter->accept(instance->get_owner())) {
		return -1.0f;
	}

	if (p_fraction < context.fraction) {
		context.hit = true;
		context.fraction = p_fraction;
		context.shape = p_shape;
		context.point = p_point;
		context.normal = p_normal;
		context.triangle_index = p_triangle;
	}

	return p_fraction;
}

struct OverlapContext {
	const QueryFilter *filter = nullptr;
	PhysicsDirectSpaceState3D::ShapeResult *results = nullptr;
	int max = 0;
	int count = 0;
};

bool _overlap_callback(b3ShapeId p_shape, void *p_context) {
	OverlapContext &context = *(OverlapContext *)p_context;

	const Box3DShapeInstance *instance = _get_instance(p_shape);
	if (instance == nullptr) {
		return true;
	}

	const Box3DObject3D *object = instance->get_owner();
	if (!context.filter->accept(object)) {
		return true;
	}

	PhysicsDirectSpaceState3D::ShapeResult &result = context.results[context.count++];
	result.shape = MAX(object->find_shape_index(instance->get_id()), 0);
	result.rid = object->get_rid();
	result.collider_id = object->get_instance_id();
	result.collider = object->get_instance();

	return context.count < context.max;
}

struct CastContext {
	const QueryFilter *filter = nullptr;
	float fraction = 1.0f;
	bool hit = false;
};

float _cast_callback(b3ShapeId p_shape, b3Pos p_point, b3Vec3 p_normal, float p_fraction, uint64_t p_material, int p_triangle, int p_child, void *p_context) {
	CastContext &context = *(CastContext *)p_context;

	const Box3DShapeInstance *instance = _get_instance(p_shape);
	if (instance == nullptr || !context.filter->accept(instance->get_owner())) {
		return -1.0f;
	}

	context.hit = true;
	context.fraction = MIN(context.fraction, p_fraction);

	return p_fraction;
}

// Sets up a query shape for `p_shape` placed at `p_transform`, keeping the translation of the transform out of the shape
// so that it can be used as the origin of the query.
bool _make_query_shape(const Box3DShape3D *p_shape, const Transform3D &p_transform, Box3DQueryShape &r_query) {
	if (!p_shape->is_convex()) {
		BOX3D_UNSUPPORTED_KEYED("Querying with a non-convex shape",
				"A shape query (intersect_shape, cast_motion, collide_shape, get_rest_info or a ShapeCast3D) was made with a concave, height map or world boundary shape.",
				"Only convex shapes (sphere, box, capsule, cylinder and convex polygon) can be swept or collided against the world by Box3D.",
				vformat("shape=%s", p_shape->to_string()),
				"query_non_convex_shape");
		return false;
	}

	Transform3D relative(p_transform.basis, Vector3());

	if (!p_shape->make_query_shape(relative, r_query)) {
		return false;
	}

	return true;
}

Transform3D _remove_scale(const Transform3D &p_transform, Vector3 &r_scale) {
	Transform3D result = p_transform;
	if (unlikely(!Box3DMath::decompose(result.basis, r_scale))) {
		WARN_PRINT("An invalid transform was passed to a physics query. The basis was singular, which is not supported by Box3D. It will be treated as identity.");
	}
	return result;
}

// The ray of a separation ray shape in the space of the body, if `p_instance` is one.
bool _get_separation_ray(const Box3DShapeInstance &p_instance, const Vector3 &p_scale, Vector3 &r_from, Vector3 &r_direction, float &r_length, bool &r_slide_on_slope) {
	const Box3DShape3D *shape = p_instance.get_shape();
	if (shape->get_type() != PhysicsServer3D::SHAPE_SEPARATION_RAY) {
		return false;
	}

	const Box3DSeparationRayShape3D *ray_shape = static_cast<const Box3DSeparationRayShape3D *>(shape);
	const Transform3D transform = p_instance.get_transform_scaled(p_scale);

	r_from = transform.origin;
	r_direction = transform.basis.get_column(2).normalized();
	r_length = ray_shape->get_length() * (float)transform.basis.get_column(2).length();
	r_slide_on_slope = ray_shape->get_slide_on_slope();
	return true;
}

} // namespace

Box3DPhysicsDirectSpaceState3D::Box3DPhysicsDirectSpaceState3D(Box3DSpace3D *p_space) :
		space(p_space) {
}

bool Box3DPhysicsDirectSpaceState3D::intersect_ray(const RayParameters &p_parameters, RayResult &r_result) {
	ERR_FAIL_COND_V_MSG(space->is_stepping(), false, "intersect_ray must not be called while the physics space is being stepped.");

	space->flush_pending_shapes();

	QueryFilter filter;
	filter.exclude_rids = &p_parameters.exclude;
	filter.bodies = p_parameters.collide_with_bodies;
	filter.areas = p_parameters.collide_with_areas;
	filter.pickable_only = p_parameters.pick_ray;

	const Vector3 vector = p_parameters.to - p_parameters.from;
	const b3Pos origin = to_b3_pos(p_parameters.from);
	const b3QueryFilter query_filter = Box3DFilter::make_query(p_parameters.collision_mask);

	if (p_parameters.hit_from_inside) {
		// A ray that starts inside a shape hits it right away, without a meaningful normal.
		ShapeResult inside_result;
		OverlapContext overlap_context;
		overlap_context.filter = &filter;
		overlap_context.results = &inside_result;
		overlap_context.max = 1;

		const b3Vec3 point = b3Vec3_zero;
		const b3ShapeProxy proxy = { &point, 1, 0.0f };
		b3World_OverlapShape(space->get_world(), origin, &proxy, query_filter, _overlap_callback, &overlap_context);

		if (overlap_context.count > 0) {
			r_result.position = p_parameters.from;
			r_result.normal = Vector3();
			r_result.rid = inside_result.rid;
			r_result.collider_id = inside_result.collider_id;
			r_result.collider = inside_result.collider;
			r_result.shape = inside_result.shape;
			r_result.face_index = -1;
			return true;
		}
	}

	RayContext context;
	context.filter = &filter;
	context.hit_from_inside = p_parameters.hit_from_inside;

	b3World_CastRay(space->get_world(), origin, to_b3(vector), query_filter, _ray_callback, &context);

	if (!context.hit) {
		return false;
	}

	const Box3DShapeInstance *instance = _get_instance(context.shape);
	ERR_FAIL_NULL_V(instance, false);

	const Box3DObject3D *object = instance->get_owner();

	Vector3 normal = to_godot(context.normal);

	// If we got a back-face normal we need to flip it.
	if (normal.dot(vector) > 0) {
		normal = -normal;
	}

	r_result.position = pos_to_godot(context.point);
	r_result.normal = normal;
	r_result.rid = object->get_rid();
	r_result.collider_id = object->get_instance_id();
	r_result.collider = object->get_instance();
	r_result.shape = MAX(object->find_shape_index(instance->get_id()), 0);
	r_result.face_index = b3Shape_GetType(context.shape) == b3_meshShape ? context.triangle_index : -1;

	return true;
}

int Box3DPhysicsDirectSpaceState3D::intersect_point(const PointParameters &p_parameters, ShapeResult *r_results, int p_result_max) {
	ERR_FAIL_COND_V_MSG(space->is_stepping(), 0, "intersect_point must not be called while the physics space is being stepped.");

	if (p_result_max == 0) {
		return 0;
	}

	space->flush_pending_shapes();

	QueryFilter filter;
	filter.exclude_rids = &p_parameters.exclude;
	filter.bodies = p_parameters.collide_with_bodies;
	filter.areas = p_parameters.collide_with_areas;

	OverlapContext context;
	context.filter = &filter;
	context.results = r_results;
	context.max = p_result_max;

	const b3Vec3 point = b3Vec3_zero;
	const b3ShapeProxy proxy = { &point, 1, 0.0f };
	b3World_OverlapShape(space->get_world(), to_b3_pos(p_parameters.position), &proxy, Box3DFilter::make_query(p_parameters.collision_mask), _overlap_callback, &context);

	return context.count;
}

int Box3DPhysicsDirectSpaceState3D::intersect_shape(const ShapeParameters &p_parameters, ShapeResult *r_results, int p_result_max) {
	ERR_FAIL_COND_V_MSG(space->is_stepping(), 0, "intersect_shape must not be called while the physics space is being stepped.");

	if (p_result_max == 0) {
		return 0;
	}

	space->flush_pending_shapes();

	const Box3DShape3D *shape = Box3DPhysicsServer3D::get_singleton()->get_shape(p_parameters.shape_rid);
	ERR_FAIL_NULL_V(shape, 0);

	Box3DQueryShape query;
	if (!_make_query_shape(shape, p_parameters.transform, query)) {
		return 0;
	}

	LocalVector<b3Vec3> points;
	float radius = 0.0f;
	query.make_proxy(points, radius);

	const b3ShapeProxy proxy = { points.ptr(), (int)points.size(), radius + MAX((float)p_parameters.margin, 0.0f) };

	QueryFilter filter;
	filter.exclude_rids = &p_parameters.exclude;
	filter.bodies = p_parameters.collide_with_bodies;
	filter.areas = p_parameters.collide_with_areas;

	OverlapContext context;
	context.filter = &filter;
	context.results = r_results;
	context.max = p_result_max;

	b3World_OverlapShape(space->get_world(), to_b3_pos(p_parameters.transform.origin), &proxy, Box3DFilter::make_query(p_parameters.collision_mask), _overlap_callback, &context);

	return context.count;
}

bool Box3DPhysicsDirectSpaceState3D::cast_motion(const ShapeParameters &p_parameters, real_t &r_closest_safe, real_t &r_closest_unsafe, ShapeRestInfo *r_info) {
	r_closest_safe = 1.0;
	r_closest_unsafe = 1.0;

	ERR_FAIL_COND_V_MSG(space->is_stepping(), false, "cast_motion must not be called while the physics space is being stepped.");

	if (r_info != nullptr) {
		BOX3D_UNSUPPORTED_KEYED("Rest info as part of cast_motion",
				"PhysicsDirectSpaceState3D.cast_motion() was called in a way that asks for rest info.",
				"Box3D reports where a cast first touches something, but does not compute the contact details of the resting position in the same query.",
				vformat("space=%s shape=%s motion=%v", space->get_rid(), p_parameters.shape_rid, p_parameters.motion),
				"cast_motion_rest_info");
		return false;
	}

	space->flush_pending_shapes();

	const Box3DShape3D *shape = Box3DPhysicsServer3D::get_singleton()->get_shape(p_parameters.shape_rid);
	ERR_FAIL_NULL_V(shape, false);

	const float motion_length = (float)p_parameters.motion.length();
	if (motion_length == 0.0f) {
		return true;
	}

	Box3DQueryShape query;
	if (!_make_query_shape(shape, p_parameters.transform, query)) {
		return false;
	}

	LocalVector<b3Vec3> points;
	float radius = 0.0f;
	query.make_proxy(points, radius);

	const b3ShapeProxy proxy = { points.ptr(), (int)points.size(), radius + MAX((float)p_parameters.margin, 0.0f) };

	QueryFilter filter;
	filter.exclude_rids = &p_parameters.exclude;
	filter.bodies = p_parameters.collide_with_bodies;
	filter.areas = p_parameters.collide_with_areas;

	CastContext context;
	context.filter = &filter;

	b3World_CastShape(space->get_world(), to_b3_pos(p_parameters.transform.origin), &proxy, to_b3(p_parameters.motion), Box3DFilter::make_query(p_parameters.collision_mask), _cast_callback, &context);

	if (context.hit) {
		// Back off a little from the point of contact so that the safe position does not overlap anything.
		constexpr float safety_distance = 0.0005f;
		r_closest_unsafe = context.fraction;
		r_closest_safe = MAX(context.fraction - safety_distance / motion_length, 0.0f);
	}

	return true;
}

bool Box3DPhysicsDirectSpaceState3D::collide_shape(const ShapeParameters &p_parameters, Vector3 *r_results, int p_result_max, int &r_result_count) {
	r_result_count = 0;

	ERR_FAIL_COND_V_MSG(space->is_stepping(), false, "collide_shape must not be called while the physics space is being stepped.");

	if (p_result_max == 0) {
		return false;
	}

	space->flush_pending_shapes();

	const Box3DShape3D *shape = Box3DPhysicsServer3D::get_singleton()->get_shape(p_parameters.shape_rid);
	ERR_FAIL_NULL_V(shape, false);

	Box3DQueryShape query;
	if (!_make_query_shape(shape, p_parameters.transform, query)) {
		return false;
	}

	const float margin = MAX((float)p_parameters.margin, 0.0f);
	const b3Pos origin = to_b3_pos(p_parameters.transform.origin);

	QueryFilter filter;
	filter.exclude_rids = &p_parameters.exclude;
	filter.bodies = p_parameters.collide_with_bodies;
	filter.areas = p_parameters.collide_with_areas;

	b3AABB bounds = query.compute_aabb();
	bounds.lowerBound = b3Sub(bounds.lowerBound, b3Vec3{ margin, margin, margin });
	bounds.upperBound = b3Add(bounds.upperBound, b3Vec3{ margin, margin, margin });

	LocalVector<b3ShapeId> candidates;
	_gather_shapes(space->get_world(), origin, bounds, p_parameters.collision_mask, filter, candidates);

	const int max_points = p_result_max * 2;
	int point_count = 0;

	LocalVector<Box3DContact> contacts;

	for (const b3ShapeId &candidate : candidates) {
		contacts.clear();
		Box3DNarrowPhase::collide(query, origin, candidate, margin, contacts);

		for (const Box3DContact &contact : contacts) {
			const Vector3 margin_offset = -contact.normal * margin;

			r_results[point_count++] = p_parameters.transform.origin + contact.point_on_query + margin_offset;
			r_results[point_count++] = p_parameters.transform.origin + contact.point_on_target;

			if (point_count >= max_points) {
				break;
			}
		}

		if (point_count >= max_points) {
			break;
		}
	}

	r_result_count = point_count / 2;

	return r_result_count > 0;
}

bool Box3DPhysicsDirectSpaceState3D::rest_info(const ShapeParameters &p_parameters, ShapeRestInfo *r_info) {
	ERR_FAIL_COND_V_MSG(space->is_stepping(), false, "get_rest_info must not be called while the physics space is being stepped.");

	space->flush_pending_shapes();

	const Box3DShape3D *shape = Box3DPhysicsServer3D::get_singleton()->get_shape(p_parameters.shape_rid);
	ERR_FAIL_NULL_V(shape, false);

	Box3DQueryShape query;
	if (!_make_query_shape(shape, p_parameters.transform, query)) {
		return false;
	}

	const float margin = MAX((float)p_parameters.margin, 0.0f);
	const b3Pos origin = to_b3_pos(p_parameters.transform.origin);

	QueryFilter filter;
	filter.exclude_rids = &p_parameters.exclude;
	filter.bodies = p_parameters.collide_with_bodies;
	filter.areas = p_parameters.collide_with_areas;

	b3AABB bounds = query.compute_aabb();
	bounds.lowerBound = b3Sub(bounds.lowerBound, b3Vec3{ margin, margin, margin });
	bounds.upperBound = b3Add(bounds.upperBound, b3Vec3{ margin, margin, margin });

	LocalVector<b3ShapeId> candidates;
	_gather_shapes(space->get_world(), origin, bounds, p_parameters.collision_mask, filter, candidates);

	LocalVector<Box3DContact> contacts;
	const Box3DContact *deepest = nullptr;
	b3ShapeId deepest_shape = b3_nullShapeId;
	LocalVector<Box3DContact> best;

	for (const b3ShapeId &candidate : candidates) {
		contacts.clear();
		Box3DNarrowPhase::collide(query, origin, candidate, margin, contacts);

		for (const Box3DContact &contact : contacts) {
			if (best.is_empty() || contact.depth > best[0].depth) {
				best.clear();
				best.push_back(contact);
				deepest_shape = candidate;
			}
		}
	}

	if (best.is_empty()) {
		return false;
	}

	deepest = &best[0];

	const Box3DShapeInstance *instance = _get_instance(deepest_shape);
	ERR_FAIL_NULL_V(instance, false);
	const Box3DObject3D *object = instance->get_owner();

	const Vector3 hit_point = p_parameters.transform.origin + deepest->point_on_target;

	r_info->point = hit_point;
	r_info->normal = deepest->normal;
	r_info->rid = object->get_rid();
	r_info->collider_id = object->get_instance_id();
	r_info->shape = MAX(object->find_shape_index(instance->get_id()), 0);
	r_info->linear_velocity = object->get_velocity_at_position(hit_point);

	return true;
}

Vector3 Box3DPhysicsDirectSpaceState3D::get_closest_point_to_object_volume(RID p_object, Vector3 p_point) const {
	ERR_FAIL_COND_V_MSG(space->is_stepping(), Vector3(), "get_closest_point_to_object_volume must not be called while the physics space is being stepped.");

	space->flush_pending_shapes();

	Box3DPhysicsServer3D *physics_server = Box3DPhysicsServer3D::get_singleton();
	Box3DObject3D *object = physics_server->get_area(p_object);

	if (object == nullptr) {
		object = physics_server->get_body(p_object);
	}

	ERR_FAIL_NULL_V(object, Vector3());
	ERR_FAIL_COND_V(object->get_space() != space, Vector3());

	float closest_distance_sq = FLT_MAX;
	Vector3 closest_point = object->get_position();
	bool found_point = false;

	for (const Box3DShapeInstance *instance : object->get_shape_instances()) {
		for (const b3ShapeId &id : instance->built_shapes) {
			const b3Vec3 closest = b3Shape_GetClosestPoint(id, to_b3(p_point));
			const Vector3 candidate = to_godot(closest);
			const float candidate_distance_sq = (float)candidate.distance_squared_to(p_point);

			if (candidate_distance_sq < closest_distance_sq) {
				closest_distance_sq = candidate_distance_sq;
				closest_point = candidate;
				found_point = true;
			}
		}
	}

	if (found_point && closest_distance_sq < CMP_EPSILON2) {
		return p_point;
	}

	return closest_point;
}

bool Box3DPhysicsDirectSpaceState3D::_body_motion_recover(const Box3DBody3D &p_body, const Transform3D &p_transform, float p_margin, const HashSet<RID> &p_excluded_bodies, const HashSet<ObjectID> &p_excluded_objects, Vector3 &r_recovery) const {
	QueryFilter filter;
	filter.exclude_rids = &p_excluded_bodies;
	filter.exclude_objects = &p_excluded_objects;
	filter.self = &p_body;

	Transform3D transform = p_transform;
	const Vector3 scale = p_body.get_scale();

	bool recovered = false;

	LocalVector<Box3DContact> shape_contacts;

	struct ContactInfo {
		Box3DContact contact;
		float priority = 1.0f;
	};

	for (int i = 0; i < Box3DProjectSettings::motion_query_recovery_iterations; ++i) {
		LocalVector<ContactInfo> contacts;

		const b3Pos origin = to_b3_pos(transform.origin);
		const Transform3D body_rotation(transform.basis, Vector3());

		for (const Box3DShapeInstance *instance : p_body.get_shape_instances()) {
			if (instance->is_disabled() || !instance->get_shape()->is_convex() || instance->get_shape()->get_type() == PhysicsServer3D::SHAPE_SEPARATION_RAY) {
				continue;
			}

			Box3DQueryShape query;
			if (!instance->get_shape()->make_query_shape(body_rotation * instance->get_transform_scaled(scale), query)) {
				continue;
			}

			b3AABB bounds = query.compute_aabb();
			bounds.lowerBound = b3Sub(bounds.lowerBound, b3Vec3{ p_margin, p_margin, p_margin });
			bounds.upperBound = b3Add(bounds.upperBound, b3Vec3{ p_margin, p_margin, p_margin });

			LocalVector<b3ShapeId> candidates;
			_gather_shapes(space->get_world(), origin, bounds, p_body.get_collision_mask(), filter, candidates);

			for (const b3ShapeId &candidate : candidates) {
				const Box3DShapeInstance *other_instance = _get_instance(candidate);
				const Box3DBody3D *other_body = other_instance != nullptr ? other_instance->get_owner()->as_body() : nullptr;
				if (other_body == nullptr) {
					continue;
				}

				shape_contacts.clear();
				Box3DNarrowPhase::collide(query, origin, candidate, p_margin, shape_contacts);

				for (const Box3DContact &contact : shape_contacts) {
					ContactInfo info;
					info.contact = contact;
					info.priority = other_body->get_collision_priority();
					contacts.push_back(info);
				}
			}
		}

		if (contacts.is_empty()) {
			break;
		}

		float combined_priority = 0.0f;
		for (const ContactInfo &info : contacts) {
			combined_priority += info.priority;
		}

		const float average_priority = MAX(combined_priority / (float)contacts.size(), (float)CMP_EPSILON);

		recovered = true;

		Vector3 recovery;

		for (const ContactInfo &info : contacts) {
			const Box3DContact &contact = info.contact;

			// The axis points from the moving body into the thing it is touching.
			const Vector3 penetration_axis = -contact.normal;
			const Vector3 margin_offset = penetration_axis * p_margin;

			const Vector3 point_on_1 = contact.point_on_query + margin_offset;
			const Vector3 point_on_2 = contact.point_on_target;

			const real_t distance_to_1 = penetration_axis.dot(point_on_1 + recovery);
			const real_t distance_to_2 = penetration_axis.dot(point_on_2);

			const float penetration_depth = float(distance_to_1 - distance_to_2);

			if (penetration_depth <= 0.0f) {
				continue;
			}

			const float recovery_distance = penetration_depth * Box3DProjectSettings::motion_query_recovery_amount;
			const float other_priority_normalized = info.priority / average_priority;
			const float scaled_recovery_distance = recovery_distance * other_priority_normalized;

			recovery -= penetration_axis * scaled_recovery_distance;
		}

		if (recovery == Vector3()) {
			break;
		}

		r_recovery += recovery;
		transform.origin += recovery;
	}

	return recovered;
}

bool Box3DPhysicsDirectSpaceState3D::_body_motion_cast(const Box3DBody3D &p_body, const Transform3D &p_transform, const Vector3 &p_motion, bool p_collide_separation_ray, const HashSet<RID> &p_excluded_bodies, const HashSet<ObjectID> &p_excluded_objects, real_t &r_safe_fraction, real_t &r_unsafe_fraction) const {
	const float motion_length = (float)p_motion.length();
	if (motion_length == 0.0f) {
		return false;
	}

	QueryFilter filter;
	filter.exclude_rids = &p_excluded_bodies;
	filter.exclude_objects = &p_excluded_objects;
	filter.self = &p_body;

	const Vector3 scale = p_body.get_scale();
	const b3Pos origin = to_b3_pos(p_transform.origin);
	const Transform3D body_rotation(p_transform.basis, Vector3());

	bool collided = false;

	for (const Box3DShapeInstance *instance : p_body.get_shape_instances()) {
		if (instance->is_disabled()) {
			continue;
		}

		Vector3 ray_from;
		Vector3 ray_direction;
		float ray_length = 0.0f;
		bool ray_slide = false;

		if (_get_separation_ray(*instance, scale, ray_from, ray_direction, ray_length, ray_slide)) {
			if (!p_collide_separation_ray) {
				continue;
			}

			// Sweep the tip of the ray as a point.
			const Vector3 tip = body_rotation.xform(ray_from + ray_direction * ray_length);
			const b3Vec3 point = to_b3(tip);
			const b3ShapeProxy proxy = { &point, 1, 0.0f };

			CastContext context;
			context.filter = &filter;
			b3World_CastShape(space->get_world(), origin, &proxy, to_b3(p_motion), Box3DFilter::make_query(p_body.get_collision_mask()), _cast_callback, &context);

			if (context.hit) {
				collided = true;
				const float safety = 0.0005f / motion_length;
				r_unsafe_fraction = MIN(r_unsafe_fraction, (real_t)context.fraction);
				r_safe_fraction = MIN(r_safe_fraction, (real_t)MAX(context.fraction - safety, 0.0f));
			}

			continue;
		}

		if (!instance->get_shape()->is_convex()) {
			continue;
		}

		Box3DQueryShape query;
		if (!instance->get_shape()->make_query_shape(body_rotation * instance->get_transform_scaled(scale), query)) {
			continue;
		}

		LocalVector<b3Vec3> points;
		float radius = 0.0f;
		query.make_proxy(points, radius);

		const b3ShapeProxy proxy = { points.ptr(), (int)points.size(), radius };

		CastContext context;
		context.filter = &filter;
		b3World_CastShape(space->get_world(), origin, &proxy, to_b3(p_motion), Box3DFilter::make_query(p_body.get_collision_mask()), _cast_callback, &context);

		if (context.hit) {
			collided = true;

			const float safety = 0.0005f / motion_length;
			r_unsafe_fraction = MIN(r_unsafe_fraction, (real_t)context.fraction);
			r_safe_fraction = MIN(r_safe_fraction, (real_t)MAX(context.fraction - safety, 0.0f));
		}
	}

	return collided;
}

bool Box3DPhysicsDirectSpaceState3D::_body_motion_collide(const Box3DBody3D &p_body, const Transform3D &p_transform, const Vector3 &p_motion, float p_margin, int p_max_collisions, bool p_collide_separation_ray, const HashSet<RID> &p_excluded_bodies, const HashSet<ObjectID> &p_excluded_objects, PhysicsServer3D::MotionResult *r_result) const {
	if (p_max_collisions == 0) {
		return false;
	}

	QueryFilter filter;
	filter.exclude_rids = &p_excluded_bodies;
	filter.exclude_objects = &p_excluded_objects;
	filter.self = &p_body;

	const Vector3 scale = p_body.get_scale();
	const b3Pos origin = to_b3_pos(p_transform.origin);
	const Transform3D body_rotation(p_transform.basis, Vector3());

	struct Hit {
		Box3DContact contact;
		const Box3DObject3D *collider = nullptr;
		int local_shape = 0;
		int collider_shape = 0;
	};

	LocalVector<Hit> hits;
	LocalVector<Box3DContact> shape_contacts;

	for (int shape_index = 0; shape_index < p_body.get_shape_count(); shape_index++) {
		const Box3DShapeInstance *instance = p_body.get_shape_instance(shape_index);

		if (instance->is_disabled()) {
			continue;
		}

		Vector3 ray_from;
		Vector3 ray_direction;
		float ray_length = 0.0f;
		bool ray_slide = false;

		if (_get_separation_ray(*instance, scale, ray_from, ray_direction, ray_length, ray_slide)) {
			if (!p_collide_separation_ray) {
				continue;
			}

			const Vector3 ray_start = body_rotation.xform(ray_from);
			const Vector3 ray_vector = body_rotation.basis.xform(ray_direction) * ray_length;

			RayContext context;
			context.filter = &filter;
			b3World_CastRay(space->get_world(), to_b3_pos(p_transform.origin + ray_start), to_b3(ray_vector), Box3DFilter::make_query(p_body.get_collision_mask()), _ray_callback, &context);

			if (context.hit) {
				const Box3DShapeInstance *other_instance = _get_instance(context.shape);
				if (other_instance == nullptr) {
					continue;
				}

				Hit hit;
				hit.collider = other_instance->get_owner();
				hit.local_shape = shape_index;
				hit.collider_shape = MAX(hit.collider->find_shape_index(other_instance->get_id()), 0);
				hit.contact.point_on_target = pos_to_godot(context.point) - p_transform.origin;
				hit.contact.point_on_query = ray_start + ray_vector;
				hit.contact.normal = ray_slide ? to_godot(context.normal) : -ray_vector.normalized();
				hit.contact.depth = ray_length * (1.0f - context.fraction);
				hits.push_back(hit);
			}

			continue;
		}

		if (!instance->get_shape()->is_convex()) {
			continue;
		}

		Box3DQueryShape query;
		if (!instance->get_shape()->make_query_shape(body_rotation * instance->get_transform_scaled(scale), query)) {
			continue;
		}

		b3AABB bounds = query.compute_aabb();
		bounds.lowerBound = b3Sub(bounds.lowerBound, b3Vec3{ p_margin, p_margin, p_margin });
		bounds.upperBound = b3Add(bounds.upperBound, b3Vec3{ p_margin, p_margin, p_margin });

		LocalVector<b3ShapeId> candidates;
		_gather_shapes(space->get_world(), origin, bounds, p_body.get_collision_mask(), filter, candidates);

		for (const b3ShapeId &candidate : candidates) {
			const Box3DShapeInstance *other_instance = _get_instance(candidate);
			if (other_instance == nullptr) {
				continue;
			}

			shape_contacts.clear();
			Box3DNarrowPhase::collide(query, origin, candidate, p_margin, shape_contacts);

			for (const Box3DContact &contact : shape_contacts) {
				Hit hit;
				hit.contact = contact;
				hit.collider = other_instance->get_owner();
				hit.local_shape = shape_index;
				hit.collider_shape = MAX(hit.collider->find_shape_index(other_instance->get_id()), 0);
				hits.push_back(hit);
			}
		}
	}

	if (hits.is_empty() || r_result == nullptr) {
		return !hits.is_empty();
	}

	// Deepest collisions first, so that the first one is the one that matters most.
	struct HitDeeper {
		bool operator()(const Hit &p_a, const Hit &p_b) const { return p_a.contact.depth > p_b.contact.depth; }
	};
	hits.sort_custom<HitDeeper>();

	int count = 0;

	for (const Hit &hit : hits) {
		const Vector3 position = p_transform.origin + hit.contact.point_on_target;

		PhysicsServer3D::MotionCollision &collision = r_result->collisions[count++];

		collision.position = position;
		collision.normal = hit.contact.normal;
		collision.collider_velocity = hit.collider->get_velocity_at_position(position);
		collision.collider_angular_velocity = hit.collider->get_angular_velocity();
		collision.depth = hit.contact.depth + p_margin;
		collision.local_shape = hit.local_shape;
		collision.collider_id = hit.collider->get_instance_id();
		collision.collider = hit.collider->get_rid();
		collision.collider_shape = hit.collider_shape;

		if (count == p_max_collisions) {
			break;
		}
	}

	r_result->collision_count = count;

	return count > 0;
}

bool Box3DPhysicsDirectSpaceState3D::body_test_motion(const Box3DBody3D &p_body, const PhysicsServer3D::MotionParameters &p_parameters, PhysicsServer3D::MotionResult *r_result) const {
	ERR_FAIL_COND_V_MSG(space->is_stepping(), false, "body_test_motion (maybe from move_and_slide?) must not be called while the physics space is being stepped.");

	if (!p_body.in_space()) {
		return false;
	}

	space->flush_pending_shapes();

	const float margin = MAX((float)p_parameters.margin, 0.0001f);
	const int max_collisions = MIN(p_parameters.max_collisions, PhysicsServer3D::MotionResult::MAX_COLLISIONS);

	Vector3 scale;
	Transform3D transform = _remove_scale(p_parameters.from, scale);

	Vector3 recovery;
	const bool recovered = _body_motion_recover(p_body, transform, margin, p_parameters.exclude_bodies, p_parameters.exclude_objects, recovery);

	transform.origin += recovery;

	real_t safe_fraction = 1.0;
	real_t unsafe_fraction = 1.0;

	const bool hit = _body_motion_cast(p_body, transform, p_parameters.motion, p_parameters.collide_separation_ray, p_parameters.exclude_bodies, p_parameters.exclude_objects, safe_fraction, unsafe_fraction);

	bool collided = false;

	if (hit || (recovered && p_parameters.recovery_as_collision)) {
		collided = _body_motion_collide(p_body, transform.translated(p_parameters.motion * unsafe_fraction), p_parameters.motion, margin, max_collisions, p_parameters.collide_separation_ray, p_parameters.exclude_bodies, p_parameters.exclude_objects, r_result);
	}

	if (r_result == nullptr) {
		return collided;
	}

	if (collided) {
		const PhysicsServer3D::MotionCollision &deepest = r_result->collisions[0];

		r_result->travel = recovery + p_parameters.motion * safe_fraction;
		r_result->remainder = p_parameters.motion - p_parameters.motion * safe_fraction;
		r_result->collision_depth = deepest.depth;
		r_result->collision_safe_fraction = safe_fraction;
		r_result->collision_unsafe_fraction = unsafe_fraction;
	} else {
		r_result->travel = recovery + p_parameters.motion;
		r_result->remainder = Vector3();
		r_result->collision_depth = 0.0f;
		r_result->collision_safe_fraction = 1.0f;
		r_result->collision_unsafe_fraction = 1.0f;
		r_result->collision_count = 0;
	}

	return collided;
}
