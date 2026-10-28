extends Node3D
# Throwaway measurement bench for the dynamic-resolution design (task 68). Not engine code.
# Params: web = URL query (?mode=sweep&up=fsr2...), native = user args after "--" (mode=sweep up=fsr2 ...).
#   mode   sweep | hitch | ctrl | capture
#   up     bilinear | fsr2 | taa      (taa = bilinear scaling + TAA)
#   iters  per-pixel shader cost (GPU load that scales with resolution)
#   cpu_ms busy-wait per frame (CPU load that does not)
#   gpu    1: the controller reads GPU time; 0: frame time only (the WebGPU-without-timestamp fallback)
#   ae     1: auto exposure on (to see what a reconfigure does to it)

var P := {"mode": "sweep", "up": "bilinear", "iters": "64", "cpu_ms": "0", "gpu": "1", "ae": "0",
		"dur": "60", "target": "60", "load2_at": "0", "iters2": "0", "cpu2": "-1"}
var vp: Viewport
var vp_rid: RID
var mat: ShaderMaterial
var t0 := 0
var frame := 0
var pre_us := 0
var draw_ms := 0.0 # frame_pre_draw -> frame_post_draw: CPU cost of the render server's frame
var is_web := OS.has_feature("web")

func out(kind: String, d: Dictionary) -> void:
	d["kind"] = kind
	d["t"] = snappedf((Time.get_ticks_usec() - t0) / 1e6, 0.001)
	print("[dr] " + JSON.stringify(d))

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
	RenderingServer.viewport_set_measure_render_time(vp_rid, true)
	RenderingServer.frame_pre_draw.connect(func(): pre_us = Time.get_ticks_usec())
	RenderingServer.frame_post_draw.connect(func(): draw_ms = (Time.get_ticks_usec() - pre_us) / 1000.0)
	_build_scene()
	_apply_upscaler()
	out("start", {"params": P, "web": is_web, "renderer": RenderingServer.get_current_rendering_method(),
			"size": [vp.size.x, vp.size.y], "adapter": RenderingServer.get_video_adapter_name()})
	match P.mode:
		"sweep": _sweep()
		"hitch": _hitch()
		"ctrl": _ctrl_run()
		"capture": _capture()

func _apply_upscaler() -> void:
	match P.up:
		"bilinear":
			vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
		"fsr2":
			vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR2
		"taa":
			vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
			vp.use_taa = true

func _build_scene() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	env.sky = sky
	env.ssao_enabled = true
	env.ssr_enabled = true
	env.glow_enabled = true
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
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
		var sm := SphereMesh.new()
		s.mesh = sm
		s.position = Vector3((i % 6) * 3.0 - 7.5, 1.0, -(i / 6) * 3.0)
		s.material_override = mat
		add_child(s)
	for i in 32:
		var l := OmniLight3D.new()
		l.position = Vector3((i % 8) * 4.0 - 14.0, 1.5, -(i / 8) * 4.0 + 2.0)
		l.omni_range = 6.0
		l.light_color = Color.from_hsv(i / 32.0, 0.6, 1.0)
		add_child(l)

# ---------------------------------------------------------------- measurement helpers

func busy_wait() -> void:
	var ms := float(P.cpu_ms)
	if ms <= 0:
		return
	var end := Time.get_ticks_usec() + int(ms * 1000)
	while Time.get_ticks_usec() < end:
		pass

func gpu_ms() -> float:
	# The GPU time the engine would see. Native: the viewport's own timestamp measurement (lags 2-3 frames).
	# Web: the page's timestamp-query hook (the engine driver has its readback disabled, so 0 here today).
	if is_web:
		var v = JavaScriptBridge.eval("window.__dr ? window.__dr.gpu() : -1")
		return float(v) if v != null else -1.0
	return RenderingServer.viewport_get_measured_render_time_gpu(vp_rid)

func engine_gpu_ms() -> float:
	return RenderingServer.viewport_get_measured_render_time_gpu(vp_rid)

