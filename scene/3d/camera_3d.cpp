/**************************************************************************/
/*  camera_3d.cpp                                                         */
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

#include "camera_3d.h"

#include "core/config/engine.h"
#include "core/math/projection.h"
#include "core/math/random_pcg.h"
#include "core/math/transform_interpolator.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/rendering/panini_projection.h"
#include "servers/rendering/rendering_server.h"

#ifndef PHYSICS_3D_DISABLED
#include "servers/physics_3d/physics_server_3d.h"
#endif // PHYSICS_3D_DISABLED

void Camera3D::_update_audio_listener_state() {
}

void Camera3D::_request_camera_update() {
	_update_camera();
}

void Camera3D::fti_pump_property() {
	switch (mode) {
		default:
			break;
		case PROJECTION_PERSPECTIVE: {
			fov.pump();
		} break;
		case PROJECTION_ORTHOGONAL: {
			size.pump();
		} break;
		case PROJECTION_FRUSTUM: {
			size.pump();
			frustum_offset.pump();
		} break;
	}
	_near.pump();
	_far.pump();

	Node3D::fti_pump_property();
}

void Camera3D::fti_update_servers_property() {
	if (camera.is_valid()) {
		float f = Engine::get_singleton()->get_physics_interpolation_fraction();

		bool update_fov = fov.interpolate(f);
		bool update_near = _near.interpolate(f);
		bool update_far = _far.interpolate(f);
		bool update_size = size.interpolate(f);
		bool update_frustum_offset = frustum_offset.interpolate(f);

		// If there have been changes due to interpolated values, OR we are forcing an update, update the servers.
		switch (mode) {
			default:
				break;
			case PROJECTION_PERSPECTIVE: {
				if (update_fov || update_near || update_far) {
					RS::get_singleton()->camera_set_perspective(camera, fov.interpolated(), _near.interpolated(), _far.interpolated());
				}
			} break;
			case PROJECTION_ORTHOGONAL: {
				if (update_size || update_near || update_far) {
					RS::get_singleton()->camera_set_orthogonal(camera, size.interpolated(), _near.interpolated(), _far.interpolated());
				}
			} break;
			case PROJECTION_FRUSTUM: {
				if (update_size || update_frustum_offset || update_near || update_far) {
					RS::get_singleton()->camera_set_frustum(camera, size.interpolated(), frustum_offset.interpolated(), _near.interpolated(), _far.interpolated());
				}
			} break;
		}
	}

	Node3D::fti_update_servers_property();
}

void Camera3D::fti_update_servers_xform() {
	if (camera.is_valid()) {
		Transform3D tr = _get_adjusted_camera_transform(_get_cached_global_transform_interpolated());
		RS::get_singleton()->camera_set_transform(camera, tr);
	}
	Node3D::fti_update_servers_xform();
}

void Camera3D::_update_camera_mode() {
	force_change = true;
	switch (mode) {
		case PROJECTION_PERSPECTIVE: {
			set_perspective(fov, _near, _far);

		} break;
		case PROJECTION_ORTHOGONAL: {
			set_orthogonal(size, _near, _far);
		} break;
		case PROJECTION_FRUSTUM: {
			set_frustum(size, frustum_offset, _near, _far);
		} break;
	}
	fti_notify_node_changed(false);
}

void Camera3D::_validate_property(PropertyInfo &p_property) const {
	if (Engine::get_singleton()->is_editor_hint()) {
		if (p_property.name == "fov") {
			if (mode != PROJECTION_PERSPECTIVE) {
				p_property.usage = PROPERTY_USAGE_NO_EDITOR;
			}
			return;
		} else if (p_property.name == "size") {
			if (mode != PROJECTION_ORTHOGONAL && mode != PROJECTION_FRUSTUM) {
				p_property.usage = PROPERTY_USAGE_NO_EDITOR;
			}
			return;
		} else if (p_property.name == "frustum_offset") {
			if (mode != PROJECTION_FRUSTUM) {
				p_property.usage = PROPERTY_USAGE_NO_EDITOR;
			}
			return;
		}
	}

	// A monitoring overlay's settings show while it is on.
	if (p_property.name.begins_with("monitoring_")) {
		if (((p_property.name == "monitoring_zebras_level" || p_property.name == "monitoring_zebras_color") && !monitoring_zebras_enabled) ||
				((p_property.name == "monitoring_zebras_second_level" || p_property.name == "monitoring_zebras_second_color") && !monitoring_zebras_second_enabled) ||
				(p_property.name.begins_with("monitoring_focus_peaking_") && p_property.name != "monitoring_focus_peaking_enabled" && !monitoring_focus_peaking_enabled) ||
				(p_property.name.begins_with("monitoring_histogram_") && p_property.name != "monitoring_histogram_enabled" && !monitoring_histogram_enabled) ||
				((p_property.name == "monitoring_histogram_mode" || p_property.name == "monitoring_histogram_position" || p_property.name == "monitoring_histogram_size" || p_property.name == "monitoring_histogram_opacity") && !monitoring_histogram_draw)) {
			p_property.usage = PROPERTY_USAGE_NO_EDITOR;
		}
		return;
	}

	if (attributes.is_valid()) {
		const CameraAttributesPhysical *physical_attributes = Object::cast_to<CameraAttributesPhysical>(attributes.ptr());
		if (physical_attributes) {
			if (p_property.name == "near" || p_property.name == "far" || p_property.name == "fov" || p_property.name == "keep_aspect") {
				p_property.usage = PROPERTY_USAGE_READ_ONLY | PROPERTY_USAGE_INTERNAL | PROPERTY_USAGE_EDITOR;
			}
		}
	}
}

void Camera3D::_update_camera() {
	if (!is_inside_tree()) {
		return;
	}

	if (!is_physics_interpolated_and_enabled()) {
		RenderingServer::get_singleton()->camera_set_transform(camera, get_camera_transform());
	} else {
		// Force a refresh next frame.
		fti_notify_node_changed();
	}

	if (is_part_of_edited_scene() || !is_current()) {
		return;
	}

	get_viewport()->_camera_3d_transform_changed_notify();
}

void Camera3D::_physics_interpolated_changed() {
	_update_process_mode();
}

void Camera3D::set_desired_process_modes(bool p_process_internal, bool p_physics_process_internal) {
	_desired_process_internal = p_process_internal;
	_desired_physics_process_internal = p_physics_process_internal;
	_update_process_mode();
}

void Camera3D::_update_process_mode() {
	// Autofocus, handheld shake and the histogram readback's signal run per frame; with all off, a camera costs nothing
	// more than before.
	set_process_internal(_desired_process_internal || _shake_active() || autofocus_mode != AUTOFOCUS_MODE_MANUAL || monitoring_histogram_enabled);
	set_physics_process_internal(_desired_physics_process_internal);
}

void Camera3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_WORLD: {
			// Needs to track the Viewport because it's needed on NOTIFICATION_EXIT_WORLD
			// and Spatial will handle it first, including clearing its reference to the Viewport,
			// therefore making it impossible to subclasses to access it
			viewport = get_viewport();
			ERR_FAIL_NULL(viewport);
			af_initialized = false;

			bool first_camera = viewport->_camera_3d_add(this);
			if (current || first_camera) {
				viewport->_camera_3d_set(this);
			}

#ifdef TOOLS_ENABLED
			if (Engine::get_singleton()->is_editor_hint()) {
				viewport->connect(SNAME("size_changed"), callable_mp((Node3D *)this, &Camera3D::update_gizmos));
			}
#endif
		} break;

		case NOTIFICATION_TRANSFORM_CHANGED: {
#if defined(DEBUG_ENABLED) && defined(TOOLS_ENABLED)
			if (is_physics_interpolated_and_enabled()) {
				if (!Engine::get_singleton()->is_in_physics_frame()) {
					PHYSICS_INTERPOLATION_NODE_WARNING(get_instance_id(), "Interpolated Camera3D triggered from outside physics process");
				}
			}
#endif
			_request_camera_update();
			if (doppler_tracking != DOPPLER_TRACKING_DISABLED) {
				velocity_tracker->update_position(get_global_transform().origin);
			}
		} break;

		case NOTIFICATION_EXIT_WORLD: {
			if (!is_part_of_edited_scene()) {
				if (is_current()) {
					clear_current();
					current = true; //keep it true

				} else {
					current = false;
				}
			}

			if (viewport) {
#ifdef TOOLS_ENABLED
				if (Engine::get_singleton()->is_editor_hint()) {
					viewport->disconnect(SNAME("size_changed"), callable_mp((Node3D *)this, &Camera3D::update_gizmos));
				}
#endif
				viewport->_camera_3d_remove(this);
				viewport = nullptr;
			}
		} break;

		case NOTIFICATION_BECAME_CURRENT: {
			if (viewport) {
				viewport->find_world_3d()->_register_camera(this);
			}
			_update_process_mode();
		} break;

		case NOTIFICATION_LOST_CURRENT: {
			if (viewport) {
				viewport->find_world_3d()->_remove_camera(this);
			}
			_update_process_mode();
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			const double delta = get_process_delta_time();
			// Shake first, so autofocus measures through the lens where it points this frame.
			if (_shake_active()) {
				_shake_process(delta);
			}
			if (autofocus_mode != AUTOFOCUS_MODE_MANUAL) {
				_autofocus_process(delta);
			}
			if (monitoring_histogram_enabled) {
				_monitoring_histogram_process();
			}
		} break;

#ifdef TOOLS_ENABLED
		// Autofocus writes the focus distance into the attributes resource every frame, also in the editor preview.
		// Saving must store the user's own value, not wherever the lens happened to be.
		case NOTIFICATION_EDITOR_PRE_SAVE: {
			if (af_wrote && autofocus_mode != AUTOFOCUS_MODE_MANUAL) {
				_autofocus_restore_baseline();
			}
		} break;
		case NOTIFICATION_EDITOR_POST_SAVE: {
			if (af_wrote && autofocus_mode != AUTOFOCUS_MODE_MANUAL && af_initialized) {
				_autofocus_apply(1.0 / MAX(af_diopters, 1e-5));
			}
		} break;
#endif
	}
}

Transform3D Camera3D::_get_adjusted_camera_transform(const Transform3D &p_xform) const {
	Transform3D tr = p_xform.orthonormalized();
	tr.origin += tr.basis.get_column(1) * v_offset;
	tr.origin += tr.basis.get_column(0) * h_offset;
	if (_shake_active()) {
		// The shake is an offset in the camera's own frame on top of the node's transform, which stays untouched.
		tr = tr * shake_offset;
	}
	return tr;
}

Transform3D Camera3D::get_camera_transform() const {
	if (is_physics_interpolated_and_enabled() && !Engine::get_singleton()->is_in_physics_frame()) {
		return _get_adjusted_camera_transform(_get_cached_global_transform_interpolated());
	}

	return _get_adjusted_camera_transform(get_global_transform());
}

Projection Camera3D::_get_camera_projection(real_t p_near) const {
	Size2 viewport_size = get_viewport()->get_visible_rect().size;
	Projection cm;

	switch (mode) {
		case PROJECTION_PERSPECTIVE: {
			cm.set_perspective(fov, viewport_size.aspect(), p_near, _far, keep_aspect == KEEP_WIDTH);
		} break;
		case PROJECTION_ORTHOGONAL: {
			cm.set_orthogonal(size, viewport_size.aspect(), p_near, _far, keep_aspect == KEEP_WIDTH);
		} break;
		case PROJECTION_FRUSTUM: {
			cm.set_frustum(size, viewport_size.aspect(), frustum_offset, p_near, _far);
		} break;
	}

	return cm;
}

bool Camera3D::_get_panini_mapping(const Projection &p_projection, PaniniMapping &r_mapping) const {
	// As the renderers decide (RendererCameraAttributes::_fill_effects_panini()): a perspective view, the attributes
	// set on the camera or else the world's.
	if (mode == PROJECTION_ORTHOGONAL) {
		return false;
	}
	Ref<CameraAttributes> effective = attributes;
	if (effective.is_null() && is_inside_tree()) {
		Ref<World3D> world = get_world_3d();
		if (world.is_valid()) {
			effective = world->get_camera_attributes();
		}
	}
	float distance = 0.0f;
	float vertical_compression = 0.0f;
	if (effective.is_null() || !effective->get_panini_projection(distance, vertical_compression)) {
		return false;
	}
	r_mapping.tan_half = Vector2(1.0 / p_projection.columns[0][0], 1.0 / p_projection.columns[1][1]);
	if (!(r_mapping.tan_half.x > 0.0f && r_mapping.tan_half.y > 0.0f) || !r_mapping.tan_half.is_finite()) {
		return false;
	}
	r_mapping.distance = distance;
	r_mapping.vertical_compression = vertical_compression;
	r_mapping.output_half = r_mapping.tan_half * PaniniProjection::fit_scale(r_mapping.tan_half.x, distance);
	return true;
}

