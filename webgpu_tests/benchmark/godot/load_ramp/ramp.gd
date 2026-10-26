extends Node3D
## Load-ramp benchmark: grows one kind of content until the frame rate falls to a target (default 20 fps) and reports
## how much content that took. Comparing the amounts across platforms gives a fair "n x the load" figure, which a
## plain fps reading cannot (browsers hold WebGL at the display rate). Driven by run_ramp.sh; see README_RAMP in that
## script's header for the legs.
##
## One unit of N, per ramp:
##   draws     one MeshInstance3D (BoxMesh, 12 triangles, a few pixels on screen) with its OWN StandardMaterial3D, so no
##             two draws can be merged. Static. Measures per-draw CPU submission cost.
##   nodes     the draws unit plus one GDScript transform update per frame (rotate_y on the node). Measures script +
##             transform sync + draw submission, i.e. a "many moving objects" game frame.
##   fill      one full-screen (1280x720) alpha-blended quad running a fixed 64-iteration trig loop per pixel.
##             Measures fragment ALU + blending; a handful of draws.
##   lit       one PBR sphere (96x48 segments, ~9.2k triangles), lit by a fixed rig of 1 directional + 3 omni + 3 spot
##             lights, all shadow-casting, so each sphere is drawn in the main pass and every shadow map. Mixed
##             vertex / shadow / shading cost.
##   particles one GPUParticles3D emitter of 2048 small opaque billboard particles (0.5 s lifetime, simulated every
##             frame: fixed_fps = 0). Measures particle simulation + vertex cost.
##
## Settings: cmdline user args (`-- --ramps=draws,fill --target-fps=20 --max-s=180`), env (RAMP_RAMPS, RAMP_TARGET_FPS,
## RAMP_MAX_S), or on the web the page's query string (`?ramps=draws&target_fps=20&max_s=180`). Everything is placed
## with a fixed seed and the camera is fixed, so every leg draws the same frames.
##
## Output (one line per ramp, parsed by summarize_ramp.py, and run_benchmark.sh's field() via ramp_<name>_n=):
##   [BENCHMARK] ramp=<name> target_fps=20 n=<N> status=converged|cap_n|cap_time|mem_cap|below_min fps_at_n=.. frame_ms_p50=..
##               cpu_ms=.. ...
## The frame time of a window is its mean (elapsed / frames), i.e. throughput; frame_ms_p50 is printed alongside.
## n is the crossing interpolated in log space between the last N above the target (n_good) and the first N below it
## (n_bad); fps_at_n, frame_ms_p50, cpu_ms, draw_calls are the n_good measurement's.
##
## Growth: N doubles while far from the target, and near it grows by what the last window predicts (x1.25 margin, at
## least x1.15), so no step lands far past the target: a web leg asked for 6x its capacity queued GPU work for
## seconds, and the backlog poisoned the next measurements. Memory: the web heap is 2 GB (Emscripten's default for a
## nothreads build, the official template's too), so N never grows past what the measured memory (the wasm heap
## size on the web, see _mem_bytes) predicts will fit in mem_mb (default 1400 MB); a ramp stopped there reports status=mem_cap, n =
## the last N above the target: "carried this much before memory ran out", not a frame-rate crossing.

const RAMPS_ALL := ["draws", "nodes", "fill", "lit", "particles"]
const N_START := {"draws": 256, "nodes": 256, "fill": 4, "lit": 16, "particles": 4}
const N_CAP := {"draws": 131072, "nodes": 131072, "fill": 8192, "lit": 65536, "particles": 8192}

const SETTLE_S := 1.0          # discarded after every change of N
const WINDOW_S := 2.0          # measured window: mean frame time (elapsed / frames) over it...
const MIN_FRAMES := 8          # ...with at least this many frames...
const MAX_WINDOW_S := 8.0      # ...unless that takes longer than this
const GROW := 2.0              # largest growth factor per step (used while far below the target frame time)
const GROW_MIN := 1.15         # smallest growth factor per step near the target
const MEM_MB_DEFAULT := 1400.0 # static-memory budget, all legs (web heap max is 2048 MB)
const BISECT_TOL := 0.04       # stop bisecting when n_bad / n_good <= 1 + this
const MAX_BISECT := 10
const STARTUP_WARMUP_S := 3.0  # once, before the first ramp (pipeline/shader warm-up on the web)
const CONFIRM_SETTLE_S := 2.0  # extra settle before the window that confirms a below-target result

