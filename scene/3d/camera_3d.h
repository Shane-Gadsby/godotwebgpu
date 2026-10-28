/**************************************************************************/
/*  camera_3d.h                                                           */
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

#include "core/templates/interpolated_property.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/velocity_tracker_3d.h"
#include "scene/resources/camera_attributes.h"
#include "scene/resources/compositor.h"
#include "scene/resources/environment.h"
#include "scene/resources/texture.h"

// What Camera3D.get_monitoring_clean_texture() returns: a server texture the renderer copies the camera's image into
// before the monitoring overlays are drawn. Internal: scripts see a Texture2D.
class CameraMonitoringCleanTexture : public Texture2D {
	GDCLASS(CameraMonitoringCleanTexture, Texture2D);

	friend class Camera3D;
	RID texture;
	ObjectID camera;

	void _release();

public:
	virtual int get_width() const override;
	virtual int get_height() const override;
	virtual Size2 get_size() const override;
	virtual RID get_rid() const override;
	virtual bool has_alpha() const override;
	virtual Ref<Image> get_image() const override;
	virtual void get_image_async(const Callable &p_callback) const override;

	~CameraMonitoringCleanTexture();
};

class Camera3D : public Node3D {
	GDCLASS(Camera3D, Node3D);

public:
	enum ProjectionType {
		PROJECTION_PERSPECTIVE,
		PROJECTION_ORTHOGONAL,
		PROJECTION_FRUSTUM
	};

	enum KeepAspect {
		KEEP_WIDTH,
		KEEP_HEIGHT
	};

	enum DopplerTracking {
		DOPPLER_TRACKING_DISABLED,
		DOPPLER_TRACKING_IDLE_STEP,
		DOPPLER_TRACKING_PHYSICS_STEP
	};

	enum AutofocusMode {
		AUTOFOCUS_MODE_MANUAL,
		AUTOFOCUS_MODE_SINGLE,
		AUTOFOCUS_MODE_CONTINUOUS,
	};

	enum ShakePreset {
		SHAKE_PRESET_CUSTOM,
		SHAKE_PRESET_HANDHELD,
		SHAKE_PRESET_HANDHELD_STABILIZED,
		SHAKE_PRESET_HANDHELD_WALKING,
		SHAKE_PRESET_RUNNING,
		SHAKE_PRESET_SHOULDER_RIG,
		SHAKE_PRESET_GIMBAL,
		SHAKE_PRESET_TRIPOD_WIND,
		SHAKE_PRESET_VEHICLE_MOUNT,
		SHAKE_PRESET_MAX,
	};

	enum MonitoringHistogramMode {
		MONITORING_HISTOGRAM_LUMA,
		MONITORING_HISTOGRAM_RGB,
	};

	enum MonitoringHistogramPosition {
		MONITORING_HISTOGRAM_TOP_LEFT,
		MONITORING_HISTOGRAM_TOP_RIGHT,
		MONITORING_HISTOGRAM_BOTTOM_LEFT,
		MONITORING_HISTOGRAM_BOTTOM_RIGHT,
	};

	enum MonitoringHistogramChannel {
		MONITORING_HISTOGRAM_CHANNEL_LUMA,
		MONITORING_HISTOGRAM_CHANNEL_RED,
		MONITORING_HISTOGRAM_CHANNEL_GREEN,
		MONITORING_HISTOGRAM_CHANNEL_BLUE,
	};

private:
	bool force_change = false;
	bool current = false;
	Viewport *viewport = nullptr;

	ProjectionType mode = PROJECTION_PERSPECTIVE;

	InterpolatedProperty<real_t> fov = 75.0;
	InterpolatedProperty<real_t> size = 1.0;
	InterpolatedProperty<Vector2> frustum_offset;
	// _ prefix to avoid conflict with Windows defines.
	InterpolatedProperty<real_t> _near = 0.05;
	InterpolatedProperty<real_t> _far = 4000.0;
	real_t v_offset = 0.0;
	real_t h_offset = 0.0;
	KeepAspect keep_aspect = KEEP_HEIGHT;

