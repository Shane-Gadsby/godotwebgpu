extends Node3D
# Task 78 capture bench (task 74's, plus ssil / ssr switches) (from task 68's proto/main.gd, mode=capture): what the picture does around a scaling_3d_scale
# change. The root viewport is HDR 2D with the Linear tonemapper, read back as float with get_image_async() (which
# also works on the web), so luminance is measured unclipped and unquantized (TASKS.md §3 trap 80).
# Params: native = user args after "--", web = URL query.
#   up      bilinear | fsr2 | taa       ae  1: auto exposure on
#   ssil    1: SSIL on                  ssr 0: SSR off (on by default, as in task 74)
#   s0, s1  scale before / after the change (s1 == s0: no change, for the bit-identical check)
#   frames  frames captured; change_at: the frame the scale changes on
#   save    1: save every frame as EXR in user:// (native)

var P := {"up": "bilinear", "ae": "0", "s0": "1.0", "s1": "0.6", "frames": "36", "change_at": "8", "iters": "64",
		"save": "0", "tag": "x", "warm": "90", "ssil": "0", "ssr": "1"}
var vp: Viewport
var is_web := OS.has_feature("web")
signal image_read(image: Image)

func out(kind: String, d: Dictionary) -> void:
	d["kind"] = kind
	print("[cap] " + JSON.stringify(d))

func _ready() -> void:
	if is_web:
		var q = JavaScriptBridge.eval("location.search")
		for kv in str(q).trim_prefix("?").split("&", false):
			var a = kv.split("=")
			if a.size() == 2:
				P[a[0]] = a[1]
	else:
		for kv in OS.get_cmdline_user_args():
			var a = kv.split("=")
			if a.size() == 2:
				P[a[0]] = a[1]
	vp = get_viewport()
	vp.use_hdr_2d = true
	_build_scene()
	match P.up:
		"bilinear":
			vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
		"fsr2":
			vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR2
		"taa":
			vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
			vp.use_taa = true
	vp.scaling_3d_scale = float(P.s0)
	out("start", {"params": P, "web": is_web, "renderer": RenderingServer.get_current_rendering_method(),
			"size": [vp.size.x, vp.size.y], "adapter": RenderingServer.get_video_adapter_name()})
	_capture()

func _build_scene() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	env.sky = sky
	env.ssao_enabled = true
	env.ssr_enabled = P.ssr == "1"
	env.ssil_enabled = P.ssil == "1"
	env.glow_enabled = true
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	var cam := Camera3D.new()
	cam.position = Vector3(0, 4, 12)
	cam.rotation_degrees = Vector3(-15, 0, 0)
	if P.ae == "1":
		var ca := CameraAttributesPractical.new()
		ca.auto_exposure_enabled = true
		ca.auto_exposure_speed = 0.5
		cam.attributes = ca
	add_child(cam)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	sun.shadow_enabled = true
	add_child(sun)
	var mat := ShaderMaterial.new()
	mat.shader = load("res://heavy.gdshader")
	mat.set_shader_parameter("iters", int(P.iters))
	var ground := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(80, 80)
	ground.mesh = pm
	ground.material_override = mat
	add_child(ground)
	for i in 24:
		var s := MeshInstance3D.new()
		s.mesh = SphereMesh.new()
		s.position = Vector3((i % 6) * 3.0 - 7.5, 1.0, -(i / 6) * 3.0)
		s.material_override = mat
		add_child(s)
	for i in 32:
		var l := OmniLight3D.new()
		l.position = Vector3((i % 8) * 4.0 - 14.0, 1.5, -(i / 8) * 4.0 + 2.0)
		l.omni_range = 6.0
		l.light_color = Color.from_hsv(i / 32.0, 0.6, 1.0)
		add_child(l)