Projection Camera3D::get_camera_projection() const {
	ERR_FAIL_COND_V_MSG(!is_inside_tree(), Projection(), "Camera is not inside the scene tree.");
	return _get_camera_projection(_near);
}

void Camera3D::set_perspective(real_t p_fovy_degrees, real_t p_z_near, real_t p_z_far) {
	if (!force_change && fov == p_fovy_degrees && p_z_near == _near && p_z_far == _far && mode == PROJECTION_PERSPECTIVE) {
		return;
	}

	fov = p_fovy_degrees;
	_near = p_z_near;
	_far = p_z_far;
	mode = PROJECTION_PERSPECTIVE;

	RenderingServer::get_singleton()->camera_set_perspective(camera, fov, _near, _far);
	update_gizmos();
	force_change = false;
}

void Camera3D::set_orthogonal(real_t p_size, real_t p_z_near, real_t p_z_far) {
	if (!force_change && size == p_size && p_z_near == _near && p_z_far == _far && mode == PROJECTION_ORTHOGONAL) {
		return;
	}

	size = p_size;

	_near = p_z_near;
	_far = p_z_far;
	mode = PROJECTION_ORTHOGONAL;
	force_change = false;

	RenderingServer::get_singleton()->camera_set_orthogonal(camera, size, _near, _far);
	update_gizmos();
}

void Camera3D::set_frustum(real_t p_size, Vector2 p_offset, real_t p_z_near, real_t p_z_far) {
	if (!force_change && size == p_size && frustum_offset == p_offset && p_z_near == _near && p_z_far == _far && mode == PROJECTION_FRUSTUM) {
		return;
	}

	size = p_size;
	frustum_offset = p_offset;

	_near = p_z_near;
	_far = p_z_far;
	mode = PROJECTION_FRUSTUM;
	force_change = false;

	RenderingServer::get_singleton()->camera_set_frustum(camera, size, frustum_offset, _near, _far);
	update_gizmos();
}

void Camera3D::set_projection(ProjectionType p_mode) {
	if (p_mode == PROJECTION_PERSPECTIVE || p_mode == PROJECTION_ORTHOGONAL || p_mode == PROJECTION_FRUSTUM) {
		mode = p_mode;
		_update_camera_mode();
		notify_property_list_changed();
	}
}

RID Camera3D::get_camera() const {
	return camera;
}

void Camera3D::make_current() {
	current = true;

	if (!is_inside_tree()) {
		return;
	}

	get_viewport()->_camera_3d_set(this);
}

void Camera3D::clear_current(bool p_enable_next) {
	current = false;
	if (!is_inside_tree()) {
		return;
	}

	if (get_viewport()->get_camera_3d() == this) {
		get_viewport()->_camera_3d_set(nullptr);

		if (p_enable_next && !Engine::get_singleton()->is_editor_hint()) {
			get_viewport()->_camera_3d_make_next_current(this);
		}
	}
}

void Camera3D::set_current(bool p_enabled) {
	if (p_enabled) {
		make_current();
	} else {
		clear_current();
	}
}

bool Camera3D::is_current() const {
	if (is_inside_tree() && !is_part_of_edited_scene()) {
		return get_viewport()->get_camera_3d() == this;
	} else {
		return current;
	}
}

Vector3 Camera3D::project_ray_normal(const Point2 &p_pos) const {
	Vector3 ray = project_local_ray_normal(p_pos);
	return get_camera_transform().basis.xform(ray).normalized();
}

Vector3 Camera3D::project_local_ray_normal(const Point2 &p_pos) const {
	ERR_FAIL_COND_V_MSG(!is_inside_tree(), Vector3(), "Camera is not inside scene.");

	Size2 viewport_size = get_viewport()->get_camera_rect_size();
	Vector2 cpos = get_viewport()->get_camera_coords(p_pos);
	Vector3 ray;

	if (mode == PROJECTION_ORTHOGONAL) {
		ray = Vector3(0, 0, -1);
	} else {
		Projection cm = _get_camera_projection(_near);
		PaniniMapping panini;
		if (_get_panini_mapping(cm, panini)) {
			// The output position is a Panini plane position; its direction can lie past 90 degrees off the axis.
			Vector2 ndc((cpos.x / viewport_size.width) * 2.0 - 1.0, (1.0 - (cpos.y / viewport_size.height)) * 2.0 - 1.0);
			Vector3 dir = PaniniProjection::to_direction(ndc * panini.output_half, panini.distance, panini.vertical_compression);
			ray = Vector3(dir.x, dir.y, -dir.z).normalized();
		} else {
			Vector2 screen_he = cm.get_viewport_half_extents();
			ray = Vector3(((cpos.x / viewport_size.width) * 2.0 - 1.0) * screen_he.x, ((1.0 - (cpos.y / viewport_size.height)) * 2.0 - 1.0) * screen_he.y, -_near).normalized();
		}
	}

	return ray;
}

Vector3 Camera3D::project_ray_origin(const Point2 &p_pos) const {
	ERR_FAIL_COND_V_MSG(!is_inside_tree(), Vector3(), "Camera is not inside scene.");

	Size2 viewport_size = get_viewport()->get_camera_rect_size();
	Vector2 cpos = get_viewport()->get_camera_coords(p_pos);
	ERR_FAIL_COND_V(viewport_size.y == 0, Vector3());

	if (mode == PROJECTION_ORTHOGONAL) {
		Vector2 pos = cpos / viewport_size;
		real_t vsize, hsize;
		if (keep_aspect == KEEP_WIDTH) {
			vsize = size / viewport_size.aspect();
			hsize = size;
		} else {
			hsize = size * viewport_size.aspect();
			vsize = size;
		}

		Vector3 ray;
		ray.x = pos.x * (hsize)-hsize / 2;
		ray.y = (1.0 - pos.y) * (vsize)-vsize / 2;
		ray.z = -_near;
		ray = get_camera_transform().xform(ray);
		return ray;
	} else {
		return get_camera_transform().origin;
	};
}

bool Camera3D::is_position_behind(const Vector3 &p_pos) const {
	Transform3D t = get_global_transform();
	Vector3 eyedir = -t.basis.get_column(2).normalized();
	return eyedir.dot(p_pos - t.origin) < _near;
}

Vector<Vector3> Camera3D::get_near_plane_points() const {
	ERR_FAIL_COND_V_MSG(!is_inside_tree(), Vector<Vector3>(), "Camera is not inside scene.");

	Projection cm = _get_camera_projection(_near);

	Vector3 endpoints[8];
	cm.get_endpoints(Transform3D(), endpoints);

	Vector<Vector3> points = {
		Vector3(),
		endpoints[4],
		endpoints[5],
		endpoints[6],
		endpoints[7]
	};
	return points;
}

bool Camera3D::safe_unproject_position(const Vector3 &p_pos, Point2 &r_result) const {
	ERR_FAIL_COND_V_MSG(!is_inside_tree(), false, "Camera is not inside scene.");

	Size2 viewport_size = get_viewport()->get_visible_rect().size;

	Projection cm = _get_camera_projection(_near);

	// These are homogeneous coordinates.
	// The 1.0 will later become w, the perspective divide.
	Plane p(get_camera_transform().xform_inv(p_pos), 1.0);

	p = cm.xform4(p);

	// If p.d is zero, there is a potential divide by zero ahead.
	// This can occur if the test point is exactly on the focal plane
	// with a perspective camera matrix (i.e. behind the near plane).

	// There are two possibilities here:
	// Either the test point is exactly at the origin, in which case the unprojected
	// point should theoretically be the center of the viewport, OR
	// infinity distance from the center of the viewport.

	// We should also handle the case where the test point is CLOSE
	// to the focal plane.
	// This can cause returned unprojected results near infinity.
	// The epsilon chosen here must be small, but still allow for near planes quite close to zero.

	// Here we return false and let the calling routine handle this error condition.
	if (Math::abs(p.d) < CMP_EPSILON) {
		// Establish the viewport center as our baseline
		Point2 center = viewport_size * 0.5f;
		r_result = center;

		// The viewport size here is irrelevant, we just want a high number
		// (representing infinity) but not actually close to infinity to prevent
		// knock on bugs if later math later does something with these values.
		// Suffice is for them to be WAY off the main viewport.
		const float SOME_HIGH_VALUE = 100000.0f;
		if (p.normal.x > 0) {
			r_result.x += SOME_HIGH_VALUE;
		} else if (p.normal.x < 0) {
			r_result.x -= SOME_HIGH_VALUE;
		}

		// +y is down in 2D viewport,
		// whereas in 3D, +y is up, so we need to flip here.
		if (p.normal.y > 0) {
			r_result.y -= SOME_HIGH_VALUE;
		} else if (p.normal.y < 0) {
			r_result.y += SOME_HIGH_VALUE;
		}

		return false;
	}
	p.normal /= p.d;

	PaniniMapping panini;
	if (p.d > 0.0f && _get_panini_mapping(cm, panini)) {
		// In front of the camera: where the resample shows this rectilinear position.
		Vector2 ndc = PaniniProjection::from_rectilinear(Vector2(p.normal.x, p.normal.y) * panini.tan_half, panini.distance, panini.vertical_compression) / panini.output_half;
		p.normal.x = ndc.x;
		p.normal.y = ndc.y;
	}

	r_result.x = (p.normal.x * 0.5 + 0.5) * viewport_size.x;
	r_result.y = (-p.normal.y * 0.5 + 0.5) * viewport_size.y;

	return true;
}

Point2 Camera3D::unproject_position(const Vector3 &p_pos) const {
	ERR_FAIL_COND_V_MSG(!is_inside_tree(), Point2(), "Camera is not inside scene.");

	Point2 res;

	// Unproject can fail if the test point is on the camera matrix focal plane
	// with a perspective transform.
	// In this case, the unprojected point is potentially at infinity from the viewport
	// center.
	if (!safe_unproject_position(p_pos, res)) {
#ifdef DEV_ENABLED
		WARN_PRINT_ONCE("Camera::unproject_position() unprojecting points on the focal plane is unreliable.");
#endif
	}
	return res;
}

Vector3 Camera3D::project_position(const Point2 &p_point, real_t p_z_depth) const {
	ERR_FAIL_COND_V_MSG(!is_inside_tree(), Vector3(), "Camera is not inside scene.");

	if (p_z_depth == 0 && mode != PROJECTION_ORTHOGONAL) {
		return get_global_transform().origin;
	}
	Size2 viewport_size = get_viewport()->get_visible_rect().size;

	Projection cm = _get_camera_projection(_near);

	Plane z_slice(Vector3(0, 0, 1), -p_z_depth);
	Vector3 res;
	z_slice.intersect_3(cm.get_projection_plane(Projection::Planes::PLANE_RIGHT), cm.get_projection_plane(Projection::Planes::PLANE_TOP), &res);
	Vector2 vp_he(res.x, res.y);

	Vector2 point;
	point.x = (p_point.x / viewport_size.x) * 2.0 - 1.0;
	point.y = (1.0 - (p_point.y / viewport_size.y)) * 2.0 - 1.0;
	PaniniMapping panini;
	if (_get_panini_mapping(cm, panini)) {
		// The rectilinear position that the resample shows at this output position. Past 90 degrees off the axis no
		// point at this depth is on the ray; the closest to the side is used.
		Vector3 dir = PaniniProjection::to_direction(point * panini.output_half, panini.distance, panini.vertical_compression);
		point = Vector2(dir.x, dir.y) / MAX(dir.z, (real_t)CMP_EPSILON) / panini.tan_half;
	}
	point *= vp_he;

	Vector3 p(point.x, point.y, -p_z_depth);

	return get_camera_transform().xform(p);
}

void Camera3D::set_environment(const Ref<Environment> &p_environment) {
	environment = p_environment;
	if (environment.is_valid()) {
		RS::get_singleton()->camera_set_environment(camera, environment->get_rid());
	} else {
		RS::get_singleton()->camera_set_environment(camera, RID());
	}
	_update_camera_mode();
}

Ref<Environment> Camera3D::get_environment() const {
	return environment;
}

void Camera3D::set_attributes(const Ref<CameraAttributes> &p_attributes) {
	if (attributes != p_attributes) {
		if (af_wrote && Engine::get_singleton()->is_editor_hint() && autofocus_mode != AUTOFOCUS_MODE_MANUAL) {
			// Leave the resource being replaced as the user set it.
			_autofocus_restore_baseline();
		}
		af_initialized = false;
		af_wrote = false;
	}
	if (attributes.is_valid()) {
		CameraAttributesPhysical *physical_attributes = Object::cast_to<CameraAttributesPhysical>(attributes.ptr());
		if (physical_attributes) {
			attributes->disconnect_changed(callable_mp(this, &Camera3D::_attributes_changed));
		}
	}

	attributes = p_attributes;

	if (attributes.is_valid()) {
		CameraAttributesPhysical *physical_attributes = Object::cast_to<CameraAttributesPhysical>(attributes.ptr());
		if (physical_attributes) {
			attributes->connect_changed(callable_mp(this, &Camera3D::_attributes_changed));
			_attributes_changed();
		}

		RS::get_singleton()->camera_set_camera_attributes(camera, attributes->get_rid());
	} else {
		RS::get_singleton()->camera_set_camera_attributes(camera, RID());
	}

	notify_property_list_changed();
}

