extends SceneTree

# CameraAttributes.frame_rate / frame_rate_hold, exposure_shutter_angle / Physical Speed-Angle mode and Light3D
# flicker (work tasks 85-87). Run with --fixed-fps 60, so the engine clock advances 1/60 s a frame; see run_native.sh.

const SIZE := Vector2i(64, 64)

var frame := 0
var results := {}
var flicker_vp: SubViewport
var flicker_cam_attr: CameraAttributesPhysical
var flicker_samples := { 100.0: [], 1000.0: [] }
var hold_vp: SubViewport
var free_vp: SubViewport
var hold_box: Node3D
var free_box: Node3D
var hold_last := PackedByteArray()
var free_last := PackedByteArray()
var hold_changes := 0
var free_changes := 0
var spec_vp: SubViewport
var spec_light: SpotLight3D
var spec_peak := {}


func _record(p_name: String, p_ok: bool, p_info := {}) -> void:
	var r := { "ok": p_ok }
	r.merge(p_info)
	results[p_name] = r


func _viewport(p_attr: CameraAttributes) -> SubViewport:
	var vp := SubViewport.new()
	vp.size = SIZE
	vp.own_world_3d = true
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(vp)
	var cam := Camera3D.new()
	cam.position = Vector3(0, 0, 3)
	cam.attributes = p_attr
	vp.add_child(cam)
	return vp


func _initialize() -> void:
	_test_api()

	# Flicker: a white wall lit only by a lamp on 50 Hz mains, fully modulated (100 Hz pulse).
	flicker_cam_attr = CameraAttributesPhysical.new()
	flicker_cam_attr.exposure_shutter_speed = 100.0
	# An indoor exposure for an indoor lamp (the default f/16 at ISO 100 is for sunlight and leaves the wall black).
	flicker_cam_attr.exposure_aperture = 2.0
	flicker_cam_attr.exposure_sensitivity = 800.0
	flicker_vp = _viewport(flicker_cam_attr)
	var wall := MeshInstance3D.new()
	wall.mesh = QuadMesh.new()
	wall.mesh.size = Vector2(4, 4)
	flicker_vp.add_child(wall)
	var lamp := OmniLight3D.new()
	lamp.position = Vector3(0, 0, 1)
	lamp.light_intensity_lumens = 2000.0
	lamp.omni_range = 10.0
	lamp.flicker_frequency = Light3D.FLICKER_50_HZ
	lamp.flicker_depth = 1.0
	flicker_vp.add_child(lamp)

	# Hold: a box moving every engine frame, seen by a 24 fps camera that holds its frames and by one that does not.
	var held := CameraAttributesPractical.new()
	held.frame_rate = 24.0
	held.frame_rate_hold = true
	hold_vp = _viewport(held)
	hold_box = _moving_box(hold_vp)
	free_vp = _viewport(CameraAttributesPractical.new())
	free_box = _moving_box(free_vp)

	# Specular size: a near-mirror sphere under a spot light, its highlight seen with light_specular_size 0 and 0.6 m.
	# Indoor exposure, as for the flicker wall: the default sunlight exposure leaves an indoor lamp invisible.
	var spec_attr := CameraAttributesPhysical.new()
	spec_attr.exposure_aperture = 2.0
	spec_attr.exposure_sensitivity = 800.0
	spec_vp = _viewport(spec_attr)
	spec_vp.size = Vector2i(256, 256)
	var ball := MeshInstance3D.new()
	ball.mesh = SphereMesh.new()
	var mirror := StandardMaterial3D.new()
	mirror.albedo_color = Color(0.1, 0.1, 0.1)
	mirror.roughness = 0.04
	ball.material_override = mirror
	spec_vp.add_child(ball)
	spec_light = SpotLight3D.new()
	spec_light.position = Vector3(0.6, 0.6, 2.0)
	spec_light.look_at_from_position(spec_light.position, Vector3.ZERO)
	spec_light.light_intensity_lumens = 2000.0
	spec_light.spot_range = 10.0
	spec_vp.add_child(spec_light)


func _moving_box(p_vp: SubViewport) -> Node3D:
	var box := MeshInstance3D.new()
	box.mesh = BoxMesh.new()
	box.mesh.size = Vector3(0.5, 0.5, 0.5)
	p_vp.add_child(box)
	var sun := DirectionalLight3D.new()
	sun.rotation = Vector3(-0.6, 0.4, 0)
	sun.light_intensity_lux = 50000.0
	p_vp.add_child(sun)
	return box


func _test_api() -> void:
	var p := CameraAttributesPhysical.new()
	p.exposure_shutter_mode = CameraAttributesPhysical.SHUTTER_MODE_ANGLE
	p.exposure_shutter_angle = 180.0
	_record("angle_180_at_24_fps_is_1_48_s", is_equal_approx(p.exposure_shutter_speed, 48.0), { "speed": p.exposure_shutter_speed })
	p.frame_rate = 50.0
	_record("angle_180_at_50_fps_is_1_100_s", is_equal_approx(p.exposure_shutter_speed, 100.0), { "speed": p.exposure_shutter_speed })
	p.exposure_shutter_mode = CameraAttributesPhysical.SHUTTER_MODE_SPEED
	p.exposure_shutter_speed = 250.0
	_record("speed_mode_keeps_its_speed", is_equal_approx(p.exposure_shutter_speed, 250.0), { "speed": p.exposure_shutter_speed })
	# Older scenes stored the angle as motion_blur_shutter_angle.
	var old := CameraAttributesPractical.new()
	old.set("motion_blur_shutter_angle", 90.0)
	_record("motion_blur_shutter_angle_still_loads", is_equal_approx(old.exposure_shutter_angle, 90.0), { "angle": old.exposure_shutter_angle })


