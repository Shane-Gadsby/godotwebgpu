extends Node3D
# Task 75 bench: the engine's dynamic 3D resolution controller (Viewport.scaling_3d_dynamic) on task 68's scene.
# Params: web = URL query, native = user args after "--".
#   mode    ctrl | cap | off
#   up      bilinear | fsr2 | taa       ae 1: auto exposure
#   iters   per-pixel shader cost (GPU load that scales with resolution); cpu_ms busy-wait per frame (CPU load)
#   dyn     1: controller on;  target / min / max: its settings;  remember 1: scaling_3d_dynamic_remember
#   dur     seconds;  load2_at: switch to iters2 / cpu2 / target2 at that time
#   s0      scaling_3d_scale (the starting scale)
# ctrl: one [dr] line per second (scale, state, source, frame, GPU) and one per change; a summary at the end.
# cap (native, --fixed-fps 60): HDR float frames around a controller-driven change (the target fps is raised at
#   frame `change_at` so the controller lowers the scale; nothing else in the picture changes); mean luminance and
#   frame-to-frame difference per frame, relative to the first frame the new scale is drawn.
# off: no controller, fixed scale, md5 of float frames (the bit-identical check against the pre-change editor).

var P := {"mode": "ctrl", "up": "bilinear", "iters": "64", "cpu_ms": "0", "ae": "0", "dyn": "1", "target": "60",
		"min": "0.5", "max": "1.0", "remember": "0", "dur": "60", "load2_at": "0", "iters2": "0", "cpu2": "-1",
		"target2": "0", "s0": "1.0", "frames": "40", "warm": "200", "change_at": "10", "hdr": "1"}
var vp: Viewport
var vp_rid: RID
var mat: ShaderMaterial
var t0 := 0
var is_web := OS.has_feature("web")
const STATES := ["OFF", "STARTING", "ADJUSTING", "STABLE", "CPU_LIMITED", "AT_MIN_SCALE"]
const SOURCES := ["NONE", "GPU_TIME", "FRAME_TIME"]
signal image_read(image: Image)

func out(kind: String, d: Dictionary) -> void:
	d["kind"] = kind
	d["t"] = snappedf((Time.get_ticks_usec() - t0) / 1e6, 0.001)
	print("[dr] " + JSON.stringify(d))

func eff() -> float:
	return vp.call("get_scaling_3d_effective_scale") if vp.has_method("get_scaling_3d_effective_scale") else vp.scaling_3d_scale

func dstate() -> int:
	return vp.call("get_scaling_3d_dynamic_state") if vp.has_method("get_scaling_3d_dynamic_state") else 0

func dsource() -> int:
	return vp.call("get_scaling_3d_dynamic_source") if vp.has_method("get_scaling_3d_dynamic_source") else 0

func now_s() -> float:
	return (Time.get_ticks_usec() - t0) / 1e6

func _ready() -> void:
	t0 = Time.get_ticks_usec()
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
	vp_rid = vp.get_viewport_rid()
	if P.mode != "ctrl":
		vp.use_hdr_2d = P.hdr == "1"
	_build_scene()
	match P.up:
		"bilinear":
			vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
		"fsr2":
			vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR2
		"fsr":
			vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR
		"taa":
			vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
			vp.use_taa = true
	vp.scaling_3d_scale = float(P.s0)
	if P.dyn == "1":
		# By name: the "off" check runs this script in the pre-change editor, which has none of this.
		vp.set("scaling_3d_dynamic_target_fps", float(P.target))
		vp.set("scaling_3d_dynamic_min_scale", float(P.min))
		vp.set("scaling_3d_dynamic_max_scale", float(P.max))
		vp.set("scaling_3d_dynamic_remember", P.remember == "1")
		vp.set("scaling_3d_dynamic", true)
		vp.connect("scaling_3d_effective_scale_changed", _on_scale_changed)
	out("start", {"params": P, "web": is_web, "renderer": RenderingServer.get_current_rendering_method(),
			"size": [vp.size.x, vp.size.y], "adapter": RenderingServer.get_video_adapter_name(),
			"start_scale": eff(), "user_scale": vp.scaling_3d_scale})
	match P.mode:
		"ctrl": _ctrl()
		"cap": _cap()
		"off": _off()

var changes := []
func _on_scale_changed(s: float) -> void:
	changes.append([snappedf(now_s(), 0.01), s])
	out("change", {"scale": s, "state": STATES[dstate()]})

func _build_scene() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	env.sky = sky
	env.ssao_enabled = true
	env.ssr_enabled = true
	env.glow_enabled = true
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC if P.mode == "ctrl" else Environment.TONE_MAPPER_LINEAR
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
	mat = ShaderMaterial.new()
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

func busy_wait() -> void:
	var ms := float(P.cpu_ms)
	if ms <= 0:
		return
	var end := Time.get_ticks_usec() + int(ms * 1000)
	while Time.get_ticks_usec() < end:
		pass