const FILL_SHADER := """
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, depth_test_disabled, cull_disabled, fog_disabled;
uniform float seed = 0.0;
void vertex() {
	POSITION = vec4(VERTEX.xy, 0.5, 1.0);
}
void fragment() {
	vec2 p = SCREEN_UV * 6.0 + vec2(seed, seed * 0.37);
	vec3 c = vec3(0.0);
	for (int i = 0; i < 64; i++) {
		p = vec2(sin(p.y * 1.31 + seed), cos(p.x * 1.73 - float(i) * 0.11)) * 1.6 + p * 0.3;
		c += abs(vec3(p.x, p.y, p.x * p.y)) * 0.012;
	}
	ALBEDO = fract(c);
	ALPHA = 0.04;
}
"""

var _target_fps := 20.0
var _max_s := 180.0
var _mem_budget := MEM_MB_DEFAULT * 1048576.0
var _ramps: Array = []
var _rng := RandomNumberGenerator.new()
var _root: Node3D
var _pool: Array = []
var _n := 0
var _ramp := ""
var _label: Label
var _label_t := 0.0
var _status_text := ""
var _vp_rid: RID
var _measure_cpu := false
var _shared := {}


func _ready() -> void:
	_parse_settings()
	_vp_rid = get_viewport().get_viewport_rid()
	# Render-time measurement is left off on the web, as in benchmark_profiler.gd (timestamp readback trouble there).
	_measure_cpu = not OS.has_feature("web")
	if _measure_cpu:
		RenderingServer.viewport_set_measure_render_time(_vp_rid, true)
	var canvas := CanvasLayer.new()
	add_child(canvas)
	_label = Label.new()
	_label.position = Vector2(8, 8)
	_label.add_theme_font_size_override("font_size", 18)
	_label.add_theme_color_override("font_color", Color.YELLOW)
	canvas.add_child(_label)
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.12, 0.14, 0.18)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.35, 0.35, 0.4)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	_run_all()


func _parse_settings() -> void:
	var opts := {}
	var e := OS.get_environment("RAMP_RAMPS")
	if e != "":
		opts["ramps"] = e
	e = OS.get_environment("RAMP_TARGET_FPS")
	if e != "":
		opts["target_fps"] = e
	e = OS.get_environment("RAMP_MAX_S")
	if e != "":
		opts["max_s"] = e
	for a in OS.get_cmdline_user_args():
		var s: String = a.trim_prefix("--")
		var eq := s.find("=")
		if eq > 0:
			opts[s.substr(0, eq).replace("-", "_")] = s.substr(eq + 1)
	if OS.has_feature("web") and Engine.has_singleton("JavaScriptBridge"):
		var js = Engine.get_singleton("JavaScriptBridge")
		var q = js.eval("window.location.search", true)
		if typeof(q) == TYPE_STRING:
			for kv in (q as String).trim_prefix("?").split("&", false):
				var p: PackedStringArray = kv.split("=")
				if p.size() == 2:
					opts[p[0]] = p[1].uri_decode()
	if opts.has("target_fps"):
		_target_fps = maxf(1.0, float(opts["target_fps"]))
	if opts.has("mem_mb"):
		_mem_budget = maxf(64.0, float(opts["mem_mb"])) * 1048576.0
	if opts.has("max_s"):
		_max_s = maxf(10.0, float(opts["max_s"]))
	if opts.has("ramps") and String(opts["ramps"]) != "" and String(opts["ramps"]) != "all":
		for r in String(opts["ramps"]).split(",", false):
			if RAMPS_ALL.has(r):
				_ramps.append(r)
			else:
				push_warning("load_ramp: unknown ramp '%s' ignored" % r)
	if _ramps.is_empty():
		_ramps = RAMPS_ALL.duplicate()


