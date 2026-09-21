/**************************************************************************/
/*  box3d_area_3d.h                                                       */
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

#include "box3d_object_3d.h"

#include "core/templates/hash_map.h"
#include "servers/physics_3d/physics_server_3d.h"

class Box3DBody3D;

class Box3DArea3D final : public Box3DObject3D {
public:
	typedef PhysicsServer3D::AreaSpaceOverrideMode OverrideMode;

private:
	// One overlapping shape of another object, keyed by the Box3D shape id of that shape.
	struct Visitor {
		RID rid;
		ObjectID instance_id;
		bool is_area = false;
		int other_shape_index = 0;
		int self_shape_index = 0;
	};

	struct PendingEvent {
		PhysicsServer3D::AreaBodyStatus status = PhysicsServer3D::AREA_BODY_ADDED;
		RID rid;
		ObjectID instance_id;
		int other_shape_index = 0;
		int self_shape_index = 0;
	};

	SelfList<Box3DArea3D> call_queries_element;

	HashMap<uint64_t, Visitor> visitors;
	HashMap<RID, int> body_shape_counts;

	LocalVector<PendingEvent> pending_body_events;
	LocalVector<PendingEvent> pending_area_events;

	Vector3 gravity_vector = Vector3(0, -1, 0);
	Vector3 wind_source;
	Vector3 wind_direction;

	Callable body_monitor_callback;
	Callable area_monitor_callback;

	float priority = 0.0f;
	float gravity = 9.8f;
	float point_gravity_distance = 0.0f;
	float linear_damp = 0.1f;
	float angular_damp = 0.1f;
	float wind_pressure = 0.0f;
	float wind_attenuation_factor = 0.0f;

	OverrideMode gravity_mode = PhysicsServer3D::AREA_SPACE_OVERRIDE_DISABLED;
	OverrideMode linear_damp_mode = PhysicsServer3D::AREA_SPACE_OVERRIDE_DISABLED;
	OverrideMode angular_damp_mode = PhysicsServer3D::AREA_SPACE_OVERRIDE_DISABLED;

	bool monitorable = false;
	bool point_gravity = false;

	virtual b3BodyType _get_body_type() const override { return b3_staticBody; }
	virtual b3ShapeDef _make_shape_def() const override;
	virtual void _fill_body_def(b3BodyDef &r_def) const override;

	virtual void _add_to_space() override;

	void _enqueue_call_queries();
	void _dequeue_call_queries();

	void _report_event(const Callable &p_callback, const PendingEvent &p_event) const;

	void _notify_body_entered(Box3DBody3D *p_body);
	void _notify_body_exited(Box3DBody3D *p_body);
	void _notify_bodies_updated(bool p_priority_changed = false);

	void _remove_all_overlaps(bool p_report);
	void _update_sensor_events();

	virtual void _shapes_committed() override;
	virtual void _space_changing() override;

public:
	Box3DArea3D();
	virtual ~Box3DArea3D() override;

	void set_transform(Transform3D p_transform);

	Variant get_param(PhysicsServer3D::AreaParameter p_param) const;
	void set_param(PhysicsServer3D::AreaParameter p_param, const Variant &p_value);

	bool has_body_monitor_callback() const { return body_monitor_callback.is_valid(); }
	void set_body_monitor_callback(const Callable &p_callback);

	bool has_area_monitor_callback() const { return area_monitor_callback.is_valid(); }
	void set_area_monitor_callback(const Callable &p_callback);

	bool is_monitoring_bodies() const { return has_body_monitor_callback(); }
	bool is_monitoring_areas() const { return has_area_monitor_callback(); }
	bool is_monitoring() const { return is_monitoring_bodies() || is_monitoring_areas(); }

	bool is_monitorable() const { return monitorable; }
	void set_monitorable(bool p_monitorable);

	bool can_monitor(const Box3DBody3D &p_other) const;
	bool can_monitor(const Box3DArea3D &p_other) const;

	virtual bool can_interact_with(const Box3DBody3D &p_other) const override;
	virtual bool can_interact_with(const Box3DArea3D &p_other) const override;