	RID camera;
	RID scenario_id;

	// String camera_group;

	uint32_t layers = 0xfffff;

	Ref<Environment> environment;
	Ref<CameraAttributes> attributes;
	Ref<Compositor> compositor;
	void _attributes_changed();

	// void _camera_make_current(Node *p_camera);
	friend class Viewport;
	void _update_audio_listener_state();
	TypedArray<Plane> _get_frustum() const;

	// Monitoring overlays, drawn over this camera's image by the renderer's camera finish pass.
	bool monitoring_visible_in_output = false;
	bool monitoring_zebras_enabled = false;
	float monitoring_zebras_level = 95.0;
	bool monitoring_zebras_second_enabled = false;
	float monitoring_zebras_second_level = 70.0;
	Color monitoring_zebras_color = Color(0.0, 0.0, 0.0);
	Color monitoring_zebras_second_color = Color(1.0, 1.0, 1.0);
	bool monitoring_focus_peaking_enabled = false;
	Color monitoring_focus_peaking_color = Color(1.0, 0.0, 0.0);
	float monitoring_focus_peaking_sensitivity = 0.5;
	bool monitoring_false_color_enabled = false;
	bool monitoring_histogram_enabled = false;
	MonitoringHistogramMode monitoring_histogram_mode = MONITORING_HISTOGRAM_LUMA;
	MonitoringHistogramPosition monitoring_histogram_position = MONITORING_HISTOGRAM_BOTTOM_RIGHT;
	float monitoring_histogram_size = 0.25;
	float monitoring_histogram_opacity = 0.75;
	// Histogram readback for scripts: runs while the histogram is enabled and a script asked for it (a call to
	// get_monitoring_histogram() or a connection to monitoring_histogram_updated).
	bool monitoring_histogram_draw = true;
	float monitoring_histogram_update_interval = 0.0;
	bool monitoring_histogram_requested = false; // get_monitoring_histogram() was called.
	bool monitoring_histogram_readback_sent = false;
	uint64_t monitoring_histogram_serial = 0;
	bool _monitoring_histogram_readback_wanted() const;
	void _monitoring_histogram_process();
	void _update_monitoring();
	// The clean frame (the image before the overlays), copied by the renderer every frame while enabled.
	bool monitoring_clean_texture_enabled = false;
	Ref<CameraMonitoringCleanTexture> monitoring_clean_texture;
	DopplerTracking doppler_tracking = DOPPLER_TRACKING_DISABLED;
	Ref<VelocityTracker3D> velocity_tracker;

#ifndef PHYSICS_3D_DISABLED
	RID pyramid_shape;
	Vector<Vector3> pyramid_shape_points;
#endif // PHYSICS_3D_DISABLED

	// These can be set by derived Cameras.
	bool _desired_process_internal = false;
	bool _desired_physics_process_internal = false;

	void _update_process_mode();

	/* Autofocus */

	AutofocusMode autofocus_mode = AUTOFOCUS_MODE_MANUAL;
	NodePath autofocus_target;
	PackedVector2Array autofocus_points = { Vector2(0.5, 0.5) };
	uint32_t autofocus_collision_mask = 1;
	real_t autofocus_time_constant = 0.1; // Seconds.
	real_t autofocus_hunting = 0.0; // Overshoot fraction.

	// Lens state, in diopters (1 / meters): a focus motor's travel is close to linear in diopters, and depth of field
	// is close to constant in diopters, so the servo and the acquisition tolerance both live in that space.
	double af_diopters = 0.0;
	double af_velocity = 0.0; // Diopters per second, for the underdamped (hunting) servo.
	double af_target_diopters = 0.0;
	bool af_initialized = false;
	bool af_has_target = false;
	bool af_single_running = false;
	bool af_acquired = false;