Ref<CameraAttributes> Camera3D::get_attributes() const {
	return attributes;
}

void Camera3D::_attributes_changed() {
	CameraAttributesPhysical *physical_attributes = Object::cast_to<CameraAttributesPhysical>(attributes.ptr());
	ERR_FAIL_NULL(physical_attributes);

	fov = physical_attributes->get_fov();
	_near = physical_attributes->get_near();
	_far = physical_attributes->get_far();
	// The field of view is the sensor's vertical one, or the horizontal one for an anamorphic lens.
	const KeepAspect physical_keep_aspect = physical_attributes->is_fov_horizontal() ? KEEP_WIDTH : KEEP_HEIGHT;
	if (keep_aspect != physical_keep_aspect) {
		keep_aspect = physical_keep_aspect;
		RenderingServer::get_singleton()->camera_set_use_vertical_aspect(camera, keep_aspect == KEEP_WIDTH);
	}
	_update_camera_mode();
}

void Camera3D::set_compositor(const Ref<Compositor> &p_compositor) {
	compositor = p_compositor;
	if (compositor.is_valid()) {
		RS::get_singleton()->camera_set_compositor(camera, compositor->get_rid());
	} else {
		RS::get_singleton()->camera_set_compositor(camera, RID());
	}
	_update_camera_mode();
}

Ref<Compositor> Camera3D::get_compositor() const {
	return compositor;
}

void Camera3D::set_keep_aspect_mode(KeepAspect p_aspect) {
	keep_aspect = p_aspect;
	RenderingServer::get_singleton()->camera_set_use_vertical_aspect(camera, p_aspect == KEEP_WIDTH);
	_update_camera_mode();
	notify_property_list_changed();
}

Camera3D::KeepAspect Camera3D::get_keep_aspect_mode() const {
	return keep_aspect;
}

void Camera3D::set_doppler_tracking(DopplerTracking p_tracking) {
	if (doppler_tracking == p_tracking) {
		return;
	}

	doppler_tracking = p_tracking;
	if (p_tracking != DOPPLER_TRACKING_DISABLED) {
		velocity_tracker->set_track_physics_step(doppler_tracking == DOPPLER_TRACKING_PHYSICS_STEP);
		if (is_inside_tree()) {
			velocity_tracker->reset(get_global_transform().origin);
		}
	}
	_update_camera_mode();
}

Camera3D::DopplerTracking Camera3D::get_doppler_tracking() const {
	return doppler_tracking;
}

/* Monitoring
 *
 * The overlays a camera's monitor draws over its image. They are sent to the RenderingServer only where they should
 * show: always in the editor (the 3D viewport's camera preview and the Camera3D inspector preview both render through
 * this camera), and in a running game only with Visible in Output. Otherwise the server is told they are off, so the
 * finish pass does not run for them at all.
 */

void Camera3D::_update_monitoring() {
	const bool show = monitoring_visible_in_output || Engine::get_singleton()->is_editor_hint();
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->camera_set_monitoring_zebras(camera, show && monitoring_zebras_enabled, monitoring_zebras_level, show && monitoring_zebras_second_enabled, monitoring_zebras_second_level);
	rs->camera_set_monitoring_zebra_colors(camera, monitoring_zebras_color, monitoring_zebras_second_color);
	rs->camera_set_monitoring_focus_peaking(camera, show && monitoring_focus_peaking_enabled, monitoring_focus_peaking_color, monitoring_focus_peaking_sensitivity);
	rs->camera_set_monitoring_false_color(camera, show && monitoring_false_color_enabled);
	rs->camera_set_monitoring_histogram(camera, show && monitoring_histogram_enabled && monitoring_histogram_draw, RSE::CameraMonitoringHistogramMode(monitoring_histogram_mode), RSE::CameraMonitoringHistogramPosition(monitoring_histogram_position), monitoring_histogram_size, monitoring_histogram_opacity);
	// The readback is independent of Visible in Output, which only decides what is drawn.
	monitoring_histogram_readback_sent = _monitoring_histogram_readback_wanted();
	rs->camera_set_monitoring_histogram_readback(camera, monitoring_histogram_readback_sent, monitoring_histogram_update_interval);
	_update_process_mode();
}

/* Histogram readback
 *
 * The renderer counts the histogram on the GPU and reads it back asynchronously, at most once per update interval,
 * only while a script wants it: it called get_monitoring_histogram(), or something is connected to
 * monitoring_histogram_updated. So an enabled histogram that is only drawn (or drawn only in the editor) reads
 * nothing back, as before. The result is published by the render thread; the signal is emitted from here, on the
 * main thread, when a new one has arrived.
 */

bool Camera3D::_monitoring_histogram_readback_wanted() const {
	return monitoring_histogram_enabled && (monitoring_histogram_requested || has_connections(SNAME("monitoring_histogram_updated")));
}

void Camera3D::_monitoring_histogram_process() {
	const bool wanted = _monitoring_histogram_readback_wanted();
	if (wanted != monitoring_histogram_readback_sent) {
		_update_monitoring(); // Something connected to the signal (or disconnected).
	}
	if (!wanted) {
		return;
	}
	const uint64_t serial = RenderingServer::get_singleton()->camera_get_monitoring_histogram_serial(camera);
	if (serial != monitoring_histogram_serial) {
		monitoring_histogram_serial = serial;
		emit_signal(SNAME("monitoring_histogram_updated"));
	}
}

PackedInt32Array Camera3D::get_monitoring_histogram(MonitoringHistogramChannel p_channel) {
	ERR_FAIL_INDEX_V((int)p_channel, 4, PackedInt32Array());
	if (!monitoring_histogram_enabled) {
		return PackedInt32Array();
	}
	if (!monitoring_histogram_requested) {
		monitoring_histogram_requested = true; // The first call starts the readback; it returns empty until one lands.
		_update_monitoring();
	}
	return RenderingServer::get_singleton()->camera_get_monitoring_histogram(camera, RSE::CameraMonitoringHistogramChannel(p_channel));
}

#define MONITORING_ACCESSORS(m_type, m_set, m_get, m_member, m_list_changed, m_value) \
	void Camera3D::m_set(m_type p_value) { \
		m_member = m_value; \
		_update_monitoring(); \
		if (m_list_changed) { \
			notify_property_list_changed(); \
		} \
	} \
	m_type Camera3D::m_get() const { \
		return m_member; \
	}

MONITORING_ACCESSORS(bool, set_monitoring_visible_in_output, is_monitoring_visible_in_output, monitoring_visible_in_output, false, p_value)
MONITORING_ACCESSORS(bool, set_monitoring_zebras_enabled, is_monitoring_zebras_enabled, monitoring_zebras_enabled, true, p_value)
MONITORING_ACCESSORS(float, set_monitoring_zebras_level, get_monitoring_zebras_level, monitoring_zebras_level, false, CLAMP(p_value, 0.0f, 110.0f))
MONITORING_ACCESSORS(bool, set_monitoring_zebras_second_enabled, is_monitoring_zebras_second_enabled, monitoring_zebras_second_enabled, true, p_value)
MONITORING_ACCESSORS(float, set_monitoring_zebras_second_level, get_monitoring_zebras_second_level, monitoring_zebras_second_level, false, CLAMP(p_value, 0.0f, 110.0f))
MONITORING_ACCESSORS(bool, set_monitoring_focus_peaking_enabled, is_monitoring_focus_peaking_enabled, monitoring_focus_peaking_enabled, true, p_value)
MONITORING_ACCESSORS(float, set_monitoring_focus_peaking_sensitivity, get_monitoring_focus_peaking_sensitivity, monitoring_focus_peaking_sensitivity, false, CLAMP(p_value, 0.0f, 1.0f))
MONITORING_ACCESSORS(bool, set_monitoring_false_color_enabled, is_monitoring_false_color_enabled, monitoring_false_color_enabled, false, p_value)
MONITORING_ACCESSORS(bool, set_monitoring_histogram_enabled, is_monitoring_histogram_enabled, monitoring_histogram_enabled, true, p_value)
MONITORING_ACCESSORS(Camera3D::MonitoringHistogramMode, set_monitoring_histogram_mode, get_monitoring_histogram_mode, monitoring_histogram_mode, false, p_value)
MONITORING_ACCESSORS(Camera3D::MonitoringHistogramPosition, set_monitoring_histogram_position, get_monitoring_histogram_position, monitoring_histogram_position, false, p_value)
MONITORING_ACCESSORS(float, set_monitoring_histogram_size, get_monitoring_histogram_size, monitoring_histogram_size, false, CLAMP(p_value, 0.05f, 1.0f))
MONITORING_ACCESSORS(float, set_monitoring_histogram_opacity, get_monitoring_histogram_opacity, monitoring_histogram_opacity, false, CLAMP(p_value, 0.0f, 1.0f))
MONITORING_ACCESSORS(bool, set_monitoring_histogram_draw, is_monitoring_histogram_draw, monitoring_histogram_draw, true, p_value)
MONITORING_ACCESSORS(float, set_monitoring_histogram_update_interval, get_monitoring_histogram_update_interval, monitoring_histogram_update_interval, false, MAX(p_value, 0.0f))

#undef MONITORING_ACCESSORS

void Camera3D::set_monitoring_zebras_color(const Color &p_color) {
	monitoring_zebras_color = p_color;
	_update_monitoring();
}

Color Camera3D::get_monitoring_zebras_color() const {
	return monitoring_zebras_color;
}

void Camera3D::set_monitoring_zebras_second_color(const Color &p_color) {
	monitoring_zebras_second_color = p_color;
	_update_monitoring();
}

Color Camera3D::get_monitoring_zebras_second_color() const {
	return monitoring_zebras_second_color;
}

void Camera3D::set_monitoring_focus_peaking_color(const Color &p_color) {
	monitoring_focus_peaking_color = p_color;
	_update_monitoring();
}

Color Camera3D::get_monitoring_focus_peaking_color() const {
	return monitoring_focus_peaking_color;
}

void Camera3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("project_ray_normal", "screen_point"), &Camera3D::project_ray_normal);
	ClassDB::bind_method(D_METHOD("project_local_ray_normal", "screen_point"), &Camera3D::project_local_ray_normal);
	ClassDB::bind_method(D_METHOD("project_ray_origin", "screen_point"), &Camera3D::project_ray_origin);
	ClassDB::bind_method(D_METHOD("unproject_position", "world_point"), &Camera3D::unproject_position);
	ClassDB::bind_method(D_METHOD("is_position_behind", "world_point"), &Camera3D::is_position_behind);
	ClassDB::bind_method(D_METHOD("project_position", "screen_point", "z_depth"), &Camera3D::project_position);
	ClassDB::bind_method(D_METHOD("set_perspective", "fov", "z_near", "z_far"), &Camera3D::set_perspective);
	ClassDB::bind_method(D_METHOD("set_orthogonal", "size", "z_near", "z_far"), &Camera3D::set_orthogonal);
	ClassDB::bind_method(D_METHOD("set_frustum", "size", "offset", "z_near", "z_far"), &Camera3D::set_frustum);
	ClassDB::bind_method(D_METHOD("make_current"), &Camera3D::make_current);
	ClassDB::bind_method(D_METHOD("clear_current", "enable_next"), &Camera3D::clear_current, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("set_current", "enabled"), &Camera3D::set_current);
	ClassDB::bind_method(D_METHOD("is_current"), &Camera3D::is_current);
	ClassDB::bind_method(D_METHOD("get_camera_transform"), &Camera3D::get_camera_transform);
	ClassDB::bind_method(D_METHOD("get_camera_projection"), &Camera3D::get_camera_projection);
	ClassDB::bind_method(D_METHOD("get_fov"), &Camera3D::get_fov);
	ClassDB::bind_method(D_METHOD("get_frustum_offset"), &Camera3D::get_frustum_offset);
	ClassDB::bind_method(D_METHOD("get_size"), &Camera3D::get_size);
	ClassDB::bind_method(D_METHOD("get_far"), &Camera3D::get_far);
	ClassDB::bind_method(D_METHOD("get_near"), &Camera3D::get_near);
	ClassDB::bind_method(D_METHOD("set_fov", "fov"), &Camera3D::set_fov);
	ClassDB::bind_method(D_METHOD("set_frustum_offset", "offset"), &Camera3D::set_frustum_offset);
	ClassDB::bind_method(D_METHOD("set_size", "size"), &Camera3D::set_size);
	ClassDB::bind_method(D_METHOD("set_far", "far"), &Camera3D::set_far);
	ClassDB::bind_method(D_METHOD("set_near", "near"), &Camera3D::set_near);
	ClassDB::bind_method(D_METHOD("get_projection"), &Camera3D::get_projection);
	ClassDB::bind_method(D_METHOD("set_projection", "mode"), &Camera3D::set_projection);
	ClassDB::bind_method(D_METHOD("set_h_offset", "offset"), &Camera3D::set_h_offset);
	ClassDB::bind_method(D_METHOD("get_h_offset"), &Camera3D::get_h_offset);
	ClassDB::bind_method(D_METHOD("set_v_offset", "offset"), &Camera3D::set_v_offset);
	ClassDB::bind_method(D_METHOD("get_v_offset"), &Camera3D::get_v_offset);
	ClassDB::bind_method(D_METHOD("set_cull_mask", "mask"), &Camera3D::set_cull_mask);
	ClassDB::bind_method(D_METHOD("get_cull_mask"), &Camera3D::get_cull_mask);
	ClassDB::bind_method(D_METHOD("set_environment", "env"), &Camera3D::set_environment);
	ClassDB::bind_method(D_METHOD("get_environment"), &Camera3D::get_environment);
	ClassDB::bind_method(D_METHOD("set_attributes", "env"), &Camera3D::set_attributes);
	ClassDB::bind_method(D_METHOD("get_attributes"), &Camera3D::get_attributes);
	ClassDB::bind_method(D_METHOD("set_compositor", "compositor"), &Camera3D::set_compositor);
	ClassDB::bind_method(D_METHOD("get_compositor"), &Camera3D::get_compositor);
	ClassDB::bind_method(D_METHOD("set_keep_aspect_mode", "mode"), &Camera3D::set_keep_aspect_mode);
	ClassDB::bind_method(D_METHOD("get_keep_aspect_mode"), &Camera3D::get_keep_aspect_mode);
	ClassDB::bind_method(D_METHOD("set_doppler_tracking", "mode"), &Camera3D::set_doppler_tracking);
	ClassDB::bind_method(D_METHOD("get_doppler_tracking"), &Camera3D::get_doppler_tracking);
	ClassDB::bind_method(D_METHOD("get_frustum"), &Camera3D::_get_frustum);
	ClassDB::bind_method(D_METHOD("is_position_in_frustum", "world_point"), &Camera3D::is_position_in_frustum);
	ClassDB::bind_method(D_METHOD("get_camera_rid"), &Camera3D::get_camera);
