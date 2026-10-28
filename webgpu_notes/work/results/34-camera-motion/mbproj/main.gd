extends Node3D
# Motion blur / rolling shutter verification scene, built in code. Arguments (after --) as key=value:
#   test=blur|rolling|sky   attrs=practical|physical   angle=deg   shutter=1/s   readout=ms   blur=0|1
#   rolling=0|1   frames=N   out=path.png   quality=0..2   speed=units/s (blur)   omega=rad/s (rolling, sky)
#   msaa=0..3   taa=0|1
# On the web there are no arguments: the defaults (or ?args in the URL hash) apply and the page captures the canvas.

var args := {}
var frame := 0
var gpu_sum := 0.0
var gpu_on := 0.0
var n_on := 0
var n_off := 0
var web_done := false
var mover: Node3D
var cam: Camera3D

func arg(k: String, d):
	return args.get(k, d)

func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			args[kv[0]] = kv[1]
	if OS.has_feature("web"):
		var h = JavaScriptBridge.eval("window.location.hash.substring(1)")
		if h is String:
			for a in (h as String).split("&", false):
				var kv := a.split("=", true, 1)
				if kv.size() == 2:
					args[kv[0]] = kv[1]
	print("ARGS ", args)
	var test: String = arg("test", "blur")
	get_viewport().msaa_3d = int(arg("msaa", "0"))
	get_viewport().use_taa = arg("taa", "0") == "1"

	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	if test == "sky":
		env.background_mode = Environment.BG_SKY
		var sky := Sky.new()
		var mat := PanoramaSkyMaterial.new()
		var img := Image.create(2048, 1024, false, Image.FORMAT_RGBA8)
		img.fill(Color(0, 0, 0))
		# A thin white stripe every 30 degrees of longitude (2048 px = 360 degrees), 6 px wide.
		for i in range(12):
			var x0 := int(i * 2048 / 12.0)
			img.fill_rect(Rect2i(x0, 0, 6, 1024), Color(1, 1, 1))
		mat.panorama = ImageTexture.create_from_image(img)
		mat.filter = false
		sky.sky_material = mat
		env.sky = sky
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)

	cam = Camera3D.new()
	add_child(cam)
	var attrs: CameraAttributes
	if arg("attrs", "practical") == "physical":
		var p := CameraAttributesPhysical.new()
		p.exposure_shutter_speed = float(arg("shutter", "50"))
		attrs = p
	else:
		attrs = CameraAttributesPractical.new()
	attrs.set("motion_blur_enabled", arg("blur", "0") == "1")
	attrs.set("motion_blur_shutter_angle", float(arg("angle", "180")))
	attrs.set("motion_blur_max_length", float(arg("max", "0.25")))
	attrs.set("motion_blur_strength", float(arg("strength", "1")))
	attrs.set("rolling_shutter_enabled", arg("rolling", "0") == "1")
	attrs.set("rolling_shutter_readout_time", float(arg("readout", "20")))
	cam.attributes = attrs
	if RenderingServer.has_method("camera_attributes_set_motion_blur_quality"):
		RenderingServer.call("camera_attributes_set_motion_blur_quality", int(arg("quality", "1")))

	var white := StandardMaterial3D.new()
	white.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	white.albedo_color = Color(1, 1, 1)

	if test == "look":
		# A lit street of pillars under a procedural sky, a spinning wheel and a red box driving past, filmed by a
		# camera panning to follow it.
		env.background_mode = Environment.BG_SKY
		env.sky = Sky.new()
		env.sky.sky_material = ProceduralSkyMaterial.new()
		env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
		var sun := DirectionalLight3D.new()
		sun.rotation = Vector3(deg_to_rad(-40), deg_to_rad(30), 0)
		sun.shadow_enabled = true
		add_child(sun)
		var ground := MeshInstance3D.new()
		var pm := PlaneMesh.new()
		pm.size = Vector2(200, 200)
		ground.mesh = pm
		var gm := StandardMaterial3D.new()
		gm.albedo_color = Color(0.35, 0.36, 0.38)
		ground.material_override = gm
		add_child(ground)
		for i in range(-12, 13):
			var pillar := MeshInstance3D.new()
			var bm := BoxMesh.new()
			bm.size = Vector3(0.6, 6, 0.6)
			pillar.mesh = bm
			var pmat := StandardMaterial3D.new()
			pmat.albedo_color = Color.from_hsv(fposmod(i * 0.13, 1.0), 0.6, 0.9)
			pillar.material_override = pmat
			pillar.position = Vector3(i * 3.0, 3, -12)
			add_child(pillar)
		var wheel := MeshInstance3D.new()
		var cm := CylinderMesh.new()
		cm.top_radius = 1.2
		cm.bottom_radius = 1.2
		cm.height = 0.3
		wheel.mesh = cm
		var wm := StandardMaterial3D.new()
		wm.albedo_texture = _checker()
		wheel.material_override = wm
		wheel.rotation.x = PI / 2
		wheel.position = Vector3(4, 1.5, -6)
		wheel.name = "Wheel"
		add_child(wheel)
		var car := MeshInstance3D.new()
		var cb := BoxMesh.new()
		cb.size = Vector3(3, 1.2, 1.5)
		car.mesh = cb
		var carm := StandardMaterial3D.new()
		carm.albedo_color = Color(0.9, 0.15, 0.1)
		carm.metallic = 0.6
		carm.roughness = 0.3
		car.material_override = carm
		car.position = Vector3(-10, 0.6, -4)
		add_child(car)
		mover = car
		cam.fov = 50.0
		cam.position = Vector3(0, 1.6, 6)
	elif test == "blur":
		# Orthographic, 10 units tall: a 2x2 white square moving right at `speed` units/s over black.
		cam.projection = Camera3D.PROJECTION_ORTHOGONAL
		cam.size = 10.0
		cam.position = Vector3(0, 0, 10)
		var mi := MeshInstance3D.new()
		var q := QuadMesh.new()
		q.size = Vector2(2, 2)
		mi.mesh = q
		mi.material_override = white
		mi.position = Vector3(-6, 0, 0)
		add_child(mi)
		mover = mi
	else:
		cam.fov = 60.0
		cam.position = Vector3(0, 0, 0)
		if test == "rolling":
			# Thin vertical white bars on a 20-unit-radius circle around the camera, every 10 degrees.
			for i in range(36):
				var a := deg_to_rad(i * 10.0)
				var bar := MeshInstance3D.new()
				var b := BoxMesh.new()
				b.size = Vector3(0.12, 40, 0.12)
				bar.mesh = b
				bar.material_override = white
				bar.position = Vector3(sin(a) * 20.0, 0, -cos(a) * 20.0)
				add_child(bar)
		mover = cam