# One readback in flight at a time (the web's async readback cannot take one per frame): each sample is tagged with
# the frame it shows, relative to the change, and compared with the last, settled sample (d_final) instead of the
# frame before.
func _capture_seq() -> void:
	for i in int(P.warm):
		await get_tree().process_frame
	var n := int(P.frames)
	var samples := []
	var change_frame := -1
	for i in n:
		if i == int(P.change_at):
			vp.scaling_3d_scale = float(P.s1)
			change_frame = Engine.get_frames_drawn()
		var f := Engine.get_frames_drawn()
		if P.get("pair", "0") == "1":
			# Two consecutive frames per sample, for a frame-to-frame difference (two readbacks in flight).
			var pair := [null, null, 0]
			vp.get_texture().get_image_async(func(im: Image):
				pair[0] = im
				pair[2] += 1)
			await get_tree().process_frame
			vp.get_texture().get_image_async(func(im: Image):
				pair[1] = im
				pair[2] += 1)
			while pair[2] < 2:
				await get_tree().process_frame
			samples.append([f, pair[1], pair[0]])
			continue
		vp.get_texture().get_image_async(image_read.emit)
		var im: Image = await image_read
		samples.append([f, im])
	var last: Image = samples[n - 1][1]
	var rows := []
	for smp in samples:
		var im: Image = smp[1]
		if im == null:
			out("error", {"msg": "null image"})
			continue
		var row := [smp[0] - change_frame, snappedf(_mean_lum(im), 0.00001), snappedf(_mean_diff(im, last), 0.00001)]
		if smp.size() > 2 and smp[2] != null:
			row.append(snappedf(_mean_diff(im, smp[2]), 0.000001)) # frame-to-frame
		rows.append(row)
	out("capture", {"tag": P.tag, "up": P.up, "ae": P.ae, "s0": P.s0, "s1": P.s1, "seq": true,
			"size": [last.get_width(), last.get_height()], "format": last.get_format(), "rows_rel_lum_dfinal": rows})
	out("done", {})
	if is_web:
		JavaScriptBridge.eval("window.__cap_done = true")
	else:
		get_tree().quit()

func _capture() -> void:
	if P.get("seq", "0") == "1":
		await _capture_seq()
		return
	# Settle: shader compiles, auto exposure and the temporal upscalers converge.
	for i in int(P.warm):
		await get_tree().process_frame
	var n := int(P.frames)
	var change_at := int(P.change_at)
	var imgs := []
	imgs.resize(n)
	var got := [0]
	for i in n:
		if i == change_at:
			vp.scaling_3d_scale = float(P.s1)
		var idx := i
		vp.get_texture().get_image_async(func(im: Image):
			imgs[idx] = im
			got[0] += 1)
		await get_tree().process_frame
	while got[0] < n:
		await get_tree().process_frame
	var rows := []
	var hashes := []
	for i in n:
		var im: Image = imgs[i]
		if im == null:
			out("error", {"msg": "null image", "frame": i})
			continue
		if not im.get_format() in [Image.FORMAT_RGBAH, Image.FORMAT_RGBH, Image.FORMAT_RGBAF, Image.FORMAT_RGBF]:
			out("error", {"msg": "not a float image", "format": im.get_format()})
		var lum := _mean_lum(im)
		var d := 0.0 if i == 0 or imgs[i - 1] == null else _mean_diff(im, imgs[i - 1])
		rows.append([i - change_at, snappedf(lum, 0.00001), snappedf(d, 0.00001)])
		var ctx := HashingContext.new()
		ctx.start(HashingContext.HASH_MD5)
		ctx.update(im.get_data())
		hashes.append(ctx.finish().hex_encode().substr(0, 12))
		if P.save == "1" and not is_web:
			im.save_exr("user://cap_%s_%s_%02d.exr" % [P.tag, P.up, i])
	out("capture", {"tag": P.tag, "up": P.up, "ae": P.ae, "ssil": P.ssil, "ssr": P.ssr, "s0": P.s0, "s1": P.s1, "size": [imgs[0].get_width(), imgs[0].get_height()],
			"format": imgs[0].get_format(), "rows_rel_lum_dprev": rows, "md5": hashes})
	out("done", {})
	if is_web:
		JavaScriptBridge.eval("window.__cap_done = true")
	else:
		get_tree().quit()

func _mean_lum(im: Image) -> float:
	var s := 0.0
	var n := 0
	for y in range(0, im.get_height(), 8):
		for x in range(0, im.get_width(), 8):
			var c := im.get_pixel(x, y)
			s += c.r * 0.2126 + c.g * 0.7152 + c.b * 0.0722
			n += 1
	return s / n

func _mean_diff(a: Image, b: Image) -> float:
	var s := 0.0
	var n := 0
	for y in range(0, a.get_height(), 4):
		for x in range(0, a.get_width(), 4):
			var c := a.get_pixel(x, y)
			var e := b.get_pixel(x, y)
			s += absf(c.r - e.r) + absf(c.g - e.g) + absf(c.b - e.b)
			n += 3
	return s / n