#ifndef PHYSICS_3D_DISABLED
	ClassDB::bind_method(D_METHOD("get_pyramid_shape_rid"), &Camera3D::get_pyramid_shape_rid);
#endif // PHYSICS_3D_DISABLED

	ClassDB::bind_method(D_METHOD("set_cull_mask_value", "layer_number", "value"), &Camera3D::set_cull_mask_value);
	ClassDB::bind_method(D_METHOD("get_cull_mask_value", "layer_number"), &Camera3D::get_cull_mask_value);

	//ClassDB::bind_method(D_METHOD("_camera_make_current"),&Camera::_camera_make_current );

	ClassDB::bind_method(D_METHOD("set_monitoring_visible_in_output", "visible"), &Camera3D::set_monitoring_visible_in_output);
	ClassDB::bind_method(D_METHOD("is_monitoring_visible_in_output"), &Camera3D::is_monitoring_visible_in_output);
	ClassDB::bind_method(D_METHOD("set_monitoring_zebras_enabled", "enabled"), &Camera3D::set_monitoring_zebras_enabled);
	ClassDB::bind_method(D_METHOD("is_monitoring_zebras_enabled"), &Camera3D::is_monitoring_zebras_enabled);
	ClassDB::bind_method(D_METHOD("set_monitoring_zebras_level", "level"), &Camera3D::set_monitoring_zebras_level);
	ClassDB::bind_method(D_METHOD("get_monitoring_zebras_level"), &Camera3D::get_monitoring_zebras_level);
	ClassDB::bind_method(D_METHOD("set_monitoring_zebras_second_enabled", "enabled"), &Camera3D::set_monitoring_zebras_second_enabled);
	ClassDB::bind_method(D_METHOD("is_monitoring_zebras_second_enabled"), &Camera3D::is_monitoring_zebras_second_enabled);
	ClassDB::bind_method(D_METHOD("set_monitoring_zebras_second_level", "level"), &Camera3D::set_monitoring_zebras_second_level);
	ClassDB::bind_method(D_METHOD("get_monitoring_zebras_second_level"), &Camera3D::get_monitoring_zebras_second_level);
	ClassDB::bind_method(D_METHOD("set_monitoring_focus_peaking_enabled", "enabled"), &Camera3D::set_monitoring_focus_peaking_enabled);
	ClassDB::bind_method(D_METHOD("is_monitoring_focus_peaking_enabled"), &Camera3D::is_monitoring_focus_peaking_enabled);
	ClassDB::bind_method(D_METHOD("set_monitoring_zebras_color", "color"), &Camera3D::set_monitoring_zebras_color);
	ClassDB::bind_method(D_METHOD("get_monitoring_zebras_color"), &Camera3D::get_monitoring_zebras_color);
	ClassDB::bind_method(D_METHOD("set_monitoring_zebras_second_color", "color"), &Camera3D::set_monitoring_zebras_second_color);
	ClassDB::bind_method(D_METHOD("get_monitoring_zebras_second_color"), &Camera3D::get_monitoring_zebras_second_color);
	ClassDB::bind_method(D_METHOD("set_monitoring_focus_peaking_color", "color"), &Camera3D::set_monitoring_focus_peaking_color);
	ClassDB::bind_method(D_METHOD("get_monitoring_focus_peaking_color"), &Camera3D::get_monitoring_focus_peaking_color);
	ClassDB::bind_method(D_METHOD("set_monitoring_focus_peaking_sensitivity", "sensitivity"), &Camera3D::set_monitoring_focus_peaking_sensitivity);
	ClassDB::bind_method(D_METHOD("get_monitoring_focus_peaking_sensitivity"), &Camera3D::get_monitoring_focus_peaking_sensitivity);
	ClassDB::bind_method(D_METHOD("set_monitoring_false_color_enabled", "enabled"), &Camera3D::set_monitoring_false_color_enabled);
	ClassDB::bind_method(D_METHOD("is_monitoring_false_color_enabled"), &Camera3D::is_monitoring_false_color_enabled);
	ClassDB::bind_method(D_METHOD("set_monitoring_histogram_enabled", "enabled"), &Camera3D::set_monitoring_histogram_enabled);
	ClassDB::bind_method(D_METHOD("is_monitoring_histogram_enabled"), &Camera3D::is_monitoring_histogram_enabled);
	ClassDB::bind_method(D_METHOD("set_monitoring_histogram_mode", "mode"), &Camera3D::set_monitoring_histogram_mode);
	ClassDB::bind_method(D_METHOD("get_monitoring_histogram_mode"), &Camera3D::get_monitoring_histogram_mode);
	ClassDB::bind_method(D_METHOD("set_monitoring_histogram_position", "position"), &Camera3D::set_monitoring_histogram_position);
	ClassDB::bind_method(D_METHOD("get_monitoring_histogram_position"), &Camera3D::get_monitoring_histogram_position);
	ClassDB::bind_method(D_METHOD("set_monitoring_histogram_size", "size"), &Camera3D::set_monitoring_histogram_size);
	ClassDB::bind_method(D_METHOD("get_monitoring_histogram_size"), &Camera3D::get_monitoring_histogram_size);
	ClassDB::bind_method(D_METHOD("set_monitoring_histogram_opacity", "opacity"), &Camera3D::set_monitoring_histogram_opacity);
	ClassDB::bind_method(D_METHOD("get_monitoring_histogram_opacity"), &Camera3D::get_monitoring_histogram_opacity);
	ClassDB::bind_method(D_METHOD("set_monitoring_histogram_draw", "draw"), &Camera3D::set_monitoring_histogram_draw);
	ClassDB::bind_method(D_METHOD("is_monitoring_histogram_draw"), &Camera3D::is_monitoring_histogram_draw);
	ClassDB::bind_method(D_METHOD("set_monitoring_histogram_update_interval", "interval"), &Camera3D::set_monitoring_histogram_update_interval);
	ClassDB::bind_method(D_METHOD("get_monitoring_histogram_update_interval"), &Camera3D::get_monitoring_histogram_update_interval);
	ClassDB::bind_method(D_METHOD("get_monitoring_histogram", "channel"), &Camera3D::get_monitoring_histogram, DEFVAL(MONITORING_HISTOGRAM_CHANNEL_LUMA));

	ADD_PROPERTY(PropertyInfo(Variant::INT, "keep_aspect", PROPERTY_HINT_ENUM, "Keep Width,Keep Height"), "set_keep_aspect_mode", "get_keep_aspect_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cull_mask", PROPERTY_HINT_LAYERS_3D_RENDER), "set_cull_mask", "get_cull_mask");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "environment", PROPERTY_HINT_RESOURCE_TYPE, Environment::get_class_static()), "set_environment", "get_environment");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "attributes", PROPERTY_HINT_RESOURCE_TYPE, "CameraAttributesPractical,CameraAttributesPhysical"), "set_attributes", "get_attributes");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "compositor", PROPERTY_HINT_RESOURCE_TYPE, Compositor::get_class_static()), "set_compositor", "get_compositor");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "h_offset", PROPERTY_HINT_NONE, "suffix:m"), "set_h_offset", "get_h_offset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "v_offset", PROPERTY_HINT_NONE, "suffix:m"), "set_v_offset", "get_v_offset");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "doppler_tracking", PROPERTY_HINT_ENUM, "Disabled,Idle,Physics"), "set_doppler_tracking", "get_doppler_tracking");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "projection", PROPERTY_HINT_ENUM, "Perspective,Orthogonal,Frustum"), "set_projection", "get_projection");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "current"), "set_current", "is_current");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fov", PROPERTY_HINT_RANGE, "1,179,0.1,degrees"), "set_fov", "get_fov");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "size", PROPERTY_HINT_RANGE, "0.001,100,0.001,or_greater,suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "frustum_offset", PROPERTY_HINT_NONE, "suffix:m"), "set_frustum_offset", "get_frustum_offset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "near", PROPERTY_HINT_RANGE, "0.001,10,0.001,or_greater,exp,suffix:m"), "set_near", "get_near");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "far", PROPERTY_HINT_RANGE, "0.01,4000,0.01,or_greater,exp,suffix:m"), "set_far", "get_far");

	ADD_GROUP("Monitoring", "monitoring_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "monitoring_visible_in_output"), "set_monitoring_visible_in_output", "is_monitoring_visible_in_output");
	ADD_SUBGROUP("Zebras", "monitoring_zebras_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "monitoring_zebras_enabled"), "set_monitoring_zebras_enabled", "is_monitoring_zebras_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "monitoring_zebras_level", PROPERTY_HINT_RANGE, "0,109,0.5,suffix:IRE"), "set_monitoring_zebras_level", "get_monitoring_zebras_level");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "monitoring_zebras_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_monitoring_zebras_color", "get_monitoring_zebras_color");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "monitoring_zebras_second_enabled"), "set_monitoring_zebras_second_enabled", "is_monitoring_zebras_second_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "monitoring_zebras_second_level", PROPERTY_HINT_RANGE, "0,109,0.5,suffix:IRE"), "set_monitoring_zebras_second_level", "get_monitoring_zebras_second_level");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "monitoring_zebras_second_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_monitoring_zebras_second_color", "get_monitoring_zebras_second_color");
	ADD_SUBGROUP("Focus Peaking", "monitoring_focus_peaking_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "monitoring_focus_peaking_enabled"), "set_monitoring_focus_peaking_enabled", "is_monitoring_focus_peaking_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "monitoring_focus_peaking_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_monitoring_focus_peaking_color", "get_monitoring_focus_peaking_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "monitoring_focus_peaking_sensitivity", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_monitoring_focus_peaking_sensitivity", "get_monitoring_focus_peaking_sensitivity");
	ADD_SUBGROUP("False Color", "monitoring_false_color_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "monitoring_false_color_enabled"), "set_monitoring_false_color_enabled", "is_monitoring_false_color_enabled");
	ADD_SUBGROUP("Histogram", "monitoring_histogram_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "monitoring_histogram_enabled"), "set_monitoring_histogram_enabled", "is_monitoring_histogram_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "monitoring_histogram_draw"), "set_monitoring_histogram_draw", "is_monitoring_histogram_draw");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "monitoring_histogram_mode", PROPERTY_HINT_ENUM, "Luma,RGB"), "set_monitoring_histogram_mode", "get_monitoring_histogram_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "monitoring_histogram_position", PROPERTY_HINT_ENUM, "Top Left,Top Right,Bottom Left,Bottom Right"), "set_monitoring_histogram_position", "get_monitoring_histogram_position");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "monitoring_histogram_size", PROPERTY_HINT_RANGE, "0.05,1,0.01"), "set_monitoring_histogram_size", "get_monitoring_histogram_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "monitoring_histogram_opacity", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_monitoring_histogram_opacity", "get_monitoring_histogram_opacity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "monitoring_histogram_update_interval", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_monitoring_histogram_update_interval", "get_monitoring_histogram_update_interval");

	BIND_ENUM_CONSTANT(PROJECTION_PERSPECTIVE);
	BIND_ENUM_CONSTANT(PROJECTION_ORTHOGONAL);
	BIND_ENUM_CONSTANT(PROJECTION_FRUSTUM);

	BIND_ENUM_CONSTANT(KEEP_WIDTH);
	BIND_ENUM_CONSTANT(KEEP_HEIGHT);

	BIND_ENUM_CONSTANT(DOPPLER_TRACKING_DISABLED);
	BIND_ENUM_CONSTANT(DOPPLER_TRACKING_IDLE_STEP);
	BIND_ENUM_CONSTANT(DOPPLER_TRACKING_PHYSICS_STEP);

	/* Autofocus */

	ClassDB::bind_method(D_METHOD("set_autofocus_mode", "mode"), &Camera3D::set_autofocus_mode);
	ClassDB::bind_method(D_METHOD("get_autofocus_mode"), &Camera3D::get_autofocus_mode);
	ClassDB::bind_method(D_METHOD("set_autofocus_target", "target"), &Camera3D::set_autofocus_target);
	ClassDB::bind_method(D_METHOD("get_autofocus_target"), &Camera3D::get_autofocus_target);
	ClassDB::bind_method(D_METHOD("set_autofocus_points", "points"), &Camera3D::set_autofocus_points);
	ClassDB::bind_method(D_METHOD("get_autofocus_points"), &Camera3D::get_autofocus_points);
	ClassDB::bind_method(D_METHOD("set_autofocus_collision_mask", "mask"), &Camera3D::set_autofocus_collision_mask);
	ClassDB::bind_method(D_METHOD("get_autofocus_collision_mask"), &Camera3D::get_autofocus_collision_mask);
	ClassDB::bind_method(D_METHOD("set_autofocus_time_constant", "seconds"), &Camera3D::set_autofocus_time_constant);
	ClassDB::bind_method(D_METHOD("get_autofocus_time_constant"), &Camera3D::get_autofocus_time_constant);
	ClassDB::bind_method(D_METHOD("set_autofocus_hunting", "overshoot"), &Camera3D::set_autofocus_hunting);
	ClassDB::bind_method(D_METHOD("get_autofocus_hunting"), &Camera3D::get_autofocus_hunting);
	ClassDB::bind_method(D_METHOD("autofocus_trigger"), &Camera3D::autofocus_trigger);
	ClassDB::bind_method(D_METHOD("get_autofocus_distance"), &Camera3D::get_autofocus_distance);
	ClassDB::bind_method(D_METHOD("is_autofocus_acquired"), &Camera3D::is_autofocus_acquired);

	ADD_GROUP("Autofocus", "autofocus_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "autofocus_mode", PROPERTY_HINT_ENUM, "Manual,Single,Continuous"), "set_autofocus_mode", "get_autofocus_mode");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "autofocus_target", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node3D"), "set_autofocus_target", "get_autofocus_target");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "autofocus_points"), "set_autofocus_points", "get_autofocus_points");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "autofocus_collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_autofocus_collision_mask", "get_autofocus_collision_mask");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "autofocus_time_constant", PROPERTY_HINT_RANGE, "0.001,2,0.001,or_greater,suffix:s"), "set_autofocus_time_constant", "get_autofocus_time_constant");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "autofocus_hunting", PROPERTY_HINT_RANGE, "0,0.9,0.01"), "set_autofocus_hunting", "get_autofocus_hunting");

	ADD_SIGNAL(MethodInfo("focus_acquired", PropertyInfo(Variant::FLOAT, "distance")));
	ADD_SIGNAL(MethodInfo("focus_failed"));
	ADD_SIGNAL(MethodInfo("monitoring_histogram_updated"));

	BIND_ENUM_CONSTANT(AUTOFOCUS_MODE_MANUAL);
	BIND_ENUM_CONSTANT(AUTOFOCUS_MODE_SINGLE);
	BIND_ENUM_CONSTANT(AUTOFOCUS_MODE_CONTINUOUS);

	/* Handheld shake */

	ClassDB::bind_method(D_METHOD("set_shake_enabled", "enabled"), &Camera3D::set_shake_enabled);
	ClassDB::bind_method(D_METHOD("is_shake_enabled"), &Camera3D::is_shake_enabled);
	ClassDB::bind_method(D_METHOD("set_shake_seed", "seed"), &Camera3D::set_shake_seed);
	ClassDB::bind_method(D_METHOD("get_shake_seed"), &Camera3D::get_shake_seed);
	ClassDB::bind_method(D_METHOD("set_shake_rotation_amplitude", "degrees"), &Camera3D::set_shake_rotation_amplitude);
	ClassDB::bind_method(D_METHOD("get_shake_rotation_amplitude"), &Camera3D::get_shake_rotation_amplitude);
	ClassDB::bind_method(D_METHOD("set_shake_rotation_frequency", "hz"), &Camera3D::set_shake_rotation_frequency);
	ClassDB::bind_method(D_METHOD("get_shake_rotation_frequency"), &Camera3D::get_shake_rotation_frequency);
	ClassDB::bind_method(D_METHOD("set_shake_position_amplitude", "meters"), &Camera3D::set_shake_position_amplitude);
	ClassDB::bind_method(D_METHOD("get_shake_position_amplitude"), &Camera3D::get_shake_position_amplitude);
	ClassDB::bind_method(D_METHOD("set_shake_position_frequency", "hz"), &Camera3D::set_shake_position_frequency);
	ClassDB::bind_method(D_METHOD("get_shake_position_frequency"), &Camera3D::get_shake_position_frequency);
	ClassDB::bind_method(D_METHOD("set_shake_breathing_amplitude", "degrees"), &Camera3D::set_shake_breathing_amplitude);
	ClassDB::bind_method(D_METHOD("get_shake_breathing_amplitude"), &Camera3D::get_shake_breathing_amplitude);
	ClassDB::bind_method(D_METHOD("set_shake_breathing_frequency", "hz"), &Camera3D::set_shake_breathing_frequency);
	ClassDB::bind_method(D_METHOD("get_shake_breathing_frequency"), &Camera3D::get_shake_breathing_frequency);
	ClassDB::bind_method(D_METHOD("set_shake_stabilization", "strength"), &Camera3D::set_shake_stabilization);
	ClassDB::bind_method(D_METHOD("get_shake_stabilization"), &Camera3D::get_shake_stabilization);
	ClassDB::bind_method(D_METHOD("set_shake_scale_with_focal_length", "enabled"), &Camera3D::set_shake_scale_with_focal_length);
	ClassDB::bind_method(D_METHOD("is_shake_scale_with_focal_length"), &Camera3D::is_shake_scale_with_focal_length);
	ClassDB::bind_method(D_METHOD("get_shake_offset"), &Camera3D::get_shake_offset);
	ClassDB::bind_method(D_METHOD("set_shake_visible_in_editor", "visible"), &Camera3D::set_shake_visible_in_editor);
	ClassDB::bind_method(D_METHOD("is_shake_visible_in_editor"), &Camera3D::is_shake_visible_in_editor);
	ClassDB::bind_method(D_METHOD("set_shake_preset", "preset"), &Camera3D::set_shake_preset);
	ClassDB::bind_method(D_METHOD("get_shake_preset"), &Camera3D::get_shake_preset);

	ADD_GROUP("Handheld Shake", "shake_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shake_enabled"), "set_shake_enabled", "is_shake_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shake_visible_in_editor"), "set_shake_visible_in_editor", "is_shake_visible_in_editor");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shake_preset", PROPERTY_HINT_ENUM, "Custom,Handheld,Handheld Stabilized,Handheld Walking,Running,Shoulder Rig,Gimbal,Tripod in Wind,Vehicle Mount"), "set_shake_preset", "get_shake_preset");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shake_seed"), "set_shake_seed", "get_shake_seed");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "shake_rotation_amplitude", PROPERTY_HINT_RANGE, "0,5,0.001,or_greater,degrees"), "set_shake_rotation_amplitude", "get_shake_rotation_amplitude");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "shake_rotation_frequency", PROPERTY_HINT_RANGE, "0,20,0.01,or_greater,suffix:Hz"), "set_shake_rotation_frequency", "get_shake_rotation_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "shake_position_amplitude", PROPERTY_HINT_RANGE, "0,0.1,0.0001,or_greater,suffix:m"), "set_shake_position_amplitude", "get_shake_position_amplitude");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "shake_position_frequency", PROPERTY_HINT_RANGE, "0,20,0.01,or_greater,suffix:Hz"), "set_shake_position_frequency", "get_shake_position_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shake_breathing_amplitude", PROPERTY_HINT_RANGE, "0,5,0.001,or_greater,degrees"), "set_shake_breathing_amplitude", "get_shake_breathing_amplitude");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shake_breathing_frequency", PROPERTY_HINT_RANGE, "0,2,0.01,or_greater,suffix:Hz"), "set_shake_breathing_frequency", "get_shake_breathing_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shake_stabilization", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_shake_stabilization", "get_shake_stabilization");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shake_scale_with_focal_length"), "set_shake_scale_with_focal_length", "is_shake_scale_with_focal_length");

	BIND_ENUM_CONSTANT(SHAKE_PRESET_CUSTOM);
	BIND_ENUM_CONSTANT(SHAKE_PRESET_HANDHELD);
	BIND_ENUM_CONSTANT(SHAKE_PRESET_HANDHELD_STABILIZED);
	BIND_ENUM_CONSTANT(SHAKE_PRESET_HANDHELD_WALKING);
	BIND_ENUM_CONSTANT(SHAKE_PRESET_RUNNING);
	BIND_ENUM_CONSTANT(SHAKE_PRESET_SHOULDER_RIG);
	BIND_ENUM_CONSTANT(SHAKE_PRESET_GIMBAL);
	BIND_ENUM_CONSTANT(SHAKE_PRESET_TRIPOD_WIND);
	BIND_ENUM_CONSTANT(SHAKE_PRESET_VEHICLE_MOUNT);
	BIND_ENUM_CONSTANT(SHAKE_PRESET_MAX);

	BIND_ENUM_CONSTANT(MONITORING_HISTOGRAM_LUMA);
	BIND_ENUM_CONSTANT(MONITORING_HISTOGRAM_RGB);
	BIND_ENUM_CONSTANT(MONITORING_HISTOGRAM_TOP_LEFT);
	BIND_ENUM_CONSTANT(MONITORING_HISTOGRAM_TOP_RIGHT);
	BIND_ENUM_CONSTANT(MONITORING_HISTOGRAM_BOTTOM_LEFT);
	BIND_ENUM_CONSTANT(MONITORING_HISTOGRAM_BOTTOM_RIGHT);
	BIND_ENUM_CONSTANT(MONITORING_HISTOGRAM_CHANNEL_LUMA);
	BIND_ENUM_CONSTANT(MONITORING_HISTOGRAM_CHANNEL_RED);
	BIND_ENUM_CONSTANT(MONITORING_HISTOGRAM_CHANNEL_GREEN);
	BIND_ENUM_CONSTANT(MONITORING_HISTOGRAM_CHANNEL_BLUE);
}

