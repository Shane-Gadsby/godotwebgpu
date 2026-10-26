extends Node3D
## Occlusion-culling benchmark: a city of GRID x GRID buildings (each a box mesh with a BoxOccluder3D) and N small
## props in the streets, each with one of `mats` materials. Two camera views, each measured with the viewport's
## occlusion culling off and on, interleaved over ROUNDS rounds:
##   street  walks down a street at eye height: most props are behind buildings (what occlusion culling is for);
##   top     looks straight down from above the city: nearly everything is visible, so occlusion culling can only cost
##           (its raycasting and per-instance tests still run).
## The camera path restarts with every phase, so the off and on phases of a view draw the same frames.
## Output, one line per measured phase (parsed by run.mjs):
##   [OCC] phase=<view>_<off|on> round=R frames=F fps=.. draws=.. objects=.. prims=.. vp_cpu_ms=.. vp_gpu_ms=..
## draws/objects/prims are per-frame means of RenderingServer.get_rendering_info(); vp_*_ms are the viewport's measured
## render times (CPU: RenderingServer's side of the frame; GPU: timestamp queries). On the web, run.mjs also times every
## requestAnimationFrame callback (the whole engine frame in a nothreads export) between the phase marks.
## Settings from the page's query string or `-- --n=6000`: n (props), mats (materials), rays (occlusion rays per
## thread), rounds, settle, measure (s).

const GRID := 24
const BLOCK := 12.0 # building footprint
const STREET := 8.0
const PITCH := BLOCK + STREET

var n_props := 6000
var n_mats := 64 # distinct materials; = n stops Forward+ from instancing identical props, one draw each
var rounds := 3
var settle_s := 2.0
var measure_s := 4.0

var cam: Camera3D
var phases: Array = []
var phase_i := -1
var phase_t := 0.0
var phase_name := ""
var measuring := false
var acc := {}
var start_us := 0
var rng := RandomNumberGenerator.new()


func _arg(key: String, def: String) -> String:
	var s := ""
	if OS.has_feature("web"):
		s = str(JavaScriptBridge.eval("location.search", true))
	for a in OS.get_cmdline_user_args():
		s += "&" + a.trim_prefix("--")
	for kv in s.trim_prefix("?").split("&", false):
		var p: PackedStringArray = kv.split("=")
		if p.size() == 2 and p[0] == key:
			return p[1]
	return def


func _mark(what: String) -> void:
	if OS.has_feature("web"):
		JavaScriptBridge.eval("window.__occMark && window.__occMark('%s')" % what, true)


func _ready() -> void:
	n_props = int(_arg("n", str(n_props)))
	n_mats = int(_arg("mats", str(n_mats)))
	rounds = int(_arg("rounds", str(rounds)))
	settle_s = float(_arg("settle", str(settle_s)))
	measure_s = float(_arg("measure", str(measure_s)))
	rng.seed = 12345
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.5, 0.6, 0.75)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.4, 0.4, 0.45)
	add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	add_child(sun)

	var ground := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(GRID * PITCH + 200, GRID * PITCH + 200)
	ground.mesh = pm
	ground.position = Vector3(GRID * PITCH * 0.5, 0, GRID * PITCH * 0.5)
	add_child(ground)

	var bmat := StandardMaterial3D.new()
	bmat.albedo_color = Color(0.7, 0.68, 0.65)
	var bmesh := BoxMesh.new()
	bmesh.material = bmat
	for gx in GRID:
		for gz in GRID:
			var h := rng.randf_range(10.0, 40.0)
			var pos := Vector3(gx * PITCH + BLOCK * 0.5, h * 0.5, gz * PITCH + BLOCK * 0.5)
			var b := MeshInstance3D.new()
			b.mesh = bmesh
			b.scale = Vector3(BLOCK, h, BLOCK)
			b.position = pos
			add_child(b)
			var oc := OccluderInstance3D.new()
			var box := BoxOccluder3D.new()
			box.size = Vector3(BLOCK, h, BLOCK)
			oc.occluder = box
			oc.position = pos
			add_child(oc)

	var mats: Array = []
	for i in n_mats:
		var m := StandardMaterial3D.new()
		m.albedo_color = Color.from_hsv(float(i) / n_mats, 0.7, 0.9)
		mats.append(m)
	var pmesh := BoxMesh.new()
	pmesh.size = Vector3(0.6, 0.6, 0.6)
	var placed := 0
	while placed < n_props:
		var x := rng.randf_range(0.0, GRID * PITCH)
		var z := rng.randf_range(0.0, GRID * PITCH)
		if fmod(x, PITCH) < BLOCK and fmod(z, PITCH) < BLOCK:
			continue # inside a building
		var p := MeshInstance3D.new()
		p.mesh = pmesh
		p.material_override = mats[placed % n_mats]
		p.position = Vector3(x, rng.randf_range(0.3, 2.5), z)
		p.rotation.y = rng.randf() * TAU
		add_child(p)
		placed += 1

	cam = Camera3D.new()
	cam.far = 600.0
	add_child(cam)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	# Occlusion buffer size = rays per thread x WorkerThreadPool threads (1 in a nothreads export, so 512 rays by default).
	var rays := int(_arg("rays", "0"))
	if rays > 0:
		RenderingServer.viewport_set_occlusion_rays_per_thread(rays)

	# Warm-up (pipeline compiles) in every configuration, then the measured rounds.
	for v in ["street", "top"]:
		for oc in ["on", "off"]:
			phases.append({"view": v, "oc": oc, "round": 0})
	for r in rounds:
		for v in ["street", "top"]:
			for oc in ["off", "on"]:
				phases.append({"view": v, "oc": oc, "round": r + 1})
	print("[OCC] ready props=%d buildings=%d" % [n_props, GRID * GRID])
	_mark("ready")
	_next_phase()