func _process(delta: float) -> void:
	if _ramp == "nodes":
		for i in _n:
			(_pool[i] as Node3D).rotate_y(0.02)
	_label_t += delta
	if _label_t > 0.5 and _label:
		_label_t = 0.0
		_label.text = "load ramp  %s  N=%d  %.0f fps  %s" % [_ramp, _n, 1.0 / maxf(delta, 0.0001), _status_text]


func _now() -> float:
	return Time.get_ticks_usec() / 1000000.0


func _run_all() -> void:
	var vs := get_viewport().get_visible_rect().size
	var info := Engine.get_version_info()
	print("[BENCHMARK] ramp_env engine=%s method=%s driver=%s os=%s web=%s viewport=%dx%d target_fps=%s ramps=%s max_s=%.0f adapter=%s" % [
		String(info.get("string", "?")).replace(" ", "_"), RenderingServer.get_current_rendering_method(), RenderingServer.get_current_rendering_driver_name(),
		OS.get_name(), str(OS.has_feature("web")), int(vs.x), int(vs.y), str(_target_fps), ",".join(PackedStringArray(_ramps)), _max_s,
		(RenderingServer.get_video_adapter_vendor() + " " + RenderingServer.get_video_adapter_name()).strip_edges().replace(" ", "_")])
	var t0 := _now()
	while _now() - t0 < STARTUP_WARMUP_S:
		await get_tree().process_frame
	for r in _ramps:
		await _run_ramp(r)
	print("[BENCHMARK] RAMP_ALL_DONE")
	print("[BENCHMARK] ================================================")
	if not OS.has_feature("web"):
		get_tree().quit()


func _run_ramp(ramp: String) -> void:
	var target_ms := 1000.0 / _target_fps
	var cap: int = N_CAP[ramp]
	_setup(ramp)
	var t_start := _now()
	var steps := 0
	var good := {}
	var bad := {}
	var status := "converged"
	var n: int = N_START[ramp]
	var mem0 := _mem_bytes()
	# Grow until the target is crossed (or shrink if even the start point is below it).
	while true:
		if _now() - t_start > _max_s:
			status = "cap_time"
			break
		var m := await _measure_confirmed(ramp, n, "grow", steps, target_ms)
		steps += 1
		if m["ms"] <= target_ms:
			good = m
			if not bad.is_empty():
				break
			if n >= cap:
				status = "cap_n"
				break
			var f := clampf(1.25 * target_ms / maxf(float(m["ms"]), 0.001), GROW_MIN, GROW)
			var nn := mini(cap, maxi(n + 1, int(ceil(n * f))))
			# Memory: extrapolate the static memory per unit measured so far; never grow past the budget.
			var mem := float(m["mem"])
			var per_unit := maxf((mem - mem0) / float(n), 1.0)
			if mem + per_unit * float(nn - n) > _mem_budget:
				nn = n + int((_mem_budget - mem) / per_unit)
				if nn <= int(n * 1.02):
					status = "mem_cap"
					break
			n = nn
		else:
			bad = m
			if not good.is_empty():
				break
			if n <= 1:
				status = "below_min"
				break
			n = maxi(1, int(n / GROW))
	# Bisect (geometric midpoint) between the last good and the first bad N.
	if status == "converged":
		var b := 0
		while b < MAX_BISECT and float(bad["n"]) / float(good["n"]) > 1.0 + BISECT_TOL:
			if _now() - t_start > _max_s:
				status = "cap_time"
				break
			var mid := int(round(sqrt(float(good["n"]) * float(bad["n"]))))
			if mid <= int(good["n"]) or mid >= int(bad["n"]):
				break
			var m := await _measure_confirmed(ramp, mid, "bisect", steps, target_ms)
			steps += 1
			b += 1
			if m["ms"] <= target_ms:
				good = m
			else:
				bad = m
	var n_est := 0.0
	if not good.is_empty() and not bad.is_empty():
		var lg := log(float(good["n"]))
		var lb := log(float(bad["n"]))
		var mg := log(float(good["ms"]))
		var mb := log(float(bad["ms"]))
		var f := 0.0
		if mb - mg > 0.000001:
			f = clampf((log(target_ms) - mg) / (mb - mg), 0.0, 1.0)
		n_est = exp(lg + f * (lb - lg))
	elif not good.is_empty():
		n_est = float(good["n"])
	var ref: Dictionary = good if not good.is_empty() else bad
	print("[BENCHMARK] ramp=%s target_fps=%s n=%.1f status=%s n_good=%d n_bad=%d ms_good=%.3f ms_bad=%.3f fps_at_n=%.2f frame_ms=%.3f frame_ms_p50=%.3f cpu_ms=%.3f proc_ms=%.3f draw_calls=%d primitives=%d mem_mb=%.0f mem_budget_mb=%.0f steps=%d elapsed_s=%.1f" % [
		ramp, str(_target_fps), n_est, status, int(good.get("n", 0)), int(bad.get("n", 0)), float(good.get("ms", 0.0)), float(bad.get("ms", 0.0)),
		1000.0 / float(ref["ms"]), float(ref["ms"]), float(ref["p50"]), float(ref["cpu_ms"]), float(ref["proc_ms"]), int(ref["draw_calls"]), int(ref["primitives"]),
		float(ref["mem"]) / 1048576.0, _mem_budget / 1048576.0, steps, _now() - t_start])
	print("[BENCHMARK] ramp_%s_n=%.1f" % [ramp, n_est])
	await _teardown()