	// What the attributes resource held before autofocus wrote to it (restored around editor saves), and what was last
	// written (a difference means the user edited the resource meanwhile, which becomes the new baseline).
	bool af_wrote = false;
	float af_base_focus_distance = 10.0; // CameraAttributesPhysical.
	float af_base_near = 2.0, af_base_far = 10.0, af_base_near_transition = 1.0, af_base_far_transition = 5.0; // Practical.
	float af_written[4] = {};

	void _autofocus_reset();
	void _autofocus_capture_baseline();
	bool _autofocus_baseline_changed() const;
	bool _autofocus_measure(double &r_diopters) const;
	void _autofocus_apply(double p_distance);
	void _autofocus_restore_baseline();
	double _autofocus_tolerance() const;
	void _autofocus_process(double p_delta);

	/* Handheld shake */

	static constexpr int SHAKE_AXES = 7; // Rotation x, y, z; position x, y, z; breathing.
	static constexpr int SHAKE_PARTIALS = 12; // Sinusoids per axis.
	bool shake_enabled = false;
	int shake_seed = 0;
	Vector3 shake_rotation_amplitude = Vector3(0.3, 0.3, 0.15); // Degrees RMS.
	Vector3 shake_rotation_frequency = Vector3(2.0, 2.0, 1.5); // Hz.
	Vector3 shake_position_amplitude = Vector3(0.002, 0.002, 0.001); // Meters RMS.
	Vector3 shake_position_frequency = Vector3(1.5, 1.5, 1.5); // Hz.
	real_t shake_breathing_amplitude = 0.15; // Degrees RMS (pitch).
	real_t shake_breathing_frequency = 0.25; // Hz.
	real_t shake_stabilization = 0.0;
	bool shake_scale_with_focal_length = true;
	bool shake_visible_in_editor = false;
	ShakePreset shake_preset = SHAKE_PRESET_CUSTOM;
	bool shake_applying_preset = false;

	double shake_phase[SHAKE_AXES][SHAKE_PARTIALS] = {};
	float shake_multiplier[SHAKE_AXES][SHAKE_PARTIALS] = {};
	Transform3D shake_offset;

	void _shake_seed_partials();
	void _shake_process(double p_delta);
	bool _shake_active() const;
	bool _shake_matches_preset() const;
	void _shake_value_changed();
	void _lens_process_changed();

protected:
	// Use from derived classes to set process modes instead of setting directly.
	// This is because physics interpolation may need to request process modes additionally.
	void set_desired_process_modes(bool p_process_internal, bool p_physics_process_internal);

	virtual void _physics_interpolated_changed() override;
	virtual Transform3D _get_adjusted_camera_transform(const Transform3D &p_xform) const;
	///////////////////////////////////////////////////////

	void _update_camera();
	virtual void _request_camera_update();
	virtual void _update_camera_mode();

	virtual void fti_pump_property() override;
	virtual void fti_update_servers_property() override;
	virtual void fti_update_servers_xform() override;

	void _notification(int p_what);
	void _validate_property(PropertyInfo &p_property) const;

	static void _bind_methods();

	Projection _get_camera_projection(real_t p_near) const;

	// The Panini projection of the attributes in effect (the camera's, else the world's), which the renderer applies
	// to the image: the screen-space functions follow it.
	struct PaniniMapping {
		double distance = 0.0;
		double vertical_compression = 0.0;
		Vector2 tan_half; // The rendered frame's half extent in the tangent plane.
		Vector2 output_half; // The output frame's half extent in the Panini plane.
	};
	bool _get_panini_mapping(const Projection &p_projection, PaniniMapping &r_mapping) const;

public:
	enum {
		NOTIFICATION_BECAME_CURRENT = 50,
		NOTIFICATION_LOST_CURRENT = 51
	};

	void set_perspective(real_t p_fovy_degrees, real_t p_z_near, real_t p_z_far);
	void set_orthogonal(real_t p_size, real_t p_z_near, real_t p_z_far);
	void set_frustum(real_t p_size, Vector2 p_offset, real_t p_z_near, real_t p_z_far);
	void set_projection(Camera3D::ProjectionType p_mode);

