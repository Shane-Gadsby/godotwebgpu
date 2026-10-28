extends SceneTree
# Handheld shake: records get_shake_offset() of several cameras for N seconds at a fixed 60 fps and writes it as JSON for
# shake_analyze.py; checks here that the node transforms stay untouched and that get_camera_transform() = global * offset.

var fails := 0

func check(name: String, ok: bool, detail: String) -> void:
	print(("PASS " if ok else "FAIL ") + name + "  " + detail)
	if not ok:
		fails += 1

func _initialize() -> void:
	run.call_deferred()

func cam_with(seed: int, is_strength: float, breathing: float, scale_fl: bool = true, fov: float = 75.0) -> Camera3D:
	var c := Camera3D.new()
	c.fov = fov
	c.shake_seed = seed
	c.shake_rotation_amplitude = Vector3(0.3, 0.2, 0.1)
	c.shake_rotation_frequency = Vector3(2.0, 3.0, 1.5)
	c.shake_position_amplitude = Vector3(0.002, 0.001, 0.003)
	c.shake_position_frequency = Vector3(1.0, 1.5, 4.0)
	c.shake_breathing_amplitude = breathing
	c.shake_breathing_frequency = 0.25
	c.shake_stabilization = is_strength
	c.shake_scale_with_focal_length = scale_fl
	c.position = Vector3(1, 2, 3)
	c.rotation = Vector3(0.1, 0.2, 0.0)
	root.add_child(c)
	c.shake_enabled = true
	return c

func run() -> void:
	var secs := 120.0
	var cams := {
		"base": cam_with(7, 0.0, 0.0),
		"same_seed": cam_with(7, 0.0, 0.0),
		"other_seed": cam_with(8, 0.0, 0.0),
		"is50": cam_with(7, 0.5, 0.0),
		"is90": cam_with(7, 0.9, 0.0),
		"breath": cam_with(7, 0.0, 0.15),
		"fl_off_tele": cam_with(7, 0.0, 0.0, false, 13.4958),  # Half the 50 mm reference FOV.
	}
	cams["breath"].shake_rotation_amplitude = Vector3()
	cams["breath"].shake_position_amplitude = Vector3()
	var control := cam_with(7, 0.0, 0.0)
	control.shake_enabled = false
	var data := {}
	for k in cams:
		data[k] = []
	var untouched := true
	var composed := true
	var n := int(secs * 60)
	for i in n:
		await process_frame
		for k in cams:
			var c: Camera3D = cams[k]
			var o: Transform3D = c.get_shake_offset()
			var e := o.basis.get_euler()
			data[k].append([rad_to_deg(e.x), rad_to_deg(e.y), rad_to_deg(e.z), o.origin.x, o.origin.y, o.origin.z])
			# Bit-exact against an identical camera with the shake off.
			if c.global_transform != control.global_transform or c.transform != control.transform:
				untouched = false
			var expect: Transform3D = c.global_transform.orthonormalized() * o
			if not c.get_camera_transform().is_equal_approx(expect):
				composed = false
	check("Shake: node transform untouched over %d frames" % n, untouched, "")
	check("Shake: get_camera_transform() == global_transform * get_shake_offset()", composed, "")
	check("Shake: same seed identical", data["base"] == data["same_seed"], "")
	check("Shake: other seed differs", data["base"] != data["other_seed"], "")
	cams["base"].shake_enabled = false
	await process_frame
	check("Shake: disabling restores the plain transform", cams["base"].get_camera_transform().is_equal_approx(cams["base"].global_transform) and cams["base"].get_shake_offset() == Transform3D(), "")
	var f := FileAccess.open(OS.get_cmdline_user_args()[0], FileAccess.WRITE)
	f.store_string(JSON.stringify({"fps": 60, "data": data}))
	f.close()
	print("SHAKE TESTS DONE, failures: %d" % fails)
	quit(1 if fails > 0 else 0)
