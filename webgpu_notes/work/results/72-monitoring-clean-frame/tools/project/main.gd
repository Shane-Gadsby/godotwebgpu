extends Node
# Task 72 verification: Camera3D.get_monitoring_clean_texture() against the same frame rendered with the overlays off;
# two viewports; release; memory and GPU cost. Prints "RESULT <json>" lines; natively quits at the end.
# Mode: user arg --mode=<m> natively, the URL hash on the web; default "suite".

var mode := "suite"
var out_dir := ""
var failures := 0


func result(name: String, data: Dictionary) -> void:
	data["test"] = name
	if data.has("pass") and not data["pass"]:
		failures += 1
	print("RESULT ", JSON.stringify(data))


func frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


# Reads several textures back from the same frame; returns their Images (null where a read failed).
func read_all(textures: Array) -> Array:
	var images := []
	images.resize(textures.size())
	var pending := [textures.size()]
	for i in textures.size():
		var idx := i
		textures[i].get_image_async(func(img: Image):
			images[idx] = img
			pending[0] -= 1)
	var waited := 0
	while pending[0] > 0 and waited < 600:
		await get_tree().process_frame
		waited += 1
	return images


func diff(a: Image, b: Image) -> Dictionary:
	if a == null or b == null:
		return {"null": true, "diff_px": -1}
	if a.get_size() != b.get_size():
		return {"size_a": str(a.get_size()), "size_b": str(b.get_size()), "diff_px": -1}
	if a.get_format() != b.get_format():
		return {"format_a": a.get_format(), "format_b": b.get_format(), "diff_px": -1}
	var da := a.get_data()
	var db := b.get_data()
	if da == db:
		return {"diff_px": 0}
	var bpp := da.size() / (a.get_width() * a.get_height())
	var n := 0
	var maxd := 0
	for i in range(0, da.size(), bpp):
		var differs := false
		for c in bpp:
			var d := absi(da[i + c] - db[i + c])
			if d > 0:
				differs = true
				maxd = maxi(maxd, d)
		if differs:
			n += 1
	return {"diff_px": n, "max": maxd}


func save(img: Image, name: String) -> void:
	if img and not out_dir.is_empty():
		img.save_png(out_dir.path_join(name + ".png"))


func make_chart() -> ImageTexture:
	var img := Image.create(256, 256, false, Image.FORMAT_RGB8)
	for y in 256:
		for x in 256:
			img.set_pixel(x, y, Color8(x, y, (x * 7 + y * 13) & 255))
	return ImageTexture.create_from_image(img)


# A SubViewport with its own world: sky, sun with shadows, a few lit objects and an unshaded chart, so the frame has
# highlights for zebras, edges for peaking and every level for false color.
func make_view(size: Vector2i, bg_only := false, bg := Color.BLACK) -> Dictionary:
	var sv := SubViewport.new()
	sv.size = size
	sv.own_world_3d = true
	sv.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(sv)
	var env := Environment.new()
	if bg_only:
		env.background_mode = Environment.BG_COLOR
		env.background_color = bg
		env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	else:
		env.background_mode = Environment.BG_SKY
		env.sky = Sky.new()
		env.sky.sky_material = ProceduralSkyMaterial.new()
		env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	var we := WorldEnvironment.new()
	we.environment = env
	sv.add_child(we)
	if not bg_only:
		var sun := DirectionalLight3D.new()
		sun.rotation_degrees = Vector3(-50, 30, 0)
		sun.shadow_enabled = true
		sv.add_child(sun)
		for i in 5:
			var mi := MeshInstance3D.new()
			mi.mesh = SphereMesh.new() if i % 2 == 0 else BoxMesh.new()
			mi.position = Vector3(i * 1.3 - 2.6, 0.5, -i * 0.5)
			var m := StandardMaterial3D.new()
			m.albedo_color = Color.from_hsv(i / 5.0, 0.7, 0.9)
			mi.material_override = m
			sv.add_child(mi)
		var floor_mi := MeshInstance3D.new()
		floor_mi.mesh = PlaneMesh.new()
		floor_mi.mesh.size = Vector2(20, 20)
		sv.add_child(floor_mi)
		var chart := MeshInstance3D.new()
		chart.mesh = QuadMesh.new()
		chart.mesh.size = Vector2(1.5, 1.5)
		chart.position = Vector3(2.2, 1.6, 0.5)
		var cm := StandardMaterial3D.new()
		cm.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		cm.albedo_texture = make_chart()
		cm.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
		chart.material_override = cm
		sv.add_child(chart)
	var cam := Camera3D.new()
	cam.position = Vector3(0, 2, 6)
	cam.rotation_degrees = Vector3(-15, 0, 0)
	sv.add_child(cam)
	cam.make_current()
	return {"sv": sv, "cam": cam}