real_t Camera3D::get_fov() const {
	return fov;
}

real_t Camera3D::get_size() const {
	return size;
}

real_t Camera3D::get_near() const {
	return _near;
}

Vector2 Camera3D::get_frustum_offset() const {
	return frustum_offset;
}

real_t Camera3D::get_far() const {
	return _far;
}

Camera3D::ProjectionType Camera3D::get_projection() const {
	return mode;
}

void Camera3D::set_fov(real_t p_fov) {
	ERR_FAIL_COND(p_fov <= CMP_EPSILON || p_fov >= 180.0 - CMP_EPSILON);
	fov = p_fov;
	_update_camera_mode();
}

void Camera3D::set_size(real_t p_size) {
	ERR_FAIL_COND(p_size <= CMP_EPSILON);
	size = p_size;
	_update_camera_mode();
}

void Camera3D::set_near(real_t p_near) {
	_near = p_near;
	_update_camera_mode();
}

void Camera3D::set_frustum_offset(Vector2 p_offset) {
	frustum_offset = p_offset;
	_update_camera_mode();
}

void Camera3D::set_far(real_t p_far) {
	_far = p_far;
	_update_camera_mode();
}

void Camera3D::set_cull_mask(uint32_t p_layers) {
	layers = p_layers;
	RenderingServer::get_singleton()->camera_set_cull_mask(camera, layers);
	_update_camera_mode();
}

uint32_t Camera3D::get_cull_mask() const {
	return layers;
}