func _next_phase() -> void:
	phase_i += 1
	if phase_i >= phases.size():
		print("[OCC] done")
		_mark("done")
		set_process(false)
		return
	var ph: Dictionary = phases[phase_i]
	phase_t = 0.0
	measuring = false
	phase_name = "%s_%s" % [ph.view, ph.oc]
	get_viewport().use_occlusion_culling = ph.oc == "on"
	_place_camera()


func _place_camera() -> void:
	var ph: Dictionary = phases[phase_i]
	var span: float = settle_s + measure_s
	var u: float = clamp(phase_t / span, 0.0, 1.0)
	if ph.view == "street":
		var x := 5 * PITCH + BLOCK + STREET * 0.5
		cam.position = Vector3(x, 1.7, lerp(2.0, GRID * PITCH * 0.6, u))
		cam.rotation = Vector3(0, PI + 0.35 * sin(u * TAU * 2.0), 0)
	else:
		cam.position = Vector3(GRID * PITCH * 0.5, 300.0, GRID * PITCH * 0.5)
		cam.rotation = Vector3(-PI * 0.5, u * 0.5, 0)


func _process(delta: float) -> void:
	if phase_i < 0 or phase_i >= phases.size():
		return
	phase_t += delta
	_place_camera()
	var ph: Dictionary = phases[phase_i]
	if not measuring and phase_t >= settle_s:
		measuring = true
		acc = {"frames": 0, "draws": 0.0, "objects": 0.0, "prims": 0.0, "cpu": 0.0, "gpu": 0.0}
		start_us = Time.get_ticks_usec()
		if ph.round > 0:
			_mark("start:" + phase_name)
		return
	if measuring:
		var vp := get_viewport().get_viewport_rid()
		acc.frames += 1
		acc.draws += RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME)
		acc.objects += RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_OBJECTS_IN_FRAME)
		acc.prims += RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_PRIMITIVES_IN_FRAME)
		acc.cpu += RenderingServer.viewport_get_measured_render_time_cpu(vp)
		acc.gpu += RenderingServer.viewport_get_measured_render_time_gpu(vp)
		if phase_t >= settle_s + measure_s:
			var el := (Time.get_ticks_usec() - start_us) / 1e6
			var f: float = max(1, acc.frames)
			if ph.round > 0:
				_mark("end:" + phase_name)
				print("[OCC] phase=%s round=%d frames=%d fps=%.1f draws=%.0f objects=%.0f prims=%.0f vp_cpu_ms=%.3f vp_gpu_ms=%.3f" % [
					phase_name, ph.round, acc.frames, acc.frames / el, acc.draws / f, acc.objects / f, acc.prims / f, acc.cpu / f, acc.gpu / f])
			_next_phase()