func done_ms() -> float:
	# Web only: submit -> onSubmittedWorkDone latency (no timestamp-query needed).
	if is_web:
		var v = JavaScriptBridge.eval("window.__dr ? window.__dr.done() : -1")
		return float(v) if v != null else -1.0
	return -1.0

func set_rscale(s: float) -> float:
	var a := Time.get_ticks_usec()
	vp.scaling_3d_scale = s
	return (Time.get_ticks_usec() - a) / 1000.0

func median(a: Array) -> float:
	if a.is_empty():
		return -1.0
	var b := a.duplicate()
	b.sort()
	return b[b.size() / 2]

func mean(a: Array) -> float:
	if a.is_empty():
		return -1.0
	var s := 0.0
	for x in a:
		s += x
	return s / a.size()

func pct(a: Array, p: float) -> float:
	if a.is_empty():
		return -1.0
	var b := a.duplicate()
	b.sort()
	return b[mini(int(b.size() * p), b.size() - 1)]

func next_frame() -> float:
	await get_tree().process_frame
	busy_wait()
	return get_process_delta_time() * 1000.0

func wait_s(s: float) -> void:
	var end := Time.get_ticks_usec() + int(s * 1e6)
	while Time.get_ticks_usec() < end:
		await next_frame()

func finish() -> void:
	out("done", {})
	if is_web:
		JavaScriptBridge.eval("window.__dr_done = true")
	else:
		get_tree().quit()

# ---------------------------------------------------------------- sweep: does a lower scale lower the frame?

func _sweep() -> void:
	await wait_s(4.0) # startup: shader compiles, page load
	for s in [1.0, 0.85, 0.7, 0.6, 0.5, 1.0]:
		set_rscale(s)
		await wait_s(1.0)
		var ft := []
		var gt := []
		var eg := []
		var dt := []
		var dn := []
		var end := Time.get_ticks_usec() + 3000000
		while Time.get_ticks_usec() < end:
			ft.append(await next_frame())
			gt.append(gpu_ms())
			eg.append(engine_gpu_ms())
			dt.append(draw_ms)
			dn.append(done_ms())
		out("sweep", {"scale": s, "frames": ft.size(), "frame_med": median(ft), "frame_p90": pct(ft, 0.9),
				"gpu_mean": mean(gt), "engine_gpu_mean": mean(eg), "draw_cpu_med": median(dt), "done_med": median(dn),
				"internal": [int(vp.size.x * s), int(vp.size.y * s)]})
	finish()

# ---------------------------------------------------------------- hitch: what one scale change costs

func _hitch() -> void:
	await wait_s(4.0)
	var base := []
	for i in 120:
		base.append(await next_frame())
	var bmed := median(base)
	out("hitch_base", {"frame_med": bmed, "draw_cpu": draw_ms})
	var scales := [0.75, 1.0]
	for k in 10:
		var s: float = scales[k % 2]
		var setter := set_rscale(s)
		var after := []
		var draws := []
		for i in 6:
			after.append(await next_frame())
			draws.append(draw_ms)
		out("hitch", {"to": s, "setter_ms": setter, "frames_after": after, "draw_cpu_after": draws, "base_med": bmed})
		await wait_s(1.5)
	finish()

# ---------------------------------------------------------------- capture: what the image does around a change (native)

func _capture() -> void:
	await wait_s(4.0)
	var imgs := []
	var change_at := 6
	for i in 30:
		if i == change_at:
			set_rscale(0.6)
		await RenderingServer.frame_post_draw
		var im := vp.get_texture().get_image()
		imgs.append(im)
		im.save_png("user://cap_%s_ae%s_%02d.png" % [P.up, P.ae, i])
	# Mean luminance and mean abs change against the frame before, per frame.
	var rows := []
	for i in imgs.size():
		var lum := _mean_lum(imgs[i])
		var d := 0.0 if i == 0 else _mean_diff(imgs[i], imgs[i - 1])
		var ds := _mean_diff(imgs[i], imgs[imgs.size() - 1])
		rows.append([i - change_at, snappedf(lum, 0.0001), snappedf(d, 0.0001), snappedf(ds, 0.0001)])
	out("capture", {"up": P.up, "ae": P.ae, "rows_rel_lum_dprev_dfinal": rows, "dir": ProjectSettings.globalize_path("user://")})
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
			var d := b.get_pixel(x, y)
			s += absf(c.r - d.r) + absf(c.g - d.g) + absf(c.b - d.b)
			n += 3
	return s / n

