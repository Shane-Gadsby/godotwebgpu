/**************************************************************************/
/*  box3d_area_3d.cpp                                                     */
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

#include "box3d_area_3d.h"

#include "../box3d_physics_server_3d.h"
#include "../spaces/box3d_space_3d.h"
#include "box3d_body_3d.h"

b3ShapeDef Box3DArea3D::_make_shape_def() const {
	b3ShapeDef def = b3DefaultShapeDef();
	def.density = 0.0f;
	def.isSensor = true;
	def.enableSensorEvents = is_monitoring() || monitorable;
	def.filter = Box3DFilter::make(collision_layer);
	def.enableCustomFiltering = true;
	def.updateBodyMass = false;
	return def;
}

void Box3DArea3D::_fill_body_def(b3BodyDef &r_def) const {
	r_def.enableSleep = false;
}

void Box3DArea3D::_add_to_space() {
	ERR_FAIL_NULL(space);

	if (space->get_default_area() == this) {
		// The default area only carries the environment parameters of the space, so it needs no body.
		return;
	}

	Box3DObject3D::_add_to_space();
}

void Box3DArea3D::_enqueue_call_queries() {
	if (space != nullptr) {
		space->enqueue_call_queries(&call_queries_element);
	}
}

void Box3DArea3D::_dequeue_call_queries() {
	if (space != nullptr) {
		space->dequeue_call_queries(&call_queries_element);
	}
}

void Box3DArea3D::_report_event(const Callable &p_callback, const PendingEvent &p_event) const {
	ERR_FAIL_COND(!p_callback.is_valid());

	const Variant arg1 = p_event.status;
	const Variant arg2 = p_event.rid;
	const Variant arg3 = p_event.instance_id;
	const Variant arg4 = p_event.other_shape_index;
	const Variant arg5 = p_event.self_shape_index;
	const Variant *args[5] = { &arg1, &arg2, &arg3, &arg4, &arg5 };

	Callable::CallError ce;
	Variant ret;
	p_callback.callp(args, 5, ret, ce);

	if (unlikely(ce.error != Callable::CallError::CALL_OK)) {
		ERR_PRINT_ONCE(vformat("Failed to call area monitor callback for '%s'. It returned the following error: '%s'.", to_string(), Variant::get_callable_error_text(p_callback, args, 5, ce)));
	}
}

void Box3DArea3D::_notify_body_entered(Box3DBody3D *p_body) {
	if (p_body != nullptr) {
		p_body->add_area(this);
	}
}

void Box3DArea3D::_notify_body_exited(Box3DBody3D *p_body) {
	if (p_body != nullptr) {
		p_body->remove_area(this);
	}
}

void Box3DArea3D::_notify_bodies_updated(bool p_priority_changed) {
	if (unlikely(space == nullptr)) {
		return;
	}

	if (space->get_default_area() == this) {
		space->increment_default_area_changed_count();
		return;
	}

	for (const KeyValue<RID, int> &E : body_shape_counts) {
		if (Box3DBody3D *body = Box3DPhysicsServer3D::get_singleton()->get_body(E.key)) {
			body->update_area(this, p_priority_changed);
		}
	}
}

void Box3DArea3D::_remove_all_overlaps(bool p_report) {
	if (p_report) {
		for (const KeyValue<uint64_t, Visitor> &E : visitors) {
			PendingEvent event;
			event.status = PhysicsServer3D::AREA_BODY_REMOVED;
			event.rid = E.value.rid;
			event.instance_id = E.value.instance_id;
			event.other_shape_index = E.value.other_shape_index;
			event.self_shape_index = E.value.self_shape_index;

			if (E.value.is_area) {
				pending_area_events.push_back(event);
			} else {
				pending_body_events.push_back(event);
			}
		}

		if (!visitors.is_empty()) {
			_enqueue_call_queries();
		}
	}

	for (const KeyValue<RID, int> &E : body_shape_counts) {
		_notify_body_exited(Box3DPhysicsServer3D::get_singleton()->get_body(E.key));
	}

	visitors.clear();
	body_shape_counts.clear();

	if (!p_report) {
		pending_body_events.clear();
		pending_area_events.clear();
	}
}

void Box3DArea3D::_update_sensor_events() {
	if (!in_space()) {
		return;
	}

	const bool enabled = is_monitoring() || monitorable;

	for (Box3DShapeInstance *instance : shapes) {
		for (const b3ShapeId &id : instance->built_shapes) {
			b3Shape_EnableSensorEvents(id, enabled);
		}
	}
}

