extends Node
# Task 62 verification: Camera3D.get_monitoring_histogram() against a CPU count of the same frame, overlays, drawing
# off, two cameras, throttling, frame time. Prints "RESULT <json>" lines; natively quits at the end.
# Mode: user arg --mode=<m> natively, the URL hash on the web (#perf_every), default "suite".
# Modes: suite | default (no histogram at all, for timing) | perf_every | perf_quarter (idle with that config).

var cam: Camera3D
var signals := 0
var mode := "suite"
var out_dir := ""

func result(name: String, data: Dictionary) -> void:
	data["test"] = name
	print("RESULT ", JSON.stringify(data))

func frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame

func _on_hist() -> void:
	signals += 1

func make_texture() -> ImageTexture:
	# Every 8-bit value in each channel, in different arrangements, so all bins of R', G', B' and many of Y' fill.
	var img := Image.create(256, 256, false, Image.FORMAT_RGB8)
	for y in 256:
		for x in 256:
			img.set_pixel(x, y, Color8(x, y, (x * 7 + y * 13) & 255))
	return ImageTexture.create_from_image(img)

func build_scene(parent: Node, bg: Color, with_quad: bool) -> Camera3D:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = bg
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var we := WorldEnvironment.new()
	we.environment = env
	parent.add_child(we)
	if with_quad:
		var mi := MeshInstance3D.new()
		var q := QuadMesh.new()
		q.size = Vector2(1.6, 1.6)
		mi.mesh = q
		var m := StandardMaterial3D.new()
		m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		m.albedo_texture = make_texture()
		m.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
		mi.material_override = m
		parent.add_child(mi)
	var c := Camera3D.new()
	c.projection = Camera3D.PROJECTION_ORTHOGONAL
	c.size = 2.0
	c.position = Vector3(0, 0, 2)
	parent.add_child(c)
	c.make_current()
	return c

func cpu_hist(img: Image) -> Array:
	# Same rule as camera_histogram.glsl: bin = round(value * 255), luma = Rec. 709 of the display-encoded values.
	var h := []
	for c in 4:
		var a := PackedInt32Array()
		a.resize(256)
		a.fill(0)
		h.append(a)
	var data := img.get_data()
	var bpp := 4 if img.get_format() == Image.FORMAT_RGBA8 else 3
	for i in range(0, data.size(), bpp):
		var r := data[i]
		var g := data[i + 1]
		var b := data[i + 2]
		h[1][r] += 1
		h[2][g] += 1
		h[3][b] += 1
		h[0][int(round(0.2126 * r + 0.7152 * g + 0.0722 * b))] += 1
	return h

func gpu_hist(c: Camera3D) -> Array:
	return [c.get_monitoring_histogram(Camera3D.MONITORING_HISTOGRAM_CHANNEL_LUMA), c.get_monitoring_histogram(Camera3D.MONITORING_HISTOGRAM_CHANNEL_RED),
		c.get_monitoring_histogram(Camera3D.MONITORING_HISTOGRAM_CHANNEL_GREEN), c.get_monitoring_histogram(Camera3D.MONITORING_HISTOGRAM_CHANNEL_BLUE)]

func compare(a: Array, b: Array) -> Dictionary:
	var names := ["Y", "R", "G", "B"]
	var d := {}
	for c in 4:
		var max_diff := 0
		var bins_diff := 0
		var total_diff := 0
		if a[c].size() != 256 or b[c].size() != 256:
			d[names[c]] = {"error": "size %d/%d" % [a[c].size(), b[c].size()]}
			continue
		for i in 256:
			var df: int = abs(a[c][i] - b[c][i])
			max_diff = max(max_diff, df)
			total_diff += df
			if df > 0:
				bins_diff += 1
		d[names[c]] = {"max_bin_diff": max_diff, "bins_differing": bins_diff, "sum_abs_diff": total_diff}
	return d

func sum(a: PackedInt32Array) -> int:
	var s := 0
	for v in a:
		s += v
	return s

func wait_signals(n: int, max_frames := 600) -> int:
	var start := signals
	var f := 0
	while signals - start < n and f < max_frames:
		await get_tree().process_frame
		f += 1
	return f

func image_diff(a: Image, b: Image) -> int:
	if a == null or b == null:
		return -2 # get_image() on the web can return null (asynchronous texture readback, TASKS §3 trap 93).
	var da := a.get_data()
	var db := b.get_data()
	if da.size() != db.size():
		return -1
	var n := 0
	for i in da.size():
		if da[i] != db[i]:
			n += 1
	return n

func save(img: Image, name: String) -> void:
	if out_dir != "":
		img.save_png(out_dir.path_join(name + ".png"))

func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--mode="):
			mode = a.substr(7)
		elif a.begins_with("--out="):
			out_dir = a.substr(6)
	if OS.has_feature("web"):
		var h = JavaScriptBridge.eval("location.hash", true)
		if h is String and h.length() > 1:
			mode = h.substr(1)
	cam = build_scene(self, Color(0.2, 0.3, 0.4), true)
	cam.monitoring_histogram_updated.connect(_on_hist)
	match mode:
		"suite":
			await suite()
		"default":
			pass # No histogram: the frame-time baseline.
		"perf_every", "perf_quarter":
			cam.monitoring_histogram_enabled = true
			cam.monitoring_histogram_draw = false
			cam.monitoring_histogram_update_interval = 0.0 if mode == "perf_every" else 0.25
			await frames(120)
			result("perf_mode", {"mode": mode, "signals_in_120_frames": signals, "luma_sum": sum(cam.get_monitoring_histogram())})
	if OS.has_feature("web"):
		return
	if mode != "suite":
		await timing()
	get_tree().quit()