func median(a: Array) -> float:
	if a.is_empty():
		return -1.0
	var b := a.duplicate()
	b.sort()
	return b[b.size() / 2]

func pct(a: Array, p: float) -> float:
	if a.is_empty():
		return -1.0
	var b := a.duplicate()
	b.sort()
	return b[mini(int(b.size() * p), b.size() - 1)]

func finish() -> void:
	out("done", {})
	if is_web:
		JavaScriptBridge.eval("window.__dr_done = true")
	else:
		get_tree().quit()

# ---------------------------------------------------------------- ctrl

func _ctrl() -> void:
	RenderingServer.viewport_set_measure_render_time(vp_rid, true) # Only for the log; the controller measures anyway.
	var dur := float(P.dur)
	var load2 := float(P.load2_at)
	var switched := false
	var tick := 1.0
	var ft := []
	var states := {}
	var last_t := now_s()
	while now_s() < dur:
		await get_tree().process_frame
		busy_wait()
		var t := now_s()
		ft.append((t - last_t) * 1000.0)
		last_t = t
		var st: String = STATES[dstate()]
		states[st] = states.get(st, 0) + 1
		if load2 > 0 and not switched and t > load2:
			switched = true
			if int(P.iters2) > 0:
				mat.set_shader_parameter("iters", int(P.iters2))
			if float(P.cpu2) >= 0:
				P.cpu_ms = P.cpu2
			if float(P.target2) > 0:
				vp.set("scaling_3d_dynamic_target_fps", float(P.target2))
			out("load_change", {"iters": P.iters2, "cpu_ms": P.cpu_ms, "target": vp.get("scaling_3d_dynamic_target_fps")})
		if t > tick:
			tick += 1.0
			out("tick", {"scale": eff(), "state": st,
					"source": SOURCES[dsource()], "frame_med": snappedf(median(ft), 0.01),
					"frame_p90": snappedf(pct(ft, 0.9), 0.01),
					"gpu": snappedf(RenderingServer.viewport_get_measured_render_time_gpu(vp_rid), 0.01), "frames": ft.size()})
			ft.clear()
	out("summary", {"changes": changes, "states_frames": states, "final_scale": eff(),
			"final_state": STATES[dstate()], "source": SOURCES[dsource()]})
	finish()

# ---------------------------------------------------------------- cap: a controller-driven change, as task 74 measured it

func _cap() -> void:
	# Warm up with an easy target (the controller holds max scale), then raise the target so it lowers the scale.
	for i in int(P.warm):
		await get_tree().process_frame
	out("warm_done", {"scale": eff(), "state": STATES[dstate()]})
	vp.set("scaling_3d_dynamic_target_fps", float(P.target2))
	# Capture a rolling window of frames until the change has been drawn for `frames` frames.
	var ring := []
	var change_idx := -1
	var idx := 0
	var deadline := now_s() + 60.0
	var scale_before := eff()
	while now_s() < deadline:
		var s_now := eff()
		var holder := [null]
		vp.get_texture().get_image_async(func(im: Image): holder[0] = im)
		await get_tree().process_frame
		ring.append([idx, s_now, holder])
		if change_idx < 0 and s_now != scale_before:
			change_idx = idx
			out("controller_change", {"from": scale_before, "to": s_now, "frame": idx})
		idx += 1
		if change_idx >= 0 and idx - change_idx >= int(P.frames):
			break
		if change_idx < 0 and ring.size() > int(P.change_at):
			ring.pop_front()
	for i in 10:
		await get_tree().process_frame
	if change_idx < 0:
		out("error", {"msg": "the controller did not change the scale"})
		finish()
		return
	var rows := []
	var prev: Image = null
	for e in ring:
		var im: Image = e[2][0]
		if im == null:
			out("error", {"msg": "null image", "frame": e[0]})
			continue
		var d := 0.0 if prev == null else _mean_diff(im, prev)
		rows.append([e[0] - change_idx, e[1], snappedf(_mean_lum(im), 0.00001), snappedf(d, 0.00001)])
		prev = im
	out("capture", {"up": P.up, "ae": P.ae, "rows_rel_scale_lum_dprev": rows})
	finish()

# ---------------------------------------------------------------- off: fixed scale, hashes (no controller)

func _off() -> void:
	for i in int(P.warm):
		await get_tree().process_frame
	var n := int(P.frames)
	var imgs := []
	imgs.resize(n)
	var got := [0]
	for i in n:
		var k := i
		vp.get_texture().get_image_async(func(im: Image):
			imgs[k] = im
			got[0] += 1)
		await get_tree().process_frame
	while got[0] < n:
		await get_tree().process_frame
	var hashes := []
	for im in imgs:
		var ctx := HashingContext.new()
		ctx.start(HashingContext.HASH_MD5)
		ctx.update(im.get_data())
		hashes.append(ctx.finish().hex_encode().substr(0, 12))
	out("hashes", {"up": P.up, "ae": P.ae, "s0": P.s0, "md5": hashes})
	finish()

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