void Box3DArea3D::_shapes_committed() {
	// New sensors have to start with a clean slate, as the previous ones took their overlaps with them. Box3D will
	// report the overlaps that still exist as new ones, so the old ones are reported as ended to keep things balanced.
	_remove_all_overlaps(true);
}

void Box3DArea3D::_space_changing() {
	Box3DObject3D::_space_changing();

	_remove_all_overlaps(false);
	_dequeue_call_queries();
}

Box3DArea3D::Box3DArea3D() :
		Box3DObject3D(OBJECT_TYPE_AREA),
		call_queries_element(this) {
}

Box3DArea3D::~Box3DArea3D() = default;

void Box3DArea3D::set_transform(Transform3D p_transform) {
	ERR_FAIL_COND_MSG(!p_transform.is_finite(), vformat("A non-finite transform was passed to area '%s'. It will be ignored.", to_string()));

	Vector3 new_scale;
	if (unlikely(!Box3DMath::decompose(p_transform.basis, new_scale))) {
		WARN_PRINT(vformat("An invalid transform was passed to area '%s'. The basis was singular, which is not supported by Box3D. This is likely caused by one or more axes having a scale of zero. The basis (and thus its scale) will be treated as identity.", to_string()));
	}

	// Ideally we would do an exact comparison here, but due to floating-point precision this would be invalidated very often.
	if (!scale.is_equal_approx(new_scale)) {
		scale = new_scale;
		shapes_changed();
	}

	if (!in_space()) {
		transform_unscaled = p_transform;
	} else {
		b3Body_SetTransform(body_id, to_b3_pos(p_transform.origin), to_b3(p_transform.basis));
	}
}

Variant Box3DArea3D::get_param(PhysicsServer3D::AreaParameter p_param) const {
	switch (p_param) {
		case PhysicsServer3D::AREA_PARAM_GRAVITY_OVERRIDE_MODE: {
			return get_gravity_mode();
		}
		case PhysicsServer3D::AREA_PARAM_GRAVITY: {
			return get_gravity();
		}
		case PhysicsServer3D::AREA_PARAM_GRAVITY_VECTOR: {
			return get_gravity_vector();
		}
		case PhysicsServer3D::AREA_PARAM_GRAVITY_IS_POINT: {
			return is_point_gravity();
		}
		case PhysicsServer3D::AREA_PARAM_GRAVITY_POINT_UNIT_DISTANCE: {
			return get_point_gravity_distance();
		}
		case PhysicsServer3D::AREA_PARAM_LINEAR_DAMP_OVERRIDE_MODE: {
			return get_linear_damp_mode();
		}
		case PhysicsServer3D::AREA_PARAM_LINEAR_DAMP: {
			return get_linear_damp();
		}
		case PhysicsServer3D::AREA_PARAM_ANGULAR_DAMP_OVERRIDE_MODE: {
			return get_angular_damp_mode();
		}
		case PhysicsServer3D::AREA_PARAM_ANGULAR_DAMP: {
			return get_angular_damp();
		}
		case PhysicsServer3D::AREA_PARAM_PRIORITY: {
			return get_priority();
		}
		case PhysicsServer3D::AREA_PARAM_WIND_FORCE_MAGNITUDE: {
			// This parameter is named incorrectly. It's actually a pressure.
			return get_wind_pressure();
		}
		case PhysicsServer3D::AREA_PARAM_WIND_SOURCE: {
			return get_wind_source();
		}
		case PhysicsServer3D::AREA_PARAM_WIND_DIRECTION: {
			return get_wind_direction();
		}
		case PhysicsServer3D::AREA_PARAM_WIND_ATTENUATION_FACTOR: {
			return get_wind_attenuation_factor();
		}
		default: {
			ERR_FAIL_V_MSG(Variant(), vformat("Unhandled area parameter: '%d'. This should not happen. Please report this.", p_param));
		}
	}
}