void Camera3D::set_cull_mask_value(int p_layer_number, bool p_value) {
	ERR_FAIL_COND_MSG(p_layer_number < 1, "Render layer number must be between 1 and 20 inclusive.");
	ERR_FAIL_COND_MSG(p_layer_number > 20, "Render layer number must be between 1 and 20 inclusive.");
	uint32_t mask = get_cull_mask();
	if (p_value) {
		mask |= 1 << (p_layer_number - 1);
	} else {
		mask &= ~(1 << (p_layer_number - 1));
	}
	set_cull_mask(mask);
}

bool Camera3D::get_cull_mask_value(int p_layer_number) const {
	ERR_FAIL_COND_V_MSG(p_layer_number < 1, false, "Render layer number must be between 1 and 20 inclusive.");
	ERR_FAIL_COND_V_MSG(p_layer_number > 20, false, "Render layer number must be between 1 and 20 inclusive.");
	return layers & (1 << (p_layer_number - 1));
}

Vector<Plane> Camera3D::get_frustum() const {
	ERR_FAIL_COND_V(!is_inside_world(), Vector<Plane>());

	Projection cm = _get_camera_projection(_near);

	return cm.get_projection_planes(get_camera_transform());
}

TypedArray<Plane> Camera3D::_get_frustum() const {
	Variant ret = get_frustum();
	return ret;
}

bool Camera3D::is_position_in_frustum(const Vector3 &p_position) const {
	Vector<Plane> frustum = get_frustum();
	for (int i = 0; i < frustum.size(); i++) {
		if (frustum[i].is_point_over(p_position)) {
			return false;
		}
	}
	return true;
}

void Camera3D::set_v_offset(real_t p_offset) {
	v_offset = p_offset;
	_update_camera();
}

real_t Camera3D::get_v_offset() const {
	return v_offset;
}

void Camera3D::set_h_offset(real_t p_offset) {
	h_offset = p_offset;
	_update_camera();
}

real_t Camera3D::get_h_offset() const {
	return h_offset;
}

Vector3 Camera3D::get_doppler_tracked_velocity() const {
	if (doppler_tracking != DOPPLER_TRACKING_DISABLED) {
		return velocity_tracker->get_tracked_linear_velocity();
	} else {
		return Vector3();
	}
}

#ifndef PHYSICS_3D_DISABLED
RID Camera3D::get_pyramid_shape_rid() {
	ERR_FAIL_COND_V_MSG(!is_inside_tree(), RID(), "Camera is not inside scene.");
	if (pyramid_shape == RID()) {
		pyramid_shape_points = get_near_plane_points();
		pyramid_shape = PhysicsServer3D::get_singleton()->convex_polygon_shape_create();
		PhysicsServer3D::get_singleton()->shape_set_data(pyramid_shape, pyramid_shape_points);

	} else { //check if points changed
		Vector<Vector3> local_points = get_near_plane_points();

		bool all_equal = true;

		for (int i = 0; i < 5; i++) {
			if (local_points[i] != pyramid_shape_points[i]) {
				all_equal = false;
				break;
			}
		}

		if (!all_equal) {
			PhysicsServer3D::get_singleton()->shape_set_data(pyramid_shape, local_points);
			pyramid_shape_points = local_points;
		}
	}

	return pyramid_shape;
}
#endif // PHYSICS_3D_DISABLED

/* Autofocus */

// The servo, the acquisition test and the Practical depth-of-field mapping all work in diopters (1 / distance in
// meters). Two facts make that the natural space: a lens's focusing travel is close to linear in diopters (thin lens:
// image distance v = 1 / (1/f - 1/u), so dv ~ f^2 d(1/u) for u >> f), and the depth of field around the focus distance u
// is close to symmetric and constant in diopters, 1/near = 1/u + 1/H and 1/far = 1/u - 1/H with H the hyperfocal
// distance (https://en.wikipedia.org/wiki/Depth_of_field#Hyperfocal_distance, valid for u >> f).

static constexpr double AF_MIN_DISTANCE = 0.01; // Meters. Closer than any lens focuses; keeps 1/d finite.
static constexpr double AF_MAX_DISTANCE = 1.0e5; // Meters. "Infinity" for a distance written to a resource.

void Camera3D::_autofocus_capture_baseline() {
	CameraAttributesPhysical *physical = Object::cast_to<CameraAttributesPhysical>(attributes.ptr());
	CameraAttributesPractical *practical = Object::cast_to<CameraAttributesPractical>(attributes.ptr());
	if (physical) {
		af_base_focus_distance = physical->get_focus_distance();
		af_written[0] = af_base_focus_distance;
	} else if (practical) {
		af_base_near = practical->get_dof_blur_near_distance();
		af_base_far = practical->get_dof_blur_far_distance();
		af_base_near_transition = practical->get_dof_blur_near_transition();
		af_base_far_transition = practical->get_dof_blur_far_transition();
		af_written[0] = af_base_near;
		af_written[1] = af_base_far;
		af_written[2] = af_base_near_transition;
		af_written[3] = af_base_far_transition;
	}
}

bool Camera3D::_autofocus_baseline_changed() const {
	CameraAttributesPhysical *physical = Object::cast_to<CameraAttributesPhysical>(attributes.ptr());
	CameraAttributesPractical *practical = Object::cast_to<CameraAttributesPractical>(attributes.ptr());
	if (physical) {
		return physical->get_focus_distance() != af_written[0];
	} else if (practical) {
		return practical->get_dof_blur_near_distance() != af_written[0] || practical->get_dof_blur_far_distance() != af_written[1] ||
				practical->get_dof_blur_near_transition() != af_written[2] || practical->get_dof_blur_far_transition() != af_written[3];
	}
	return false;
}

// The baseline's in-focus band for CameraAttributesPractical, as a center and half width in diopters. Practical has no
// focus distance; the band between the near and far blur distances stands for the depth of field of a lens focused at
// its diopter midpoint. Returns false when neither blur is enabled (nothing to move).
static bool _af_practical_band(const CameraAttributesPractical *p_practical, double p_near, double p_far, double &r_center, double &r_half) {
	const bool use_near = p_practical->is_dof_blur_near_enabled();
	const bool use_far = p_practical->is_dof_blur_far_enabled();
	const double near_d = 1.0 / MAX((double)p_near, AF_MIN_DISTANCE);
	const double far_d = 1.0 / MAX((double)p_far, AF_MIN_DISTANCE);
	if (use_near && use_far) {
		r_center = 0.5 * (near_d + far_d);
		r_half = 0.5 * Math::abs(near_d - far_d);
	} else if (use_near) {
		// Far limit at infinity: the hyperfocal case, focused at twice the near limit.
		r_center = 0.5 * near_d;
		r_half = 0.5 * near_d;
	} else if (use_far) {
		// No near limit to pair with; take the focus at half the far distance (a convention, see the class docs).
		r_center = 2.0 * far_d;
		r_half = far_d;
	} else {
		return false;
	}
	return true;
}

void Camera3D::_autofocus_reset() {
	af_initialized = true;
	af_velocity = 0.0;
	af_has_target = false;
	af_single_running = false;
	af_acquired = false;
	af_wrote = false;

	double distance = 10.0; // Without attributes, start where CameraAttributesPhysical's default focus distance is.
	CameraAttributesPhysical *physical = Object::cast_to<CameraAttributesPhysical>(attributes.ptr());
	CameraAttributesPractical *practical = Object::cast_to<CameraAttributesPractical>(attributes.ptr());
	_autofocus_capture_baseline();
	if (physical) {
		distance = af_base_focus_distance;
	} else if (practical) {
		double center, half;
		if (_af_practical_band(practical, af_base_near, af_base_far, center, half)) {
			distance = 1.0 / MAX(center, 1e-5);
		}
	}
	af_diopters = 1.0 / CLAMP(distance, AF_MIN_DISTANCE, AF_MAX_DISTANCE);
	af_target_diopters = af_diopters;
}

double Camera3D::_autofocus_tolerance() const {
	// Focus counts as acquired once the error is within a quarter of the depth of field's half width, so the subject
	// sits well inside the sharp zone, not on its edge (a look choice; real AF systems quote accuracy as a fraction of
	// the depth of focus). The floor keeps a deep depth of field from making acquisition meaningless.
	double half_width = 0.02;
	CameraAttributesPhysical *physical = Object::cast_to<CameraAttributesPhysical>(attributes.ptr());
	CameraAttributesPractical *practical = Object::cast_to<CameraAttributesPractical>(attributes.ptr());
	if (physical) {
		// Same circle of confusion as CameraAttributesPhysical::_update_frustum() (36x24 mm sensor, diagonal / 1500).
		const double coc = Math::sqrt(36.0 * 36.0 + 24.0 * 24.0) / 1500.0;
		const double f = physical->get_focal_length();
		const double hyperfocal_m = (f + f * f / (MAX((double)physical->get_aperture(), 0.1) * coc)) / 1000.0;
		half_width = 1.0 / hyperfocal_m;
	} else if (practical) {
		double center, half;
		if (_af_practical_band(practical, af_base_near, af_base_far, center, half)) {
			half_width = half;
		}
	}
	return MIN(MAX(0.25 * half_width, 1e-4), 0.25);
}

bool Camera3D::_autofocus_measure(double &r_diopters) const {
	if (!is_inside_tree()) {
		return false;
	}
	const Transform3D xform = get_camera_transform();
	const Vector3 forward = -xform.basis.get_column(2).normalized();

	// A tracked node: its distance along the optical axis (the plane of focus is perpendicular to it).
	if (!autofocus_target.is_empty()) {
		const Node3D *target = Object::cast_to<Node3D>(get_node_or_null(autofocus_target));
		if (target) {
			const double depth = forward.dot(target->get_global_position() - xform.origin);
			if (depth <= 0.0) {
				return false;
			}
			r_diopters = 1.0 / CLAMP(depth, AF_MIN_DISTANCE, AF_MAX_DISTANCE);
			return true;
		}
	}

#ifndef PHYSICS_3D_DISABLED
	// Screen points: a ray per point, like a laser or phase-detect sensor measuring distance; the nearest hit wins
	// (closest-subject priority of zone AF).
	Ref<World3D> world = get_world_3d();
	if (world.is_null() || autofocus_points.is_empty()) {
		return false;
	}
	PhysicsDirectSpaceState3D *space = PhysicsServer3D::get_singleton()->space_get_direct_state(world->get_space());
	ERR_FAIL_NULL_V(space, false);

	const Projection inverse = _get_camera_projection(_near).inverse();
	double nearest = Math::INF;
	for (const Vector2 &point : autofocus_points) {
		// Normalized (0..1, y down) to clip space, then unprojected to the near and far planes: correct for every
		// projection mode, frustum offset included.
		const Vector2 ndc(point.x * 2.0 - 1.0, 1.0 - point.y * 2.0);
		PS3DT::RayParameters ray;
		ray.from = xform.xform(inverse.xform(Vector3(ndc.x, ndc.y, -1.0)));
		ray.to = xform.xform(inverse.xform(Vector3(ndc.x, ndc.y, 1.0)));
		ray.collision_mask = autofocus_collision_mask;
		PS3DT::RayResult hit;
		if (space->intersect_ray(ray, hit)) {
			const double depth = forward.dot(hit.position - xform.origin);
			if (depth > 0.0) {
				nearest = MIN(nearest, depth);
			}
		}
	}
	if (nearest == Math::INF) {
		return false;
	}
	r_diopters = 1.0 / CLAMP(nearest, AF_MIN_DISTANCE, AF_MAX_DISTANCE);
	return true;
#else
	return false;
#endif // PHYSICS_3D_DISABLED
}

void Camera3D::_autofocus_restore_baseline() {
	CameraAttributesPhysical *physical = Object::cast_to<CameraAttributesPhysical>(attributes.ptr());
	CameraAttributesPractical *practical = Object::cast_to<CameraAttributesPractical>(attributes.ptr());
	if (physical) {
		physical->set_focus_distance(af_base_focus_distance);
	} else if (practical) {
		practical->set_dof_blur_near_distance(af_base_near);
		practical->set_dof_blur_far_distance(af_base_far);
		practical->set_dof_blur_near_transition(af_base_near_transition);
		practical->set_dof_blur_far_transition(af_base_far_transition);
	}
}