func timing() -> void:
	# Frame time natively (uncapped): the process loop's wall time over 600 frames.
	Engine.max_fps = 0
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	await frames(60)
	var t0 := Time.get_ticks_usec()
	var s0 := signals
	await frames(600)
	var dt := (Time.get_ticks_usec() - t0) / 600.0
	result("timing", {"mode": mode, "us_per_frame": dt, "signals": signals - s0})

func suite() -> void:
	var vp := get_viewport()
	var size := vp.get_texture().get_size()
	var renderer: String = RenderingServer.get_current_rendering_method()
	await frames(10)
	var base := vp.get_texture().get_image()
	save(base, "base")

	# 1. Not enabled: empty, and connecting alone does nothing.
	result("disabled_empty", {"size": cam.get_monitoring_histogram().size(), "signals": signals})

	# 2. Counts vs a CPU count, drawing off, Visible in Output off. Record when the first data arrives.
	cam.monitoring_histogram_enabled = true
	cam.monitoring_histogram_draw = false
	var empty_frames := 0
	var f := 0
	while f < 300:
		await get_tree().process_frame
		f += 1
		if cam.get_monitoring_histogram().is_empty():
			empty_frames += 1
		else:
			break
	await wait_signals(3)
	var img := vp.get_texture().get_image()
	var g := gpu_hist(cam)
	var cpu := cpu_hist(img)
	save(img, "nodraw_hidden")
	result("counts", {"renderer": renderer, "w": size.x, "h": size.y, "empty_frames_before_first": empty_frames, "luma_sum": sum(g[0]), "expected_sum": int(size.x * size.y),
		"vs_cpu": compare(g, cpu), "image_vs_base_px": image_diff(img, base), "signals": signals})
	var counts_ref := g

	# 3. Count without drawing, Visible in Output on: no overlay, counts still update.
	cam.monitoring_visible_in_output = true
	var s0 := signals
	await wait_signals(3)
	img = vp.get_texture().get_image()
	result("nodraw_visible", {"image_vs_base_px": image_diff(img, base), "new_signals": signals - s0, "vs_ref": compare(gpu_hist(cam), counts_ref)})

	# 4. With zebras, false colour and focus peaking drawn: counts unchanged, image changed.
	cam.monitoring_zebras_enabled = true
	cam.monitoring_zebras_level = 50.0
	cam.monitoring_false_color_enabled = true
	cam.monitoring_focus_peaking_enabled = true
	await frames(4)
	await wait_signals(3)
	img = vp.get_texture().get_image()
	save(img, "overlays")
	result("overlays", {"image_vs_base_px": image_diff(img, base), "vs_ref": compare(gpu_hist(cam), counts_ref)})
	cam.monitoring_zebras_enabled = false
	cam.monitoring_false_color_enabled = false
	cam.monitoring_focus_peaking_enabled = false

	# 5. Drawn histogram (the default draw = true) with readback: drawn in the corner, counts unchanged.
	cam.monitoring_histogram_draw = true
	await frames(4)
	await wait_signals(3)
	img = vp.get_texture().get_image()
	save(img, "drawn")
	result("drawn", {"image_vs_base_px": image_diff(img, base), "vs_ref": compare(gpu_hist(cam), counts_ref)})
	cam.monitoring_histogram_draw = false
	cam.monitoring_visible_in_output = false

	# 6. Throttling: signals per second at 0.25 s and at every frame, over 3 s of wall time.
	for interval in [0.25, 0.0]:
		cam.monitoring_histogram_update_interval = interval
		await frames(10)
		var t0 := Time.get_ticks_msec()
		var sig0 := signals
		var fr := 0
		while Time.get_ticks_msec() - t0 < 3000:
			await get_tree().process_frame
			fr += 1
		var secs := (Time.get_ticks_msec() - t0) / 1000.0
		result("throttle", {"interval": interval, "signals_per_s": (signals - sig0) / secs, "frames_per_s": fr / secs})

	# 7. Two cameras in two SubViewports with their own worlds: one black, one white.
	var cams := []
	var sigs := [0, 0]
	for i in 2:
		var svc := SubViewportContainer.new()
		var sv := SubViewport.new()
		sv.size = Vector2i(320, 180)
		sv.own_world_3d = true
		sv.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		svc.add_child(sv)
		add_child(svc)
		var c := build_scene(sv, Color.BLACK if i == 0 else Color.WHITE, false)
		c.monitoring_histogram_enabled = true
		c.monitoring_histogram_draw = false
		c.monitoring_histogram_updated.connect(func(): sigs[i] += 1)
		cams.append(c)
	var waited := 0
	while (sigs[0] < 3 or sigs[1] < 3) and waited < 600:
		await get_tree().process_frame
		waited += 1
	var hb: PackedInt32Array = cams[0].get_monitoring_histogram()
	var hw: PackedInt32Array = cams[1].get_monitoring_histogram()
	result("two_cameras", {"black_bin0": hb[0] if hb.size() == 256 else -1, "black_sum": sum(hb), "white_bin255": hw[255] if hw.size() == 256 else -1,
		"white_sum": sum(hw), "expected": 320 * 180, "signals": sigs, "main_vs_ref": compare(gpu_hist(cam), counts_ref)})

	# 8. Freeing a camera with readbacks in flight: no errors (checked in the log).
	cams[0].get_parent().get_parent().queue_free()
	await frames(10)
	result("done", {})