func _luma(p_vp: SubViewport) -> float:
	var img := p_vp.get_texture().get_image()
	img.convert(Image.FORMAT_RGB8)
	var sum := 0.0
	for y in range(24, 40):
		for x in range(24, 40):
			sum += img.get_pixel(x, y).get_luminance()
	return sum / 256.0


# The brightest pixel and how many pixels are within 90% of it (the highlight's area).
func _highlight(p_vp: SubViewport) -> Dictionary:
	var img := p_vp.get_texture().get_image()
	img.convert(Image.FORMAT_RGB8)
	var peak := 0.0
	for y in img.get_height():
		for x in img.get_width():
			peak = maxf(peak, img.get_pixel(x, y).get_luminance())
	var area := 0
	for y in img.get_height():
		for x in img.get_width():
			area += int(img.get_pixel(x, y).get_luminance() >= peak * 0.9)
	return { "peak": peak, "area": area }


func _process(_delta: float) -> bool:
	frame += 1
	# Linear, ~2 px a frame at this size, so every frame differs (a sine stalls at its ends).
	hold_box.position.x = fmod(frame * 0.15, 3.0) - 1.5
	free_box.position.x = fmod(frame * 0.15, 3.0) - 1.5
	if frame > 20 and frame <= 80:
		var h := hold_vp.get_texture().get_image().get_data()
		var f := free_vp.get_texture().get_image().get_data()
		if frame > 21:
			hold_changes += int(h != hold_last)
			free_changes += int(f != free_last)
		hold_last = h
		free_last = f
	if frame > 20 and frame <= 50:
		flicker_samples[100.0].append(_luma(flicker_vp))
	if frame == 50:
		flicker_cam_attr.exposure_shutter_speed = 1000.0
	if frame > 55 and frame <= 85:
		flicker_samples[1000.0].append(_luma(flicker_vp))
	if frame == 30:
		spec_peak[0.0] = _highlight(spec_vp)
		if OS.has_environment("CT_SAVE"):
			spec_vp.get_texture().get_image().save_png(OS.get_environment("CT_SAVE") + "/spec_0.png")
		spec_light.light_specular_size = 0.6
	if frame == 40:
		spec_peak[0.6] = _highlight(spec_vp)
		if OS.has_environment("CT_SAVE"):
			spec_vp.get_texture().get_image().save_png(OS.get_environment("CT_SAVE") + "/spec_06.png")
	if frame == 90:
		_finish()
		return true
	return false


func _spread(p_values: Array) -> float:
	var lo := INF
	var hi := -INF
	var mean := 0.0
	for v: float in p_values:
		lo = minf(lo, v)
		hi = maxf(hi, v)
		mean += v
	mean /= p_values.size()
	return (hi - lo) / maxf(mean, 1e-6)


func _finish() -> void:
	# 1/100 s is one whole 100 Hz pulse: steady. 1/1000 s samples the pulse: it beats frame to frame.
	var steady := _spread(flicker_samples[100.0])
	var beating := _spread(flicker_samples[1000.0])
	var mean_steady: float = flicker_samples[100.0].reduce(func(a, b): return a + b, 0.0) / flicker_samples[100.0].size()
	var mean_beating: float = flicker_samples[1000.0].reduce(func(a, b): return a + b, 0.0) / flicker_samples[1000.0].size()
	# Both must be mid-range: a clipped or black wall shows no flicker either way.
	_record("flicker_steady_at_1_100_s", steady < 0.05 and mean_steady > 0.05 and mean_steady < 0.95, { "spread": steady, "mean": mean_steady })
	# At 60 fps the 100 Hz pulse is sampled at three phases (0, 120, 240 degrees): linear factors ~1.98, 0.51, 0.51,
	# which the tonemapper compresses (measured 0.47 on the 4080). 0.3 still tells a beat from a steady light (0.0).
	_record("flicker_beats_at_1_1000_s", beating > 0.3 and mean_beating > 0.01 and mean_beating < 0.95, { "spread": beating, "mean": mean_beating })
	# 59 frame pairs at 60 fps: a 24 fps hold changes its image about 24 times, a free camera every frame.
	_record("hold_24_fps_changes_24_times_a_second", abs(hold_changes - 24) <= 1, { "changes": hold_changes })
	_record("no_hold_changes_every_frame", free_changes >= 58, { "changes": free_changes })
	# light_specular_size widens the catchlight: more pixels near its peak, and the peak no longer clipped.
	var point: Dictionary = spec_peak[0.0]
	var sized: Dictionary = spec_peak[0.6]
	_record("specular_size_widens_the_highlight", sized.area > point.area * 4 and sized.peak < 0.999, { "point": point, "sized": sized })
	var passed := 0
	for k: String in results:
		passed += int(results[k].ok)
	print("CT RESULT " + JSON.stringify({ "passed": passed, "total": results.size(), "results": results }))
	print("CT DONE")