void Camera3D::_autofocus_apply(double p_distance) {
	CameraAttributesPhysical *physical = Object::cast_to<CameraAttributesPhysical>(attributes.ptr());
	CameraAttributesPractical *practical = Object::cast_to<CameraAttributesPractical>(attributes.ptr());
	const double distance = CLAMP(p_distance, AF_MIN_DISTANCE, AF_MAX_DISTANCE);
	if (physical) {
		const float value = distance;
		if (physical->get_focus_distance() != value) {
			physical->set_focus_distance(value);
		}
		af_written[0] = value;
		af_wrote = true;
	} else if (practical) {
		double center, half;
		if (!_af_practical_band(practical, af_base_near, af_base_far, center, half)) {
			return;
		}
		// Move the band to the new focus keeping its width in diopters, as a real lens's depth of field does at a fixed
		// aperture and focal length. The transitions keep their diopter widths too.
		const double c = 1.0 / distance;
		float near = af_base_near, far = af_base_far;
		float near_transition = af_base_near_transition, far_transition = af_base_far_transition;
		if (practical->is_dof_blur_far_enabled()) {
			const double far_d = c - half;
			far = far_d > 1.0 / AF_MAX_DISTANCE ? 1.0 / far_d : AF_MAX_DISTANCE;
			if (af_base_far_transition > 0.0) {
				const double end_d = far_d - (1.0 / af_base_far - 1.0 / (af_base_far + af_base_far_transition));
				far_transition = end_d > 1.0 / AF_MAX_DISTANCE ? 1.0 / end_d - far : AF_MAX_DISTANCE;
			}
		}
		if (practical->is_dof_blur_near_enabled()) {
			const double near_d = c + half;
			near = 1.0 / near_d;
			if (af_base_near_transition > 0.0 && af_base_near_transition < af_base_near) {
				const double width_d = 1.0 / (af_base_near - af_base_near_transition) - 1.0 / af_base_near;
				near_transition = near - 1.0 / (near_d + width_d);
			}
		}
		if (practical->get_dof_blur_near_distance() != near) {
			practical->set_dof_blur_near_distance(near);
		}
		if (practical->get_dof_blur_far_distance() != far) {
			practical->set_dof_blur_far_distance(far);
		}
		if (practical->get_dof_blur_near_transition() != near_transition) {
			practical->set_dof_blur_near_transition(near_transition);
		}
		if (practical->get_dof_blur_far_transition() != far_transition) {
			practical->set_dof_blur_far_transition(far_transition);
		}
		af_written[0] = near;
		af_written[1] = far;
		af_written[2] = near_transition;
		af_written[3] = far_transition;
		af_wrote = true;
	}
}

void Camera3D::_autofocus_process(double p_delta) {
	if (!af_initialized) {
		_autofocus_reset();
	}
	if (af_wrote && _autofocus_baseline_changed()) {
		// The user changed the resource while autofocus runs (inspector or script): that becomes the new manual
		// setting, and the lens starts from it, as turning a focus ring under AF-C does.
		_autofocus_reset();
	}

	if (autofocus_mode == AUTOFOCUS_MODE_CONTINUOUS) {
		double measured;
		if (_autofocus_measure(measured)) {
			af_target_diopters = measured;
			af_has_target = true;
		} else {
			if (af_has_target) {
				emit_signal(SNAME("focus_failed"));
			}
			// Nothing to focus on: hold the lens where it is, as AF-C does.
			af_has_target = false;
			af_acquired = false;
			af_target_diopters = af_diopters;
			af_velocity = 0.0;
		}
	}

	const bool driving = (autofocus_mode == AUTOFOCUS_MODE_CONTINUOUS && af_has_target) || (autofocus_mode == AUTOFOCUS_MODE_SINGLE && af_single_running);
	if (driving && p_delta > 0.0) {
		const double tau = MAX((double)autofocus_time_constant, 1e-4);
		const double error = af_diopters - af_target_diopters;
		if (autofocus_hunting <= 0.0) {
			// First-order servo, solved exactly over the frame: frame-rate independent, error falls by 1/e every tau.
			const double next_error = error * Math::exp(-p_delta / tau);
			af_velocity = (next_error - error) / p_delta;
			af_diopters = af_target_diopters + next_error;
		} else {
			// Underdamped second-order servo, the overshoot-and-settle of contrast-detect hunting (a look model: CDAF
			// sweeps past the contrast peak and comes back). The damping ratio gives the requested overshoot
			// fraction, OS = exp(-pi zeta / sqrt(1 - zeta^2)); the natural frequency keeps the envelope's time constant
			// 1 / (zeta omega) equal to tau, so the setting means the same in both servos. Solved exactly over the frame.
			const double log_os = Math::log(CLAMP((double)autofocus_hunting, 1e-4, 0.95));
			const double zeta = -log_os / Math::sqrt(Math::PI * Math::PI + log_os * log_os);
			const double sigma = 1.0 / tau;
			const double omega_d = sigma / zeta * Math::sqrt(1.0 - zeta * zeta);
			const double a = error;
			const double b = (af_velocity + sigma * error) / omega_d;
			const double decay = Math::exp(-sigma * p_delta);
			const double c = Math::cos(omega_d * p_delta);
			const double s = Math::sin(omega_d * p_delta);
			af_diopters = af_target_diopters + decay * (a * c + b * s);
			af_velocity = decay * (af_velocity * c - (sigma * b + omega_d * a) * s);
		}
	}

	if (driving) {
		const double tolerance = _autofocus_tolerance();
		const double error = Math::abs(af_diopters - af_target_diopters);
		// Settled: inside the tolerance and not about to leave it (matters for the hunting servo's overshoot).
		const bool settled = error < tolerance && Math::abs(af_velocity) * autofocus_time_constant < tolerance;
		if (!af_acquired && settled) {
			// AF-S locks on the distance it measured when triggered; the lens still finishes its travel to it.
			af_acquired = true;
			emit_signal(SNAME("focus_acquired"), get_autofocus_distance());
		} else if (af_acquired && error > 2.0 * tolerance) {
			af_acquired = false; // Hysteresis, so a subject near the threshold does not re-emit every frame.
		}
	}

	_autofocus_apply(1.0 / MAX(af_diopters, 1e-5));
}

void Camera3D::autofocus_trigger() {
	ERR_FAIL_COND_MSG(!is_inside_tree(), "Camera is not inside the scene tree.");
	if (autofocus_mode != AUTOFOCUS_MODE_SINGLE) {
		return; // Continuous keeps focusing on its own; manual never does.
	}
	if (!af_initialized) {
		_autofocus_reset();
	}
	double measured;
	if (_autofocus_measure(measured)) {
		af_target_diopters = measured;
		af_has_target = true;
		af_single_running = true;
		af_acquired = false;
	} else {
		af_single_running = false;
		af_acquired = false;
		emit_signal(SNAME("focus_failed"));
	}
}

real_t Camera3D::get_autofocus_distance() const {
	if (!af_initialized) {
		CameraAttributesPhysical *physical = Object::cast_to<CameraAttributesPhysical>(attributes.ptr());
		return physical ? physical->get_focus_distance() : 10.0;
	}
	return 1.0 / MAX(af_diopters, 1.0 / AF_MAX_DISTANCE);
}

bool Camera3D::is_autofocus_acquired() const {
	return af_acquired;
}

void Camera3D::set_autofocus_mode(AutofocusMode p_mode) {
	ERR_FAIL_INDEX((int)p_mode, 3);
	if (autofocus_mode == p_mode) {
		return;
	}
	if (autofocus_mode == AUTOFOCUS_MODE_MANUAL) {
		af_initialized = false; // Start from the resource's current (manual) setting.
	}
	// Switching to manual leaves the lens where autofocus put it, as on a camera.
	autofocus_mode = p_mode;
	af_single_running = false;
	af_acquired = false;
	_update_process_mode();
}

Camera3D::AutofocusMode Camera3D::get_autofocus_mode() const {
	return autofocus_mode;
}

void Camera3D::set_autofocus_target(const NodePath &p_target) {
	autofocus_target = p_target;
}

NodePath Camera3D::get_autofocus_target() const {
	return autofocus_target;
}

void Camera3D::set_autofocus_points(const PackedVector2Array &p_points) {
	autofocus_points = p_points;
}

PackedVector2Array Camera3D::get_autofocus_points() const {
	return autofocus_points;
}

void Camera3D::set_autofocus_collision_mask(uint32_t p_mask) {
	autofocus_collision_mask = p_mask;
}

uint32_t Camera3D::get_autofocus_collision_mask() const {
	return autofocus_collision_mask;
}

void Camera3D::set_autofocus_time_constant(real_t p_seconds) {
	autofocus_time_constant = MAX(p_seconds, (real_t)0.001);
}

real_t Camera3D::get_autofocus_time_constant() const {
	return autofocus_time_constant;
}

void Camera3D::set_autofocus_hunting(real_t p_overshoot) {
	autofocus_hunting = CLAMP(p_overshoot, (real_t)0.0, (real_t)0.9);
}

real_t Camera3D::get_autofocus_hunting() const {
	return autofocus_hunting;
}

/* Handheld shake */

// Each axis is a sum of sinusoids with seeded random frequencies spread log-uniformly over two octaves around the axis
// frequency (f/2 to 2f; breathing, a narrow +-0.3 octave) and seeded random phases: band-limited noise with an exact
// RMS (sinusoids of different frequencies are orthogonal, so RMS = sqrt(sum a_i^2 / 2)), deterministic for a seed, and
// frame-rate independent. Phases are advanced, not computed from absolute time, so changing a frequency does not jump.
//
// Default amplitudes and frequencies are a look choice for a camera held in two hands while standing, within measured
// ranges: hand and arm sway concentrate below ~5 Hz while physiological tremor (8-12 Hz) is far smaller, which is why
// image stabilization tests use hand-shake waveforms with most energy between ~1 and ~10 Hz (CIPA DC-011); resting
// breathing is 12-20 breaths per minute (0.2-0.33 Hz).

static constexpr double SHAKE_STABILIZATION_CUTOFF = 0.5; // Hz. Gyro high-pass of the stabilizer, see _shake_process().
static constexpr double SHAKE_REFERENCE_FOV = 26.9915; // Degrees. Vertical FOV of a 50 mm lens on 36x24 mm: 2 atan(12 / 50).

void Camera3D::_shake_seed_partials() {
	RandomPCG rng(hash_murmur3_one_32((uint32_t)shake_seed));
	for (int axis = 0; axis < SHAKE_AXES; axis++) {
		const double spread = axis == SHAKE_AXES - 1 ? 0.3 : 1.0; // Octaves each side of the axis frequency.
		for (int i = 0; i < SHAKE_PARTIALS; i++) {
			// Stratified: one partial per slice of the band, jittered within it, so a seed never leaves gaps.
			const double u = (i + rng.randf()) / SHAKE_PARTIALS;
			shake_multiplier[axis][i] = Math::pow(2.0, spread * (2.0 * u - 1.0));
			shake_phase[axis][i] = rng.randf() * Math::TAU;
		}
	}
}

void Camera3D::_shake_process(double p_delta) {
	const Vector3 rot_amp = shake_rotation_amplitude;
	const double amplitude[SHAKE_AXES] = { rot_amp.x, rot_amp.y, rot_amp.z, shake_position_amplitude.x, shake_position_amplitude.y, shake_position_amplitude.z, shake_breathing_amplitude };
	const double frequency[SHAKE_AXES] = { shake_rotation_frequency.x, shake_rotation_frequency.y, shake_rotation_frequency.z, shake_position_frequency.x, shake_position_frequency.y, shake_position_frequency.z, shake_breathing_frequency };
	const double partial_scale = Math::sqrt(2.0 / SHAKE_PARTIALS); // Per-partial amplitude for a total RMS of 1.

	double value[SHAKE_AXES];
	for (int axis = 0; axis < SHAKE_AXES; axis++) {
		double sum = 0.0;
		for (int i = 0; i < SHAKE_PARTIALS; i++) {
			const double f = frequency[axis] * shake_multiplier[axis][i];
			shake_phase[axis][i] = Math::fmod(shake_phase[axis][i] + Math::TAU * f * p_delta, Math::TAU);
			// Optical and sensor-shift stabilizers integrate a gyro signal that is high-pass filtered (a cutoff of a
			// few tenths of a hertz) so they do not fight intentional pans: they cancel the stabilization strength of
			// the motion above the cutoff and progressively less below it. First-order high-pass magnitude.
			const double high_pass = f / Math::sqrt(f * f + SHAKE_STABILIZATION_CUTOFF * SHAKE_STABILIZATION_CUTOFF);
			const double gain = 1.0 - shake_stabilization * high_pass;
			sum += gain * Math::sin(shake_phase[axis][i]);
		}
		value[axis] = sum * partial_scale * amplitude[axis];
	}

	double rotation_scale = 1.0;
	if (!shake_scale_with_focal_length && mode == PROJECTION_PERSPECTIVE) {
		// The hand's angular motion does not depend on the lens, so in degrees a long lens shows more shake in the
		// frame by itself (physically right, the default). Off: the amplitudes are as seen through a 50 mm full-frame
		// lens and scale with the field of view, so the shake keeps its size on screen while zooming.
		rotation_scale = fov / SHAKE_REFERENCE_FOV;
	}
	const Vector3 euler = Vector3(value[0] + value[6], value[1], value[2]) * Math::deg_to_rad(rotation_scale);
	shake_offset = Transform3D(Basis::from_euler(euler), Vector3(value[3], value[4], value[5]));

	if (is_inside_tree()) {
		RenderingServer::get_singleton()->camera_set_transform(camera, get_camera_transform());
	}
}