	void make_current();
	void clear_current(bool p_enable_next = true);
	void set_current(bool p_enabled);
	bool is_current() const;

	RID get_camera() const;

	real_t get_fov() const;
	real_t get_size() const;
	real_t get_far() const;
	real_t get_near() const;
	Vector2 get_frustum_offset() const;

	ProjectionType get_projection() const;

	void set_fov(real_t p_fov);
	void set_size(real_t p_size);
	void set_far(real_t p_far);
	void set_near(real_t p_near);
	void set_frustum_offset(Vector2 p_offset);

	virtual Transform3D get_camera_transform() const;
	virtual Projection get_camera_projection() const;

	virtual Vector3 project_ray_normal(const Point2 &p_pos) const;
	virtual Vector3 project_ray_origin(const Point2 &p_pos) const;
	virtual Vector3 project_local_ray_normal(const Point2 &p_pos) const;
	virtual Point2 unproject_position(const Vector3 &p_pos) const;
	bool safe_unproject_position(const Vector3 &p_pos, Point2 &r_result) const;
	bool is_position_behind(const Vector3 &p_pos) const;
	virtual Vector3 project_position(const Point2 &p_point, real_t p_z_depth) const;

	Vector<Vector3> get_near_plane_points() const;

	void set_cull_mask(uint32_t p_layers);
	uint32_t get_cull_mask() const;

	void set_cull_mask_value(int p_layer_number, bool p_enable);
	bool get_cull_mask_value(int p_layer_number) const;

	virtual Vector<Plane> get_frustum() const;
	bool is_position_in_frustum(const Vector3 &p_position) const;

	void set_environment(const Ref<Environment> &p_environment);
	Ref<Environment> get_environment() const;

	void set_attributes(const Ref<CameraAttributes> &p_effects);
	Ref<CameraAttributes> get_attributes() const;

	void set_compositor(const Ref<Compositor> &p_compositor);
	Ref<Compositor> get_compositor() const;

	void set_monitoring_visible_in_output(bool p_visible);
	bool is_monitoring_visible_in_output() const;
	void set_monitoring_zebras_enabled(bool p_enabled);
	bool is_monitoring_zebras_enabled() const;
	void set_monitoring_zebras_level(float p_level);
	float get_monitoring_zebras_level() const;
	void set_monitoring_zebras_second_enabled(bool p_enabled);
	bool is_monitoring_zebras_second_enabled() const;
	void set_monitoring_zebras_second_level(float p_level);
	float get_monitoring_zebras_second_level() const;
	void set_monitoring_zebras_color(const Color &p_color);
	Color get_monitoring_zebras_color() const;
	void set_monitoring_zebras_second_color(const Color &p_color);
	Color get_monitoring_zebras_second_color() const;
	void set_monitoring_focus_peaking_enabled(bool p_enabled);
	bool is_monitoring_focus_peaking_enabled() const;
	void set_monitoring_focus_peaking_color(const Color &p_color);
	Color get_monitoring_focus_peaking_color() const;
	void set_monitoring_focus_peaking_sensitivity(float p_sensitivity);
	float get_monitoring_focus_peaking_sensitivity() const;
	void set_monitoring_false_color_enabled(bool p_enabled);
	bool is_monitoring_false_color_enabled() const;
	void set_monitoring_histogram_enabled(bool p_enabled);
	bool is_monitoring_histogram_enabled() const;
	void set_monitoring_histogram_mode(MonitoringHistogramMode p_mode);
	MonitoringHistogramMode get_monitoring_histogram_mode() const;
	void set_monitoring_histogram_position(MonitoringHistogramPosition p_position);
	MonitoringHistogramPosition get_monitoring_histogram_position() const;
	void set_monitoring_histogram_size(float p_size);
	float get_monitoring_histogram_size() const;
	void set_monitoring_histogram_opacity(float p_opacity);
	float get_monitoring_histogram_opacity() const;
	void set_monitoring_histogram_draw(bool p_draw);
	bool is_monitoring_histogram_draw() const;
	void set_monitoring_histogram_update_interval(float p_interval);
	float get_monitoring_histogram_update_interval() const;
	PackedInt32Array get_monitoring_histogram(MonitoringHistogramChannel p_channel = MONITORING_HISTOGRAM_CHANNEL_LUMA);
	void set_monitoring_clean_texture_enabled(bool p_enabled);
	bool is_monitoring_clean_texture_enabled() const;
	Ref<Texture2D> get_monitoring_clean_texture() const;