## Measure at n; a result below the target is only accepted if a second window agrees (sustained-below check), so a
## single slow window (hitch, background task) cannot end the ramp.
func _measure_confirmed(ramp: String, n: int, phase: String, step: int, target_ms: float) -> Dictionary:
	_apply(ramp, n)
	var m := await _measure(true)
	_log_step(ramp, phase, step, m, "")
	# Native legs: RAMP_SHOTS_DIR=<dir> saves <dir>/<ramp>.png at the third step (the web runner screenshots the page).
	var shots := OS.get_environment("RAMP_SHOTS_DIR")
	if shots != "" and step == 2 and phase == "grow" and not OS.has_feature("web"):
		DirAccess.make_dir_recursive_absolute(shots)
		get_viewport().get_texture().get_image().save_png(shots.path_join(ramp + ".png"))
	if m["ms"] > target_ms and m["ms"] < 3.0 * target_ms:
		var m2 := await _measure(true, CONFIRM_SETTLE_S)
		_log_step(ramp, phase, step, m2, "confirm")
		if m2["ms"] <= target_ms:
			return m2
		if m2["ms"] < m["ms"]:
			return m2
	return m


func _log_step(ramp: String, phase: String, step: int, m: Dictionary, tag: String) -> void:
	print("[RAMP] ramp=%s step=%d phase=%s%s n=%d ms=%.3f p50=%.3f fps=%.2f frames=%d cpu_ms=%.3f proc_ms=%.3f draw_calls=%d mem_mb=%.0f" % [
		ramp, step, phase, ("_" + tag) if tag != "" else "", int(m["n"]), float(m["ms"]), float(m["p50"]), 1000.0 / float(m["ms"]), int(m["frames"]),
		float(m["cpu_ms"]), float(m["proc_ms"]), int(m["draw_calls"]), float(m["mem"]) / 1048576.0])
	_status_text = "%s %d: %.1f ms" % [phase, int(m["n"]), float(m["ms"])]


## Memory the budget is checked against: on the web the wasm heap's size (what runs into the 2 GB limit), which
## ramp_browser.mjs exposes as window.__rampHeapBytes(); natively, and if the page does not provide it, Godot's tracked
## static memory (0 in release templates, hence the heap on the web).
func _mem_bytes() -> float:
	if OS.has_feature("web") and Engine.has_singleton("JavaScriptBridge"):
		var v = Engine.get_singleton("JavaScriptBridge").eval("window.__rampHeapBytes ? window.__rampHeapBytes() : 0", true)
		if (typeof(v) == TYPE_FLOAT or typeof(v) == TYPE_INT) and float(v) > 0.0:
			return float(v)
	return float(OS.get_static_memory_usage())


func _median(a: PackedFloat64Array) -> float:
	if a.is_empty():
		return 0.0
	var s := a.duplicate()
	s.sort()
	return s[s.size() / 2]


