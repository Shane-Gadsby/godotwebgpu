extends SceneTree
# Task 39: the editor tick boxes. Renders into a SubViewport (as the editor's camera previews do), the same in a game run
# and in an editor-hint run (--editor -s motion.gd). Arguments after --, key=value:
#   out=<png>  test=blur|rolling|shake  fx=0|1 (the effect enabled)  vis=0|1 (its Visible in Editor tick box)
# blur: a white square crossing the frame at 10 m/s, 360 degree shutter. rolling: the camera yaws at 2 rad/s past white
# bars, 30 ms readout. shake: shake on/off; prints how many distinct shake offsets and camera transforms were seen.

var opts := {}
var frames := 0
var sv: SubViewport
var cam: Camera3D
var mover: Node3D
var offsets := {}
var cam_xforms := {}

func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		opts[kv[0]] = kv[1] if kv.size() > 1 else "1"
	print("EDITOR_HINT=", Engine.is_editor_hint())
	sv = SubViewport.new()
	sv.size = Vector2i(640, 360)
	sv.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	sv.own_world_3d = true
	root.add_child(sv)
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var we := WorldEnvironment.new()
	we.environment = env
	sv.add_child(we)
	var white := StandardMaterial3D.new()
	white.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	white.albedo_color = Color(1, 1, 1)
	var test: String = opts.get("test", "blur")
	var fx: bool = opts.get("fx", "0") == "1"
	var vis: bool = opts.get("vis", "0") == "1"
	var attrs := CameraAttributesPractical.new()
	if test == "blur":
		attrs.motion_blur_enabled = fx
		attrs.motion_blur_shutter_angle = 360.0
		attrs.motion_blur_max_length = 0.25
		attrs.set("motion_blur_visible_in_editor", vis)
		mover = MeshInstance3D.new()
		var q := QuadMesh.new()
		q.size = Vector2(1, 1)
		(mover as MeshInstance3D).mesh = q
		(mover as MeshInstance3D).material_override = white
		mover.position = Vector3(-2, 0, -5)
		sv.add_child(mover)
	elif test == "rolling":
		attrs.rolling_shutter_enabled = fx
		attrs.rolling_shutter_readout_time = 30.0
		attrs.set("rolling_shutter_visible_in_editor", vis)
		for i in range(-6, 7):
			var bar := MeshInstance3D.new()
			var q := QuadMesh.new()
			q.size = Vector2(0.2, 20)
			bar.mesh = q
			bar.material_override = white
			bar.position = Vector3(i * 1.5, 0, -5)
			sv.add_child(bar)
	else:
		for i in range(-6, 7):
			var box := MeshInstance3D.new()
			box.mesh = BoxMesh.new()
			box.material_override = white
			box.position = Vector3(i * 1.5, (i % 3) * 0.7, -6)
			sv.add_child(box)
	cam = Camera3D.new()
	cam.fov = 60.0
	cam.attributes = attrs
	if test == "shake":
		cam.shake_enabled = fx
		cam.set("shake_visible_in_editor", vis)
	sv.add_child(cam)
	cam.make_current()

func _process(delta: float) -> bool:
	frames += 1
	var test: String = opts.get("test", "blur")
	if test == "blur":
		mover.position.x += 10.0 * delta
	elif test == "rolling":
		cam.rotation.y -= 2.0 * delta
	else:
		offsets[str(cam.get_shake_offset())] = true
		cam_xforms[str(cam.get_camera_transform())] = true
	if frames == int(opts.get("frames", 24)):
		if test == "shake":
			print("SHAKE distinct_offsets=%d distinct_camera_transforms=%d node_transform_identity=%s" % [offsets.size(), cam_xforms.size(), str(cam.global_transform == Transform3D())])
		var img := sv.get_texture().get_image()
		img.save_png(opts["out"])
		print("SAVED ", opts["out"])
		return true
	return false