# ---------------------------------------------------------------- ctrl: the proposed controller, prototyped

# Tunables (the DESIGN.md defaults).
const MIN_SCALE := 0.5
const MAX_SCALE := 1.0
const STEP := 0.05
const WINDOW := 30 # frames per decision
const SETTLE_S := 0.5 # after a change
const SETTLE_FRAMES := 8
const STARTUP_S := 3.0
const HITCH_X := 4.0 # frames over 4x the target are hitches (compiles, loads), not load
const DOWN_AT := 1.0 # GPU path: lower when the GPU frame is over the budget
const UP_AT := 0.80 # raise when the frame at the next step would still fit with this margin
const HEADROOM := 0.90 # aim at 90 % of the budget when lowering
const CPU_GAIN_MIN := 0.25 # frame-time path: a step down must deliver 25 % of its predicted gain

var c_scale := 1.0
var c_settle_until := 0.0
var c_win := []
var c_gwin := []
var c_changes := []
var c_cpu_bound_until := 0.0
var c_cpu_backoff := 5.0
var c_probe: Dictionary = {} # frame-time path: the step being verified
var c_last_ok_scale := 1.0

func now_s() -> float:
	return (Time.get_ticks_usec() - t0) / 1e6

func c_change(s: float, why: String) -> void:
	s = clampf(snappedf(s, STEP), MIN_SCALE, MAX_SCALE)
	if is_equal_approx(s, c_scale):
		return
	out("ctrl_change", {"from": c_scale, "to": s, "why": why})
	c_changes.append(now_s())
	c_scale = s
	set_rscale(s)
	c_settle_until = now_s() + SETTLE_S
	c_win.clear()
	c_gwin.clear()