func _process(delta: float) -> void:
	var test: String = arg("test", "blur")
	if test == "look":
		mover.position.x += float(arg("speed", "12")) * delta
		if mover.position.x > 12.0:
			mover.position.x -= 24.0
		get_node("Wheel").rotate_object_local(Vector3.UP, 8.0 * delta)
		cam.look_at(mover.position + Vector3(0, 0.4, 0))
	elif test == "blur":
		mover.position.x += float(arg("speed", "30")) * delta
		if mover.position.x > 8.0:
			mover.position.x -= 16.0
	else:
		# Pan right: yaw the camera clockwise seen from above.
		cam.rotation.y -= float(arg("omega", "1.0")) * delta
	frame += 1
	if arg("time", "0") == "ab":
		# In-process A/B: the effect toggles every 50 frames, so clock and load drift hit both halves alike.
		# Frames 200-1799; the first 10 frames after each toggle are skipped (pipeline and motion settle).
		var rid := get_viewport().get_viewport_rid()
		if frame == 1:
			RenderingServer.viewport_set_measure_render_time(rid, true)
		var on := (frame / 50) % 2 == 1
		if frame % 50 == 0:
			cam.attributes.set("motion_blur_enabled", on and arg("ab_blur", "1") == "1")
			cam.attributes.set("rolling_shutter_enabled", on and arg("ab_rolling", "0") == "1")
			get_viewport().use_taa = on and arg("ab_taa", "0") == "1"
			if arg("ab_dof", "0") == "1":
				cam.attributes.set("dof_blur_far_enabled", on)
		elif frame >= 200 and frame < 1800 and frame % 50 >= 10:
			var t := RenderingServer.viewport_get_measured_render_time_gpu(rid)
			if on:
				gpu_on += t
				n_on += 1
			else:
				gpu_sum += t
				n_off += 1
		if frame == 1800:
			print("AB_MS off=", gpu_sum / n_off, " on=", gpu_on / n_on, " delta=", gpu_on / n_on - gpu_sum / n_off)
			get_tree().quit()
		return
	if arg("time", "0") == "1":
		# GPU time of the whole viewport, averaged over frames 200-799.
		if frame == 1:
			RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
		elif frame >= 200 and frame < 800:
			gpu_sum += RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid())
		elif frame == 800:
			print("GPU_MS ", gpu_sum / 600.0)
			get_tree().quit()
		return
	if str(frame) in str(arg("seq", "")).split(",") and not OS.has_feature("web"):
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(str(arg("out", "/tmp/mb.png")).replace(".png", "_%03d.png" % frame))
	if not web_done and frame >= int(arg("frames", "20")) and OS.has_feature("web") and (test != "blur" or absf(mover.position.x) < 3.0):
		# Viewport readback is not available here; the page screenshots the canvas on this line.
		print("[MB] READY delta=", delta, " x=", mover.position.x)
	if frame == int(arg("frames", "20")) and not OS.has_feature("web"):
		await RenderingServer.frame_post_draw
		var img := get_viewport().get_texture().get_image()
		img.save_png(arg("out", "/tmp/mb.png"))
		print("SAVED ", arg("out", ""), " pos=", mover.position, " rot=", cam.rotation, " delta=", delta)
		get_tree().quit()


func _checker() -> ImageTexture:
	var img := Image.create(256, 256, false, Image.FORMAT_RGBA8)
	for y in range(256):
		for x in range(256):
			img.set_pixel(x, y, Color(1, 1, 1) if ((x / 32) + (y / 32)) % 2 == 0 else Color(0.1, 0.1, 0.1))
	img.generate_mipmaps()
	return ImageTexture.create_from_image(img)