func _measure(settle: bool, settle_s: float = SETTLE_S) -> Dictionary:
	var tree := get_tree()
	# The frame that applied the change carries the node creation stall: skip it, then settle.
	await tree.process_frame
	if settle:
		var ts := _now()
		var k := 0
		while _now() - ts < settle_s or k < 3:
			await tree.process_frame
			k += 1
	var frames := PackedFloat64Array()
	var cpu := PackedFloat64Array()
	var proc := PackedFloat64Array()
	await tree.process_frame
	var last := Time.get_ticks_usec()
	var t0 := last
	var dc := 0.0
	var prims := 0.0
	while true:
		await tree.process_frame
		var t := Time.get_ticks_usec()
		frames.push_back((t - last) / 1000.0)
		last = t
		proc.push_back(Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0)
		if _measure_cpu:
			cpu.push_back(RenderingServer.viewport_get_measured_render_time_cpu(_vp_rid))
		dc = Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME)
		prims = Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME)
		var el := (t - t0) / 1000000.0
		if (el >= WINDOW_S and frames.size() >= MIN_FRAMES) or el >= MAX_WINDOW_S:
			break
	# Throughput, not the median interval: on the web the browser can run several frames ahead of the GPU and then
	# block for a long one, so the median interval reads far faster than the frames actually complete.
	var mean_ms := (last - t0) / 1000.0 / maxf(1.0, frames.size())
	return {"n": _n, "ms": maxf(mean_ms, 0.001), "p50": _median(frames), "frames": frames.size(), "cpu_ms": _median(cpu) if _measure_cpu else -1.0,
			"proc_ms": _median(proc), "draw_calls": dc, "primitives": prims, "mem": _mem_bytes()}


# --- Content ---------------------------------------------------------------------------------------------------------

func _setup(ramp: String) -> void:
	_ramp = ramp
	_rng.seed = 12345
	_pool = []
	_n = 0
	_shared = {}
	_root = Node3D.new()
	add_child(_root)
	var cam := Camera3D.new()
	cam.fov = 70.0
	cam.far = 200.0
	_root.add_child(cam)
	# Node3D.look_at needs the node in the tree: every look_at below comes after its add_child.
	match ramp:
		"draws", "nodes":
			var l := DirectionalLight3D.new()
			l.rotation_degrees = Vector3(-50, 30, 0)
			_root.add_child(l)
			var bm := BoxMesh.new()
			bm.size = Vector3(0.25, 0.25, 0.25)
			_shared["mesh"] = bm
		"fill":
			var sh := Shader.new()
			sh.code = FILL_SHADER
			_shared["shader"] = sh
			var qm := QuadMesh.new()
			qm.size = Vector2(2, 2)
			_shared["mesh"] = qm
		"lit":
			cam.position = Vector3(0, 9, 6)
			cam.look_at(Vector3(0, 0, -18), Vector3.UP)
			var d := DirectionalLight3D.new()
			d.rotation_degrees = Vector3(-55, -35, 0)
			d.shadow_enabled = true
			d.light_energy = 0.6
			_root.add_child(d)
			for i in 3:
				var o := OmniLight3D.new()
				o.position = Vector3(-10 + i * 10, 5, -12 - (i % 2) * 10)
				o.omni_range = 40.0
				o.shadow_enabled = true
				o.light_color = Color.from_hsv(i / 3.0, 0.4, 1.0)
				o.light_energy = 1.5
				_root.add_child(o)
				var s := SpotLight3D.new()
				s.spot_range = 50.0
				s.spot_angle = 40.0
				s.shadow_enabled = true
				s.light_energy = 3.0
				_root.add_child(s)
				s.look_at_from_position(Vector3(-12 + i * 12, 12, -6 - i * 8), Vector3(-6 + i * 6, 0, -20), Vector3.UP)
			var g := MeshInstance3D.new()
			var pm := PlaneMesh.new()
			pm.size = Vector2(80, 80)
			g.mesh = pm
			g.position = Vector3(0, 0, -20)
			var gm := StandardMaterial3D.new()
			gm.albedo_color = Color(0.55, 0.55, 0.5)
			g.material_override = gm
			_root.add_child(g)
			var sm := SphereMesh.new()
			sm.radius = 0.35
			sm.height = 0.7
			sm.radial_segments = 96
			sm.rings = 48
			_shared["mesh"] = sm
			var mats := []
			for i in 16:
				var m := StandardMaterial3D.new()
				m.albedo_color = Color.from_hsv(i / 16.0, 0.6, 0.9)
				m.metallic = float(i % 4) / 3.0
				m.roughness = 0.15 + float(i / 4) * 0.25
				mats.append(m)
			_shared["mats"] = mats
		"particles":
			cam.position = Vector3(0, 4, 8)
			cam.look_at(Vector3(0, 2, -14), Vector3.UP)
			var pmat := ParticleProcessMaterial.new()
			pmat.direction = Vector3(0, 1, 0)
			pmat.spread = 35.0
			pmat.initial_velocity_min = 2.0
			pmat.initial_velocity_max = 4.0
			pmat.gravity = Vector3(0, -4, 0)
			pmat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
			pmat.emission_sphere_radius = 0.3
			_shared["pmat"] = pmat
			var qm := QuadMesh.new()
			qm.size = Vector2(0.06, 0.06)
			var dm := StandardMaterial3D.new()
			dm.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			dm.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
			dm.albedo_color = Color(1.0, 0.7, 0.3)
			qm.material = dm
			_shared["mesh"] = qm