	virtual Vector3 get_velocity_at_position(const Vector3 &p_position) const override { return Vector3(); }

	virtual bool reports_contacts() const override { return false; }

	float get_priority() const { return priority; }
	void set_priority(float p_priority);

	float get_gravity() const { return gravity; }
	void set_gravity(float p_gravity);

	bool is_point_gravity() const { return point_gravity; }
	void set_point_gravity(bool p_enabled);

	float get_point_gravity_distance() const { return point_gravity_distance; }
	void set_point_gravity_distance(float p_distance);

	float get_linear_damp() const { return linear_damp; }
	void set_linear_damp(float p_damp);

	float get_angular_damp() const { return angular_damp; }
	void set_angular_damp(float p_damp);

	OverrideMode get_gravity_mode() const { return gravity_mode; }
	void set_gravity_mode(OverrideMode p_mode);

	OverrideMode get_linear_damp_mode() const { return linear_damp_mode; }
	void set_linear_damp_mode(OverrideMode p_mode);

	OverrideMode get_angular_damp_mode() const { return angular_damp_mode; }
	void set_angular_damp_mode(OverrideMode p_mode);

	Vector3 get_gravity_vector() const { return gravity_vector; }
	void set_gravity_vector(const Vector3 &p_vector);

	float get_wind_pressure() const { return wind_pressure; }
	void set_wind_pressure(float p_wind_pressure) { wind_pressure = p_wind_pressure; }

	float get_wind_attenuation_factor() const { return wind_attenuation_factor; }
	void set_wind_attenuation_factor(float p_wind_attenuation_factor) { wind_attenuation_factor = p_wind_attenuation_factor; }

	const Vector3 &get_wind_source() const { return wind_source; }
	void set_wind_source(const Vector3 &p_wind_source) { wind_source = p_wind_source; }

	const Vector3 &get_wind_direction() const { return wind_direction; }
	void set_wind_direction(const Vector3 &p_wind_direction) { wind_direction = p_wind_direction; }

	Vector3 compute_gravity(const Vector3 &p_position) const;

	// Called with the shape id of the visiting shape when Box3D reports it began or ended overlapping one of the sensors of this area.
	void shape_entered(const b3ShapeId &p_visitor_shape, Box3DObject3D *p_other, uint32_t p_other_instance_id, uint32_t p_self_instance_id);
	void shape_exited(const b3ShapeId &p_visitor_shape);

	void call_queries();

	virtual bool has_custom_center_of_mass() const override { return false; }
	virtual Vector3 get_center_of_mass_custom() const override { return Vector3(); }

	// Incorporates the value provided by `p_getter` into `p_value` according to the override mode `p_mode`.
	// Returns true if further calls to this function should stop (i.e. value has been replaced entirely).
	template <typename TValue, typename TGetter>
	static bool apply_override(TValue &p_value, PhysicsServer3D::AreaSpaceOverrideMode p_mode, TGetter &&p_getter);
};

template <typename TValue, typename TGetter>
inline bool Box3DArea3D::apply_override(TValue &p_value, PhysicsServer3D::AreaSpaceOverrideMode p_mode, TGetter &&p_getter) {
	switch (p_mode) {
		case PhysicsServer3D::AREA_SPACE_OVERRIDE_DISABLED: {
			return false;
		}
		case PhysicsServer3D::AREA_SPACE_OVERRIDE_COMBINE: {
			p_value += p_getter();
			return false;
		}
		case PhysicsServer3D::AREA_SPACE_OVERRIDE_COMBINE_REPLACE: {
			p_value += p_getter();
			return true;
		}
		case PhysicsServer3D::AREA_SPACE_OVERRIDE_REPLACE: {
			p_value = p_getter();
			return true;
		}
		case PhysicsServer3D::AREA_SPACE_OVERRIDE_REPLACE_COMBINE: {
			p_value = p_getter();
			return false;
		}
		default: {
			ERR_FAIL_V_MSG(false, vformat("Unhandled override mode: '%d'. This should not happen. Please report this.", p_mode));
		}
	}
}