func _ctrl_run() -> void:
	var budget := 1000.0 / float(P.target)
	var use_gpu: bool = P.gpu == "1"
	var dur := float(P.dur)
	var load2 := float(P.load2_at)
	var switched := false
	var settle_frames := 0
	var log_t := 0.0
	var all_ft := []
	while now_s() < dur:
		var ft: float = await next_frame()
		if load2 > 0 and not switched and now_s() > load2:
			switched = true
			if int(P.iters2) > 0:
				mat.set_shader_parameter("iters", int(P.iters2))
			if float(P.cpu2) >= 0:
				P.cpu_ms = P.cpu2
			out("load_change", {"iters": P.iters2, "cpu_ms": P.cpu_ms})
		all_ft.append([now_s(), ft, c_scale])
		if now_s() > log_t:
			log_t = now_s() + 1.0
			out("ctrl_tick", {"scale": c_scale, "frame": ft, "gpu": gpu_ms(), "done": done_ms(), "draw_cpu": draw_ms})
		if now_s() < STARTUP_S or now_s() < c_settle_until:
			settle_frames = 0
			continue
		settle_frames += 1
		if settle_frames < SETTLE_FRAMES:
			continue
		if ft > HITCH_X * budget:
			continue # a hitch: not evidence about load
		c_win.append(ft)
		if use_gpu:
			c_gwin.append(gpu_ms())
		if c_win.size() < WINDOW:
			continue
		var fmed := median(c_win)
		var missed := 0
		for x in c_win:
			if x > budget * 1.5:
				missed += 1
		var slow := fmed > budget * 1.05 or missed > WINDOW / 4
		if use_gpu:
			var g := median(c_gwin)
			# GPU path. Pixel cost ~ scale^2: predict the GPU frame at a scale, aim under the budget.
			if g > budget * DOWN_AT and c_scale > MIN_SCALE:
				c_change(c_scale * sqrt(budget * HEADROOM / g), "gpu %.1f ms > %.1f" % [g, budget])
			elif slow and g < budget * 0.75:
				# Frame misses while the GPU has room: CPU-bound. Resolution can't help; say so, change nothing.
				if now_s() > c_cpu_bound_until:
					out("ctrl_cpu_bound", {"frame_med": fmed, "gpu": g, "scale": c_scale})
					c_cpu_bound_until = now_s() + 5.0
			elif c_scale < MAX_SCALE:
				var nxt := minf(c_scale + STEP, MAX_SCALE)
				if g * pow(nxt / c_scale, 2) < budget * UP_AT:
					c_change(nxt, "gpu %.1f ms, room for %.2f" % [g, nxt])
		else:
			# Frame-time path: no GPU time. Step down and verify it helped; raise by probing.
			if not c_probe.is_empty():
				var predicted: float = c_probe.before - c_probe.before * (1.0 - pow(c_scale / c_probe.from, 2))
				var gain_pred: float = c_probe.before - predicted
				var gain: float = c_probe.before - fmed
				if c_probe.dir < 0:
					if gain < gain_pred * CPU_GAIN_MIN:
						out("ctrl_cpu_bound", {"before": c_probe.before, "after": fmed, "predicted_gain": gain_pred, "scale": c_scale})
						var back: float = c_probe.from
						c_probe = {}
						c_change(back, "step down did not help: CPU-bound")
						c_cpu_bound_until = now_s() + c_cpu_backoff
						c_cpu_backoff = minf(c_cpu_backoff * 2.0, 60.0)
						continue
					c_cpu_backoff = 5.0
				else:
					if slow:
						var back2: float = c_probe.from
						c_probe = {}
						c_last_ok_scale = back2
						c_change(back2, "probe up missed frames")
						c_cpu_bound_until = now_s() + 10.0 # don't probe again right away
						continue
				c_probe = {}
			if slow and c_scale > MIN_SCALE and now_s() > c_cpu_bound_until:
				var from := c_scale
				c_probe = {"from": from, "before": fmed, "dir": -1}
				c_change(c_scale * sqrt(budget * HEADROOM / fmed) if fmed > budget * 1.2 else c_scale - STEP, "frame %.1f ms > %.1f" % [fmed, budget])
				if is_equal_approx(from, c_scale):
					c_probe = {}
			elif not slow and fmed < budget * 0.8 and c_scale < MAX_SCALE and now_s() > c_cpu_bound_until:
				# Uncapped or high-refresh: real headroom shows; probe one step up.
				c_probe = {"from": c_scale, "before": fmed, "dir": 1}
				c_change(c_scale + STEP, "frame %.1f ms, probing up" % fmed)
		c_win.clear()
		c_gwin.clear()
	# Summary: changes per minute after the first 15 s, frame stats per load phase.
	var late := c_changes.filter(func(t): return t > 15.0 and (load2 <= 0 or t < load2 or t > load2 + 15.0))
	var span := dur - 15.0 - (15.0 if load2 > 0 else 0.0)
	var ph := {}
	for r in all_ft:
		if r[0] < STARTUP_S:
			continue
		var k := "after" if load2 > 0 and r[0] > load2 else "before"
		if not ph.has(k):
			ph[k] = {"ft": [], "scale": []}
		ph[k].ft.append(r[1])
		ph[k].scale.append(r[2])
	var sm := {}
	for k in ph:
		var miss := 0
		for x in ph[k].ft:
			if x > budget * 1.5:
				miss += 1
		sm[k] = {"frame_med": median(ph[k].ft), "missed_frac": float(miss) / ph[k].ft.size(), "final_scale": ph[k].scale[-1]}
	out("ctrl_summary", {"changes": c_changes.size(), "changes_per_min_settled": late.size() * 60.0 / maxf(span, 1.0), "phases": sm})
	finish()