func _make(ramp: String, i: int) -> Node3D:
	match ramp:
		"draws", "nodes":
			var mi := MeshInstance3D.new()
			mi.mesh = _shared["mesh"]
			var m := StandardMaterial3D.new()
			m.albedo_color = Color(_rng.randf_range(0.2, 1.0), _rng.randf_range(0.2, 1.0), _rng.randf_range(0.2, 1.0))
			mi.material_override = m
			mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			mi.position = Vector3(_rng.randf_range(-10, 10), _rng.randf_range(-6, 6), _rng.randf_range(-28, -12))
			mi.rotation = Vector3(_rng.randf_range(0, TAU), _rng.randf_range(0, TAU), 0)
			return mi
		"fill":
			var mi := MeshInstance3D.new()
			mi.mesh = _shared["mesh"]
			var m := ShaderMaterial.new()
			m.shader = _shared["shader"]
			m.set_shader_parameter("seed", float(i) * 0.618)
			mi.material_override = m
			mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			mi.custom_aabb = AABB(Vector3(-1000, -1000, -1000), Vector3(2000, 2000, 2000))
			return mi
		"lit":
			var mi := MeshInstance3D.new()
			mi.mesh = _shared["mesh"]
			var mats: Array = _shared["mats"]
			mi.material_override = mats[i % mats.size()]
			mi.position = Vector3(_rng.randf_range(-12, 12), _rng.randf_range(0.35, 4.0), _rng.randf_range(-32, -6))
			return mi
		"particles":
			var p := GPUParticles3D.new()
			p.amount = 2048
			p.lifetime = 0.5
			p.fixed_fps = 0
			p.process_material = _shared["pmat"]
			p.draw_pass_1 = _shared["mesh"]
			p.visibility_aabb = AABB(Vector3(-20, -20, -20), Vector3(40, 40, 40))
			p.position = Vector3(_rng.randf_range(-12, 12), _rng.randf_range(0, 2), _rng.randf_range(-24, -6))
			return p
	return null


func _apply(ramp: String, n: int) -> void:
	while _pool.size() < n:
		var nd := _make(ramp, _pool.size())
		_root.add_child(nd)
		_pool.append(nd)
	for i in _pool.size():
		var on := i < n
		var nd: Node3D = _pool[i]
		if nd.visible != on:
			nd.visible = on
			if nd is GPUParticles3D:
				(nd as GPUParticles3D).emitting = on
	_n = n


func _teardown() -> void:
	_ramp = ""
	_n = 0
	_pool = []
	_shared = {}
	if _root:
		_root.queue_free()
		_root = null
	# Let the frees go through before the next ramp starts building.
	await get_tree().process_frame
	await get_tree().process_frame