void Camera3D::set_shake_enabled(bool p_enabled) {
	if (shake_enabled == p_enabled) {
		return;
	}
	shake_enabled = p_enabled;
	shake_offset = Transform3D();
	_update_process_mode();
	_update_camera();
}

bool Camera3D::is_shake_enabled() const {
	return shake_enabled;
}

void Camera3D::set_shake_seed(int p_seed) {
	shake_seed = p_seed;
	_shake_seed_partials();
}

int Camera3D::get_shake_seed() const {
	return shake_seed;
}

void Camera3D::set_shake_rotation_amplitude(const Vector3 &p_degrees) {
	shake_rotation_amplitude = p_degrees;
	_shake_value_changed();
}

Vector3 Camera3D::get_shake_rotation_amplitude() const {
	return shake_rotation_amplitude;
}

void Camera3D::set_shake_rotation_frequency(const Vector3 &p_hz) {
	shake_rotation_frequency = p_hz;
	_shake_value_changed();
}

Vector3 Camera3D::get_shake_rotation_frequency() const {
	return shake_rotation_frequency;
}

void Camera3D::set_shake_position_amplitude(const Vector3 &p_meters) {
	shake_position_amplitude = p_meters;
	_shake_value_changed();
}

Vector3 Camera3D::get_shake_position_amplitude() const {
	return shake_position_amplitude;
}

void Camera3D::set_shake_position_frequency(const Vector3 &p_hz) {
	shake_position_frequency = p_hz;
	_shake_value_changed();
}

Vector3 Camera3D::get_shake_position_frequency() const {
	return shake_position_frequency;
}

void Camera3D::set_shake_breathing_amplitude(real_t p_degrees) {
	shake_breathing_amplitude = p_degrees;
	_shake_value_changed();
}

real_t Camera3D::get_shake_breathing_amplitude() const {
	return shake_breathing_amplitude;
}

void Camera3D::set_shake_breathing_frequency(real_t p_hz) {
	shake_breathing_frequency = p_hz;
	_shake_value_changed();
}

real_t Camera3D::get_shake_breathing_frequency() const {
	return shake_breathing_frequency;
}

void Camera3D::set_shake_stabilization(real_t p_strength) {
	shake_stabilization = CLAMP(p_strength, (real_t)0.0, (real_t)1.0);
	_shake_value_changed();
}

real_t Camera3D::get_shake_stabilization() const {
	return shake_stabilization;
}

void Camera3D::set_shake_scale_with_focal_length(bool p_enabled) {
	shake_scale_with_focal_length = p_enabled;
	_shake_value_changed();
}

bool Camera3D::is_shake_scale_with_focal_length() const {
	return shake_scale_with_focal_length;
}

Transform3D Camera3D::get_shake_offset() const {
	return _shake_active() ? shake_offset : Transform3D();
}

// In the editor the shake would move the 3D viewport's camera preview and the Camera3D inspector preview while the user
// frames the shot, so it runs there only with Visible in Editor ticked. A running project always shakes.
bool Camera3D::_shake_active() const {
	return shake_enabled && (shake_visible_in_editor || !Engine::get_singleton()->is_editor_hint());
}

void Camera3D::set_shake_visible_in_editor(bool p_visible) {
	if (shake_visible_in_editor == p_visible) {
		return;
	}
	shake_visible_in_editor = p_visible;
	shake_offset = Transform3D();
	_update_process_mode();
	_update_camera();
}

bool Camera3D::is_shake_visible_in_editor() const {
	return shake_visible_in_editor;
}

/* Handheld shake presets
 *
 * Each preset writes every shake setting except the seed. Rotation is (pitch, yaw, roll) in degrees RMS and position
 * (right, up, back) in meters RMS, each with the center of its two-octave band (see _shake_seed_partials()). The model
 * has one band per axis, so a preset gives each axis the motion that dominates it. Sources:
 * - Hand shake while holding a camera still: CIPA DC-X011-2012 (image stabilization measurement) models it from
 *   measured hand-shake data with most energy between ~1 and ~10 Hz; the angle over a 1/8-1/30 s exposure is a few
 *   hundredths of a degree, which is 0.1-0.2 degree RMS over the band. Physiological tremor (8-12 Hz) is ~10x smaller
 *   and is not modeled separately.
 * - Standing sway: quiet-stance postural sway is a few millimeters RMS with a mean frequency of ~0.3-0.5 Hz (Prieto et al.
 *   1996, "Measures of postural steadiness", IEEE Trans. Biomed. Eng. 43(9)).
 * - Breathing: 12-20 breaths a minute at rest (0.2-0.33 Hz), ~25 walking, 35-45 running.
 * - Walking: step frequency ~2 Hz (Pachi and Ji 2005, "Frequency and velocity of people walking", The Structural
 *   Engineer 83(3): mean 1.87-2.0 Hz); vertical head travel 2-4 cm peak to peak with head pitch of a few degrees at the
 *   step frequency, and lateral sway at the stride frequency, half of it (Hirasaki et al. 1999, "Effects of walking
 *   velocity on vertical head and body movements during locomotion", Exp. Brain Res. 127). The hands absorb part of it.
 * - Running: step frequency 2.7-3 Hz (Cavanagh and Kram 1989, "Stride length in distance running", Med. Sci. Sports
 *   Exerc. 21(4)); vertical center-of-mass travel 6-10 cm peak to peak.
 * - Vehicles: sprung-mass bounce and pitch at 1-1.5 Hz, wheel hop at 10-15 Hz (Gillespie 1992, "Fundamentals of
 *   Vehicle Dynamics", ch. 5).
 * - Tripods: a loaded tripod and head ring at roughly 8-20 Hz when wind or a touch excites them; the amplitude is a
 *   look choice for a light breeze.
 * Amplitudes for shoulder rigs and gimbals (residual drift, unstabilized walking bob) are look choices within those
 * ranges: a rig's mass lowers and slows the motion, a gimbal removes rotation but not translation.
 */

struct ShakePresetValues {
	real_t rotation_amplitude[3];
	real_t rotation_frequency[3];
	real_t position_amplitude[3];
	real_t position_frequency[3];
	real_t breathing_amplitude;
	real_t breathing_frequency;
	real_t stabilization;
	bool scale_with_focal_length;
};

static const ShakePresetValues shake_presets[Camera3D::SHAKE_PRESET_MAX] = {
	// Custom: not applied; listed so the table is indexed by the enum.
	{ { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, 0, 0, 0, true },
	// Handheld: standing still, stills grip. CIPA hand-shake band, postural sway, resting breath.
	{ { 0.12, 0.12, 0.06 }, { 3.0, 3.0, 2.5 }, { 0.001, 0.001, 0.0005 }, { 0.5, 0.5, 0.5 }, 0.1, 0.25, 0.0, true },
	// Handheld, stabilized: the same with a lens or sensor-shift stabilizer at 0.8 (about 2.3 stops on the 1-10 Hz
	// band, inside the 2-5 stops CIPA ratings report; a look choice).
	{ { 0.12, 0.12, 0.06 }, { 3.0, 3.0, 2.5 }, { 0.001, 0.001, 0.0005 }, { 0.5, 0.5, 0.5 }, 0.1, 0.25, 0.8, true },
	// Handheld walking: pitch and vertical bob at the step frequency, yaw, roll and lateral sway at the stride frequency.
	{ { 0.8, 0.6, 0.5 }, { 2.0, 1.0, 1.0 }, { 0.008, 0.012, 0.005 }, { 1.0, 2.0, 2.0 }, 0.15, 0.4, 0.0, true },
	// Running / action: the same pattern at a running step frequency, larger, with fast breathing.
	{ { 2.0, 1.5, 1.5 }, { 2.8, 1.4, 1.4 }, { 0.015, 0.03, 0.01 }, { 1.4, 2.8, 2.8 }, 0.3, 0.7, 0.0, true },
	// Shoulder rig: braced on the shoulder, so slow body sway rather than hand shake; breathing moves the shoulder.
	{ { 0.15, 0.2, 0.2 }, { 1.2, 1.0, 0.8 }, { 0.003, 0.003, 0.002 }, { 0.8, 0.8, 0.8 }, 0.12, 0.25, 0.0, true },
	// Gimbal / stabilizer while walking: slow residual rotation drift; the walking bob passes through, damped by the arms.
	{ { 0.03, 0.03, 0.02 }, { 0.5, 0.5, 0.5 }, { 0.004, 0.006, 0.003 }, { 1.0, 2.0, 2.0 }, 0.0, 0.25, 0.0, true },
	// Tripod in wind: no operator, a small vibration at the tripod's resonance.
	{ { 0.03, 0.03, 0.01 }, { 10.0, 10.0, 10.0 }, { 0.0002, 0.0002, 0.0001 }, { 10.0, 10.0, 10.0 }, 0.0, 0.25, 0.0, true },
	// Vehicle mount: body bounce, pitch and roll at the ride frequency; yaw buzz from wheel hop through the mount.
	{ { 0.3, 0.1, 0.3 }, { 1.2, 8.0, 1.0 }, { 0.003, 0.01, 0.003 }, { 1.0, 1.2, 1.2 }, 0.0, 0.25, 0.0, true },
};

void Camera3D::set_shake_preset(ShakePreset p_preset) {
	ERR_FAIL_INDEX(p_preset, SHAKE_PRESET_MAX);
	shake_preset = p_preset;
	if (p_preset != SHAKE_PRESET_CUSTOM) {
		const ShakePresetValues &v = shake_presets[p_preset];
		shake_applying_preset = true;
		set_shake_rotation_amplitude(Vector3(v.rotation_amplitude[0], v.rotation_amplitude[1], v.rotation_amplitude[2]));
		set_shake_rotation_frequency(Vector3(v.rotation_frequency[0], v.rotation_frequency[1], v.rotation_frequency[2]));
		set_shake_position_amplitude(Vector3(v.position_amplitude[0], v.position_amplitude[1], v.position_amplitude[2]));
		set_shake_position_frequency(Vector3(v.position_frequency[0], v.position_frequency[1], v.position_frequency[2]));
		set_shake_breathing_amplitude(v.breathing_amplitude);
		set_shake_breathing_frequency(v.breathing_frequency);
		set_shake_stabilization(v.stabilization);
		set_shake_scale_with_focal_length(v.scale_with_focal_length);
		shake_applying_preset = false;
	}
	notify_property_list_changed();
}

Camera3D::ShakePreset Camera3D::get_shake_preset() const {
	return shake_preset;
}

// Exact comparison: a saved float reads back bit-exact, so a loaded scene matches its preset, and any edit does not.
bool Camera3D::_shake_matches_preset() const {
	const ShakePresetValues &v = shake_presets[shake_preset];
	return shake_rotation_amplitude == Vector3(v.rotation_amplitude[0], v.rotation_amplitude[1], v.rotation_amplitude[2]) &&
			shake_rotation_frequency == Vector3(v.rotation_frequency[0], v.rotation_frequency[1], v.rotation_frequency[2]) &&
			shake_position_amplitude == Vector3(v.position_amplitude[0], v.position_amplitude[1], v.position_amplitude[2]) &&
			shake_position_frequency == Vector3(v.position_frequency[0], v.position_frequency[1], v.position_frequency[2]) &&
			shake_breathing_amplitude == v.breathing_amplitude &&
			shake_breathing_frequency == v.breathing_frequency &&
			shake_stabilization == v.stabilization &&
			shake_scale_with_focal_length == v.scale_with_focal_length;
}

// Editing a value a preset set makes the settings the user's own: the preset becomes Custom. A value set to what the
// preset already has (as loading a saved scene does, after the preset) keeps the preset.
void Camera3D::_shake_value_changed() {
	if (shake_applying_preset || shake_preset == SHAKE_PRESET_CUSTOM || _shake_matches_preset()) {
		return;
	}
	shake_preset = SHAKE_PRESET_CUSTOM;
	notify_property_list_changed();
}

Camera3D::Camera3D() {
	camera = RenderingServer::get_singleton()->camera_create();
	set_perspective(75.0, 0.05, 4000.0);
	RenderingServer::get_singleton()->camera_set_cull_mask(camera, layers);
	//active=false;
	velocity_tracker.instantiate();
	_shake_seed_partials();
	set_notify_transform(true);
	set_disable_scale(true);
}

Camera3D::~Camera3D() {
	ERR_FAIL_NULL(RenderingServer::get_singleton());
	RenderingServer::get_singleton()->free_rid(camera);
#ifndef PHYSICS_3D_DISABLED
	if (pyramid_shape.is_valid()) {
		ERR_FAIL_NULL(PhysicsServer3D::get_singleton());
		PhysicsServer3D::get_singleton()->free_rid(pyramid_shape);
	}
#endif // PHYSICS_3D_DISABLED
}