void Box3DArea3D::set_param(PhysicsServer3D::AreaParameter p_param, const Variant &p_value) {
	switch (p_param) {
		case PhysicsServer3D::AREA_PARAM_GRAVITY_OVERRIDE_MODE: {
			set_gravity_mode((OverrideMode)(int)p_value);
		} break;
		case PhysicsServer3D::AREA_PARAM_GRAVITY: {
			set_gravity(p_value);
		} break;
		case PhysicsServer3D::AREA_PARAM_GRAVITY_VECTOR: {
			set_gravity_vector(p_value);
		} break;
		case PhysicsServer3D::AREA_PARAM_GRAVITY_IS_POINT: {
			set_point_gravity(p_value);
		} break;
		case PhysicsServer3D::AREA_PARAM_GRAVITY_POINT_UNIT_DISTANCE: {
			set_point_gravity_distance(p_value);
		} break;
		case PhysicsServer3D::AREA_PARAM_LINEAR_DAMP_OVERRIDE_MODE: {
			set_linear_damp_mode((OverrideMode)(int)p_value);
		} break;
		case PhysicsServer3D::AREA_PARAM_LINEAR_DAMP: {
			set_linear_damp(p_value);
		} break;
		case PhysicsServer3D::AREA_PARAM_ANGULAR_DAMP_OVERRIDE_MODE: {
			set_angular_damp_mode((OverrideMode)(int)p_value);
		} break;
		case PhysicsServer3D::AREA_PARAM_ANGULAR_DAMP: {
			set_angular_damp(p_value);
		} break;
		case PhysicsServer3D::AREA_PARAM_PRIORITY: {
			set_priority(p_value);
		} break;
		case PhysicsServer3D::AREA_PARAM_WIND_FORCE_MAGNITUDE: {
			// This parameter is named incorrectly. It's actually a pressure.
			set_wind_pressure(p_value);
		} break;
		case PhysicsServer3D::AREA_PARAM_WIND_SOURCE: {
			set_wind_source(p_value);
		} break;
		case PhysicsServer3D::AREA_PARAM_WIND_DIRECTION: {
			set_wind_direction(p_value);
		} break;
		case PhysicsServer3D::AREA_PARAM_WIND_ATTENUATION_FACTOR: {
			set_wind_attenuation_factor(p_value);
		} break;
		default: {
			ERR_FAIL_MSG(vformat("Unhandled area parameter: '%d'. This should not happen. Please report this.", p_param));
		} break;
	}
}

void Box3DArea3D::set_body_monitor_callback(const Callable &p_callback) {
	if (p_callback == body_monitor_callback) {
		return;
	}

	body_monitor_callback = p_callback;

	_update_sensor_events();
}

void Box3DArea3D::set_area_monitor_callback(const Callable &p_callback) {
	if (p_callback == area_monitor_callback) {
		return;
	}

	area_monitor_callback = p_callback;

	_update_sensor_events();
}

void Box3DArea3D::set_monitorable(bool p_monitorable) {
	if (p_monitorable == monitorable) {
		return;
	}

	monitorable = p_monitorable;

	_update_sensor_events();
}

bool Box3DArea3D::can_monitor(const Box3DBody3D &p_other) const {
	return is_monitoring_bodies() && (collision_mask & p_other.get_collision_layer()) != 0;
}

bool Box3DArea3D::can_monitor(const Box3DArea3D &p_other) const {
	return is_monitoring_areas() && p_other.is_monitorable() && (collision_mask & p_other.get_collision_layer()) != 0;
}

bool Box3DArea3D::can_interact_with(const Box3DBody3D &p_other) const {
	return can_monitor(p_other);
}

bool Box3DArea3D::can_interact_with(const Box3DArea3D &p_other) const {
	return can_monitor(p_other) || p_other.can_monitor(*this);
}

void Box3DArea3D::set_priority(float p_priority) {
	if (p_priority == priority) {
		return;
	}

	priority = p_priority;

	_notify_bodies_updated(true);
}

void Box3DArea3D::set_gravity(float p_gravity) {
	if (p_gravity == gravity) {
		return;
	}

	gravity = p_gravity;

	_notify_bodies_updated();
}

void Box3DArea3D::set_point_gravity(bool p_enabled) {
	if (p_enabled == point_gravity) {
		return;
	}

	point_gravity = p_enabled;

	_notify_bodies_updated();
}

void Box3DArea3D::set_point_gravity_distance(float p_distance) {
	if (p_distance == point_gravity_distance) {
		return;
	}

	point_gravity_distance = p_distance;

	_notify_bodies_updated();
}

void Box3DArea3D::set_linear_damp(float p_damp) {
	if (p_damp == linear_damp) {
		return;
	}

	linear_damp = p_damp;

	_notify_bodies_updated();
}

void Box3DArea3D::set_angular_damp(float p_damp) {
	if (p_damp == angular_damp) {
		return;
	}

	angular_damp = p_damp;

	_notify_bodies_updated();
}