func set_overlays(cam: Camera3D, cfg: String) -> void:
	cam.monitoring_visible_in_output = true
	cam.monitoring_zebras_enabled = cfg in ["all", "zebras"]
	cam.monitoring_zebras_level = 50.0
	cam.monitoring_zebras_second_enabled = cfg == "all"
	cam.monitoring_focus_peaking_enabled = cfg == "all"
	cam.monitoring_histogram_enabled = cfg in ["all", "histogram"]
	cam.monitoring_false_color_enabled = cfg == "false_color"


func overlays_off(cam: Camera3D) -> void:
	cam.monitoring_zebras_enabled = false
	cam.monitoring_zebras_second_enabled = false
	cam.monitoring_focus_peaking_enabled = false
	cam.monitoring_histogram_enabled = false
	cam.monitoring_false_color_enabled = false


# The clean texture of a frame with overlays equals the viewport's frame with the overlays off.
func test_clean_vs_off(cfg: String, attributes: String) -> void:
	var v := make_view(Vector2i(640, 360))
	var sv: SubViewport = v.sv
	var cam: Camera3D = v.cam
	if attributes == "nr_sharpen":
		var attr := CameraAttributesPractical.new()
		attr.noise_reduction_enabled = true
		attr.sharpening_enabled = true
		cam.attributes = attr
	set_overlays(cam, cfg)
	cam.monitoring_clean_texture_enabled = true
	await frames(12)
	var on: Array = await read_all([cam.get_monitoring_clean_texture(), sv.get_texture()])
	overlays_off(cam)
	await frames(12)
	var off: Array = await read_all([cam.get_monitoring_clean_texture(), sv.get_texture()])
	var name := cfg + ("_" + attributes if attributes != "" else "")
	# Compatibility has no histogram (no compute): nothing is drawn for it there.
	var overlay_drawn := not (cfg == "histogram" and RenderingServer.get_current_rendering_method() == "gl_compatibility")
	save(on[0], name + "_clean")
	save(on[1], name + "_overlay")
	save(off[1], name + "_off")
	var d_clean_off := diff(on[0], off[1])
	var d_clean2_off := diff(off[0], off[1])
	var d_overlay_off := diff(on[1], off[1])
	result("clean_vs_off", {"cfg": cfg, "attributes": attributes, "clean_vs_off": d_clean_off, "clean_without_overlays_vs_off": d_clean2_off,
		"overlay_vs_off": d_overlay_off, "format": on[0].get_format() if on[0] else -1, "size": str(on[0].get_size()) if on[0] else "",
		"pass": d_clean_off.diff_px == 0 and d_clean2_off.diff_px == 0 and (d_overlay_off.diff_px > 0 or not overlay_drawn)})
	cam.monitoring_clean_texture_enabled = false
	sv.queue_free()
	await frames(2)


# The documented pattern: enable, read in the same function, disable in the callback.
func test_same_frame() -> void:
	var v := make_view(Vector2i(640, 360))
	var sv: SubViewport = v.sv
	var cam: Camera3D = v.cam
	await frames(12)
	var off: Array = await read_all([sv.get_texture()])
	set_overlays(cam, "all")
	await frames(12)
	cam.monitoring_clean_texture_enabled = true
	var tex := cam.get_monitoring_clean_texture()
	var box := [null, false]
	tex.get_image_async(func(img: Image):
		cam.monitoring_clean_texture_enabled = false
		box[0] = img
		box[1] = true)
	var waited := 0
	while not box[1] and waited < 600:
		await get_tree().process_frame
		waited += 1
	var d := diff(box[0], off[0])
	var released: Array = await read_all([tex])
	result("same_frame", {"clean_vs_off": d, "frames_waited": waited, "enabled_after": cam.monitoring_clean_texture_enabled,
		"texture_after": cam.get_monitoring_clean_texture() == null, "kept_texture_reads_null": released[0] == null,
		"pass": d.diff_px == 0 and not cam.monitoring_clean_texture_enabled and released[0] == null})
	sv.queue_free()
	await frames(2)


