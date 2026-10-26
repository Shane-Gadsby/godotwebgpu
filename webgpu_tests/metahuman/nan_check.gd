extends SceneTree

# The groom strand shaders must not write NaN when a strand's coverage interpolates below zero
# (webgpu_notes/TASKS.md §3). Grooms that taper to a width of exactly 0 (Ava's eyelashes) have
# v_coverage = 0 at their tip vertices, and at some pixels beside a tip the interpolated value comes
# out at about -1e-9; pow() of that was NaN, which the bokeh DOF spread into a black hexagon. Where
# that happens depends on the exact camera and rasterizer, so this forces it instead: every strand
# vertex gets v_coverage = -1e-9, and the scene color must hold no NaN. A control copy that keeps the
# unclamped pow() of the old shader must produce NaN, or this renderer cannot show the fault and the
# check says SKIP rather than PASS.
#
#   godot --path <project> --rendering-driver vulkan -s nan_check.gd

const VERTEX_LINE := "v_coverage = clamp(width / drawn, 0.0, 1.0);"
const OLD_ALPHA := "\tALPHA = pow(v_coverage, coverage_exponent) * opacity * (1.0 - 0.6 * smoothstep(0.7, 1.0, v_t));\n"

var failures := 0


func _report(ok: bool, what: String) -> void:
	print(("PASS " if ok else "FAIL ") + what)
	if not ok:
		failures += 1


func _forced(mat: ShaderMaterial, control: bool) -> ShaderMaterial:
	var code := mat.shader.code.replace(VERTEX_LINE, "v_coverage = -1e-9;")
	if control:
		var i := code.find("\tALPHA = ")
		code = code.substr(0, i) + OLD_ALPHA + code.substr(code.find("\n", i) + 1)
	var sh := Shader.new()
	sh.code = code
	var m: ShaderMaterial = mat.duplicate()
	m.shader = sh
	return m


func _initialize() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.4, 0.4, 0.4)
	var we := WorldEnvironment.new()
	we.environment = env
	root.add_child(we)
	var key := DirectionalLight3D.new()
	key.rotation_degrees = Vector3(-25, 35, 0)
	root.add_child(key)

	var ava: Node3D = (load("res://Ava/head.dna") as PackedScene).instantiate()
	root.add_child(ava)
	var cam := Camera3D.new()
	cam.fov = 30
	root.add_child(cam)
	cam.look_at_from_position(Vector3(0.0, 1.63, 0.62), Vector3(0, 1.61, 0.0))
	var probe: CompositorEffect = load("res://nan_probe.gd").new()
	cam.compositor = Compositor.new()
	cam.compositor.compositor_effects = [probe]
	root.size = Vector2i(900, 1100)

	var strands := {}
	for mi: MeshInstance3D in ava.find_children("*", "MeshInstance3D", true, false):
		var m := mi.get_active_material(0) as ShaderMaterial
		if m and m.shader and m.shader.code.contains(VERTEX_LINE):
			strands[mi] = m
			_report(not m.shader.code.contains("pow(v_coverage,"), "%s: the strand shader clamps v_coverage before pow()" % mi.name)
			# The blended brows and lashes are the ones in front of the face.
			if not m.shader.code.contains("ALPHA = pow"):
				mi.visible = false
	_report(strands.size() > 0, "found the groom strand meshes (%d)" % strands.size())

	var results := {}
	for control in [true, false]:
		for mi: MeshInstance3D in strands:
			if mi.visible:
				mi.material_override = _forced(strands[mi], control)
		for i in 8:
			await process_frame
		await RenderingServer.frame_post_draw
		await process_frame
		results[control] = probe.count
		print("INFO coverage forced to -1e-9, %s shader: %d non-finite pixels" % ["old unclamped" if control else "current", probe.count])
	if results[true] <= 0:
		print("SKIP this renderer computes no NaN for pow() of a negative, so it cannot show the fault")
	else:
		_report(results[false] == 0, "strands with coverage below zero write no NaN (%d pixels; the old shader: %d)" % [results[false], results[true]])
	cam.compositor = null
	await RenderingServer.frame_post_draw
	RenderingServer.call_on_render_thread(probe.free_resources)
	await RenderingServer.frame_post_draw
	print("DONE failures=%d" % failures)
	quit(1 if failures > 0 else 0)