void Box3DArea3D::set_gravity_mode(OverrideMode p_mode) {
	if (p_mode == gravity_mode) {
		return;
	}

	gravity_mode = p_mode;

	_notify_bodies_updated();
}

void Box3DArea3D::set_linear_damp_mode(OverrideMode p_mode) {
	if (p_mode == linear_damp_mode) {
		return;
	}

	linear_damp_mode = p_mode;

	_notify_bodies_updated();
}

void Box3DArea3D::set_angular_damp_mode(OverrideMode p_mode) {
	if (p_mode == angular_damp_mode) {
		return;
	}

	angular_damp_mode = p_mode;

	_notify_bodies_updated();
}

void Box3DArea3D::set_gravity_vector(const Vector3 &p_vector) {
	if (p_vector == gravity_vector) {
		return;
	}

	gravity_vector = p_vector;

	_notify_bodies_updated();
}

Vector3 Box3DArea3D::compute_gravity(const Vector3 &p_position) const {
	if (!point_gravity) {
		return gravity_vector * gravity;
	}

	const Vector3 point = get_transform_scaled().xform(gravity_vector);
	const Vector3 to_point = point - p_position;
	const real_t to_point_dist_sq = MAX(to_point.length_squared(), (real_t)CMP_EPSILON);
	const Vector3 to_point_dir = to_point / Math::sqrt(to_point_dist_sq);

	if (point_gravity_distance == 0.0f) {
		return to_point_dir * gravity;
	}

	const float gravity_dist_sq = point_gravity_distance * point_gravity_distance;

	return to_point_dir * (gravity * gravity_dist_sq / to_point_dist_sq);
}

void Box3DArea3D::shape_entered(const b3ShapeId &p_visitor_shape, Box3DObject3D *p_other, uint32_t p_other_instance_id, uint32_t p_self_instance_id) {
	Visitor visitor;
	visitor.rid = p_other->get_rid();
	visitor.instance_id = p_other->get_instance_id();
	visitor.is_area = p_other->is_area();
	visitor.other_shape_index = MAX(p_other->find_shape_index(p_other_instance_id), 0);
	visitor.self_shape_index = MAX(find_shape_index(p_self_instance_id), 0);

	PendingEvent event;
	event.status = PhysicsServer3D::AREA_BODY_ADDED;
	event.rid = visitor.rid;
	event.instance_id = visitor.instance_id;
	event.other_shape_index = visitor.other_shape_index;
	event.self_shape_index = visitor.self_shape_index;

	visitors[box3d_shape_key(p_visitor_shape)] = visitor;

	if (visitor.is_area) {
		pending_area_events.push_back(event);
	} else {
		if (body_shape_counts[visitor.rid]++ == 0) {
			_notify_body_entered(p_other->as_body());
		}

		pending_body_events.push_back(event);
	}

	_enqueue_call_queries();
}

void Box3DArea3D::shape_exited(const b3ShapeId &p_visitor_shape) {
	const uint64_t key = box3d_shape_key(p_visitor_shape);

	HashMap<uint64_t, Visitor>::Iterator found = visitors.find(key);
	if (found == visitors.end()) {
		return;
	}

	const Visitor visitor = found->value;
	visitors.remove(found);

	PendingEvent event;
	event.status = PhysicsServer3D::AREA_BODY_REMOVED;
	event.rid = visitor.rid;
	event.instance_id = visitor.instance_id;
	event.other_shape_index = visitor.other_shape_index;
	event.self_shape_index = visitor.self_shape_index;

	if (visitor.is_area) {
		pending_area_events.push_back(event);
	} else {
		int *count = body_shape_counts.getptr(visitor.rid);
		if (count != nullptr && --(*count) <= 0) {
			body_shape_counts.erase(visitor.rid);
			_notify_body_exited(Box3DPhysicsServer3D::get_singleton()->get_body(visitor.rid));
		}

		pending_body_events.push_back(event);
	}

	_enqueue_call_queries();
}

void Box3DArea3D::call_queries() {
	LocalVector<PendingEvent> body_events = std::move(pending_body_events);
	pending_body_events.clear();

	LocalVector<PendingEvent> area_events = std::move(pending_area_events);
	pending_area_events.clear();

	if (body_monitor_callback.is_valid()) {
		for (const PendingEvent &event : body_events) {
			_report_event(body_monitor_callback, event);
		}
	}

	if (area_monitor_callback.is_valid()) {
		for (const PendingEvent &event : area_events) {
			_report_event(area_monitor_callback, event);
		}
	}
}