	void set_keep_aspect_mode(KeepAspect p_aspect);
	KeepAspect get_keep_aspect_mode() const;

	void set_v_offset(real_t p_offset);
	real_t get_v_offset() const;

	void set_h_offset(real_t p_offset);
	real_t get_h_offset() const;

	void set_doppler_tracking(DopplerTracking p_tracking);
	DopplerTracking get_doppler_tracking() const;

	Vector3 get_doppler_tracked_velocity() const;

	void set_autofocus_mode(AutofocusMode p_mode);
	AutofocusMode get_autofocus_mode() const;
	void set_autofocus_target(const NodePath &p_target);
	NodePath get_autofocus_target() const;
	void set_autofocus_points(const PackedVector2Array &p_points);
	PackedVector2Array get_autofocus_points() const;
	void set_autofocus_collision_mask(uint32_t p_mask);
	uint32_t get_autofocus_collision_mask() const;
	void set_autofocus_time_constant(real_t p_seconds);
	real_t get_autofocus_time_constant() const;
	void set_autofocus_hunting(real_t p_overshoot);
	real_t get_autofocus_hunting() const;
	void autofocus_trigger();
	real_t get_autofocus_distance() const;
	bool is_autofocus_acquired() const;

	void set_shake_enabled(bool p_enabled);
	bool is_shake_enabled() const;
	void set_shake_seed(int p_seed);
	int get_shake_seed() const;
	void set_shake_rotation_amplitude(const Vector3 &p_degrees);
	Vector3 get_shake_rotation_amplitude() const;
	void set_shake_rotation_frequency(const Vector3 &p_hz);
	Vector3 get_shake_rotation_frequency() const;
	void set_shake_position_amplitude(const Vector3 &p_meters);
	Vector3 get_shake_position_amplitude() const;
	void set_shake_position_frequency(const Vector3 &p_hz);
	Vector3 get_shake_position_frequency() const;
	void set_shake_breathing_amplitude(real_t p_degrees);
	real_t get_shake_breathing_amplitude() const;
	void set_shake_breathing_frequency(real_t p_hz);
	real_t get_shake_breathing_frequency() const;
	void set_shake_stabilization(real_t p_strength);
	real_t get_shake_stabilization() const;
	void set_shake_scale_with_focal_length(bool p_enabled);
	bool is_shake_scale_with_focal_length() const;
	Transform3D get_shake_offset() const;
	void set_shake_visible_in_editor(bool p_visible);
	bool is_shake_visible_in_editor() const;
	void set_shake_preset(ShakePreset p_preset);
	ShakePreset get_shake_preset() const;

#ifndef PHYSICS_3D_DISABLED
	RID get_pyramid_shape_rid();
#endif // PHYSICS_3D_DISABLED

	Camera3D();
	~Camera3D();
};

VARIANT_ENUM_CAST(Camera3D::ProjectionType);
VARIANT_ENUM_CAST(Camera3D::KeepAspect);
VARIANT_ENUM_CAST(Camera3D::DopplerTracking);
VARIANT_ENUM_CAST(Camera3D::AutofocusMode);
VARIANT_ENUM_CAST(Camera3D::ShakePreset);
VARIANT_ENUM_CAST(Camera3D::MonitoringHistogramMode);
VARIANT_ENUM_CAST(Camera3D::MonitoringHistogramPosition);
VARIANT_ENUM_CAST(Camera3D::MonitoringHistogramChannel);