# Two viewports, each with its own camera and clean texture; then one is resized.
func test_two_viewports() -> void:
	var a := make_view(Vector2i(320, 180), true, Color(0.9, 0.85, 0.8)) # Above the 50 IRE zebras.
	var b := make_view(Vector2i(400, 240))
	for v in [a, b]:
		set_overlays(v.cam, "all")
		v.cam.monitoring_clean_texture_enabled = true
	await frames(12)
	var on: Array = await read_all([a.cam.get_monitoring_clean_texture(), b.cam.get_monitoring_clean_texture(), a.sv.get_texture(), b.sv.get_texture()])
	for v in [a, b]:
		overlays_off(v.cam)
	await frames(12)
	var off: Array = await read_all([a.sv.get_texture(), b.sv.get_texture()])
	var da := diff(on[0], off[0])
	var db := diff(on[1], off[1])
	var da_ov := diff(on[2], off[0])
	var db_ov := diff(on[3], off[1])
	# Resize b with overlays back on: the clean texture follows the new size.
	set_overlays(b.cam, "all")
	b.sv.size = Vector2i(512, 288)
	await frames(12)
	var on2: Array = await read_all([b.cam.get_monitoring_clean_texture()])
	overlays_off(b.cam)
	await frames(12)
	var off2: Array = await read_all([b.sv.get_texture()])
	var dr := diff(on2[0], off2[0])
	result("two_viewports", {"a_clean_vs_off": da, "b_clean_vs_off": db, "a_overlay_vs_off": da_ov, "b_overlay_vs_off": db_ov,
		"b_resized_clean_vs_off": dr, "b_resized_size": str(on2[0].get_size()) if on2[0] else "",
		"texture_sizes": [str(a.cam.get_monitoring_clean_texture().get_size()), str(b.cam.get_monitoring_clean_texture().get_size())],
		"pass": da.diff_px == 0 and db.diff_px == 0 and dr.diff_px == 0 and da_ov.diff_px > 0 and db_ov.diff_px > 0 and on2[0] != null and on2[0].get_size() == Vector2i(512, 288)})
	for v in [a, b]:
		v.cam.monitoring_clean_texture_enabled = false
		v.sv.queue_free()
	await frames(2)


func avg_gpu_ms(vp: RID, n: int) -> float:
	await frames(20)
	var total := 0.0
	for i in n:
		await get_tree().process_frame
		total += RenderingServer.viewport_get_measured_render_time_gpu(vp)
	return total / n


# Memory and GPU time: nothing while disabled, one texture and one copy while enabled. GPU time in alternating rounds.
func test_cost() -> void:
	var v := make_view(Vector2i(1920, 1080))
	var sv: SubViewport = v.sv
	var cam: Camera3D = v.cam
	var vp := sv.get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(vp, true)
	await frames(20)
	var info := RenderingServer.RENDERING_INFO_TEXTURE_MEM_USED
	var mem0 := RenderingServer.get_rendering_info(info)
	cam.monitoring_clean_texture_enabled = true
	await frames(5)
	var mem1 := RenderingServer.get_rendering_info(info)
	cam.monitoring_clean_texture_enabled = false
	await frames(5)
	var mem2 := RenderingServer.get_rendering_info(info)
	var t := {"off": [], "clean": [], "zebras": [], "zebras_clean": []}
	for round in 3:
		for k in ["off", "clean", "zebras", "zebras_clean"]:
			if k.begins_with("zebras"):
				set_overlays(cam, "zebras")
			else:
				overlays_off(cam)
			cam.monitoring_clean_texture_enabled = k.ends_with("clean")
			t[k].append(snappedf(await avg_gpu_ms(vp, 200), 0.001))
	cam.monitoring_clean_texture_enabled = false
	var expected := 1920 * 1080 * 4
	result("cost", {"tex_mem_before": mem0, "tex_mem_enabled": mem1, "tex_mem_after_disable": mem2, "delta_enabled": mem1 - mem0,
		"expected_delta": expected, "gpu_ms_1080p": t,
		"pass": (mem1 - mem0 >= expected and mem1 - mem0 < expected * 1.1 and mem2 == mem0) or OS.has_feature("web")})
	sv.queue_free()
	await frames(2)


func _ready() -> void:
	if OS.has_feature("web"):
		var h = JavaScriptBridge.eval("location.hash")
		if h is String and h.length() > 1:
			mode = h.substr(1)
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--mode="):
			mode = a.substr(7)
		elif a.begins_with("--out="):
			out_dir = a.substr(6)
	if not out_dir.is_empty():
		DirAccess.make_dir_recursive_absolute(out_dir)
	result("start", {"mode": mode, "renderer": RenderingServer.get_current_rendering_method(), "driver": RenderingServer.get_current_rendering_driver_name()})
	await frames(5)
	if mode == "suite":
		for cfg in ["all", "zebras", "false_color", "histogram"]:
			await test_clean_vs_off(cfg, "")
		await test_clean_vs_off("all", "nr_sharpen")
		await test_same_frame()
		await test_two_viewports()
		await test_cost()
	result("done", {"failures": failures})
	if not OS.has_feature("web"):
		get_tree().quit(1 if failures else 0)
