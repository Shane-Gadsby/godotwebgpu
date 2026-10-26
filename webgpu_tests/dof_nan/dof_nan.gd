extends Node3D
## One NaN pixel through the post effects that read the scene color (webgpu_notes/TASKS.md §3, the
## NaN x 0 DOF spread, and its follow-up for glow, SSR and auto exposure). A filter that weights
## samples (mix(center, sample, m), a gaussian, an average) turns one non-finite pixel into a NaN
## over its whole reach, because NaN * 0 is still NaN: the bokeh DOF blanked a hexagon, glow its
## radius, SSR a reflection blur, and auto exposure averaged it into the luminance of the whole frame.
## Each effect now counts a non-finite scene color as black where it reads it (finite_inc.glsl).
##
## Every case renders a bright scene with and without a tiny quad that writes NaN, and counts the
## pixels the quad turned black (or left NaN). Before the fixes that was a shape of hundreds of
## pixels or the whole frame; now it is the quad itself, a few pixels, darkened neighbors aside (a
## black sample in their blur is the correct result). Auto exposure is also checked on the frame's
## mean brightness, since a poisoned luminance can brighten the frame instead of blanking it.
##
##   godot --path . [--rendering-method mobile] --resolution 640x360 -- --out=<dir> [--only=dof,glow,ssr,exposure]
##
## Prints "DOFNAN <PASS|FAIL> <case> black=<n>" per case and "DOFNAN DONE failures=<n>". Needs a real
## renderer (not --headless). Forward+ runs the compute DOF, glow and luminance reduction, Mobile the
## raster ones; SSR is Forward+ only and is skipped (and says so) on Mobile. The viewport is HDR so
## that Mobile's color buffer is float and can hold the NaN at all.

# The quad covers about 2x2 pixels; anything well past that is the spread this test exists for.
const MAX_BLACK := 16
# A poisoned luminance changes the exposure of every pixel; one black pixel in 230,400 changes the
# average by far less than this.
const MAX_MEAN_CHANGE := 0.02
# Glow and SSR are also gated on the pixels the quad changed at all, black or not: NVIDIA's min()
# returns the other operand, so its glow turned the NaN into a blob at the luminance cap (59,791
# pixels on Forward+, 1,591 on Mobile) instead of a black one. Fixed, glow changes the quad's own 4
# and SSR adds the black quad's reflection (55 on the mirror floor).
const MAX_CHANGED_GLOW := 64
const MAX_CHANGED_SSR := 256

var cam: Camera3D
var attrs: CameraAttributesPractical
var env: Environment
var nan_quad: MeshInstance3D
var floor_mesh: MeshInstance3D
var out_dir := ""
var only := PackedStringArray()
var failures := 0


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out_dir = a.substr(6)
		elif a.begins_with("--only="):
			only = a.substr(7).split(",")
	if not out_dir.is_empty():
		DirAccess.make_dir_recursive_absolute(out_dir)
	# Forward Mobile renders 3D into RGB10A2 unless the viewport is HDR, and a unorm target stores a
	# NaN as 0, so without this its raster effects would never see one. Forward+ is float either way.
	get_viewport().use_hdr_2d = true
	_build()
	_run()


func _unshaded(code: String) -> ShaderMaterial:
	var sh := Shader.new()
	sh.code = code
	var m := ShaderMaterial.new()
	m.shader = sh
	return m


func _build() -> void:
	env = Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.8, 0.8, 0.8)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)

	attrs = CameraAttributesPractical.new()
	cam = Camera3D.new()
	cam.fov = 50.0
	cam.attributes = attrs
	cam.current = true
	add_child(cam)

	# A bright checker backdrop 20 m away, so the far blur has something finite to blur.
	var back := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(60, 40)
	pm.orientation = PlaneMesh.FACE_Z
	back.mesh = pm
	back.material_override = _unshaded("""shader_type spatial;
render_mode unshaded;
void fragment() {
	vec2 c = floor(UV * vec2(30.0, 20.0));
	ALBEDO = vec3(0.5 + 0.3 * mod(c.x + c.y, 2.0));
}
""")
	back.position = Vector3(0, 0, -20)
	add_child(back)

	# A mirror floor for SSR, 1.5 m below the camera, only in the SSR cases.
	floor_mesh = MeshInstance3D.new()
	var fm := PlaneMesh.new()
	fm.size = Vector2(60, 40)
	floor_mesh.mesh = fm
	var floor_mat := StandardMaterial3D.new()
	floor_mat.albedo_color = Color(1, 1, 1)
	floor_mat.metallic = 1.0
	floor_mat.roughness = 0.0
	floor_mesh.material_override = floor_mat
	floor_mesh.position = Vector3(0, -1.5, -10)
	floor_mesh.visible = false
	add_child(floor_mesh)

	# The NaN: built from a uniform's bits at run time, so no compiler can fold it, and WGSL's
	# rule against a NaN constant expression never applies.
	var q := QuadMesh.new()
	nan_quad = MeshInstance3D.new()
	nan_quad.mesh = q
	var m := _unshaded("""shader_type spatial;
render_mode unshaded, cull_disabled;
uniform uint nan_bits = 2143289344u;
void fragment() {
	ALBEDO = vec3(uintBitsToFloat(nan_bits));
}
""")
	nan_quad.material_override = m
	add_child(nan_quad)


func _frames(n: int) -> void:
	for i in n:
		await RenderingServer.frame_post_draw


func _black_count(img: Image, ref: Image) -> int:
	var n := 0
	for y in img.get_height():
		for x in img.get_width():
			var c := img.get_pixel(x, y)
			var r := ref.get_pixel(x, y)
			# Black (or still NaN: the HDR viewport's image is float) where the reference is not.
			if not (c.r + c.g + c.b >= 0.03) and r.r + r.g + r.b > 0.3:
				n += 1
	return n


# Pixels the quad changed by more than a tenth (summed over RGB), black or not: a NaN that a
# min() or clamp() turned into its bound (NVIDIA returns the other operand) shows as a bright or
# dark blob instead of a black one.
func _changed_count(img: Image, ref: Image) -> int:
	var n := 0
	for y in img.get_height():
		for x in img.get_width():
			var c := img.get_pixel(x, y)
			var r := ref.get_pixel(x, y)
			if not (absf(c.r - r.r) + absf(c.g - r.g) + absf(c.b - r.b) <= 0.1):
				n += 1
	return n


# Mean brightness of the frame, NaN counted as 0 so that a NaN frame reads as dark, not as NaN.
func _mean(img: Image) -> float:
	var s := 0.0
	for y in img.get_height():
		for x in img.get_width():
			var c := img.get_pixel(x, y)
			var v := c.r + c.g + c.b
			if v >= 0.0 and v < 1.0e30:
				s += v
	return s / float(img.get_width() * img.get_height() * 3)


# Places the quad at `dist` meters, about 2 px across at 640x360 and 50 degrees.
func _place_quad(dist: float, offset: Vector2) -> void:
	var px := 2.0 * dist * tan(deg_to_rad(25.0)) / 360.0
	(nan_quad.mesh as QuadMesh).size = Vector2(px, px) * 2.0
	nan_quad.position = Vector3(offset.x * dist / 18.0, offset.y * dist / 18.0, -dist)


# Renders without and with the quad, prints the result and counts a failure.
func _measure(mode: String, settle: int, check_mean := false, max_changed := -1) -> void:
	nan_quad.visible = false
	await _frames(settle)
	var ref := get_viewport().get_texture().get_image()
	nan_quad.visible = true
	await _frames(settle)
	var img := get_viewport().get_texture().get_image()
	var black := _black_count(img, ref)
	var changed := _changed_count(img, ref)
	var ok := black <= MAX_BLACK
	var extra := " changed=%d" % changed
	if max_changed >= 0:
		ok = ok and changed <= max_changed
		extra += " (max %d)" % max_changed
	if check_mean:
		var m0 := _mean(ref)
		var m1 := _mean(img)
		var change := absf(m1 - m0) / maxf(m0, 1.0e-6)
		ok = ok and change <= MAX_MEAN_CHANGE
		extra += " mean=%.4f->%.4f change=%.4f (max %.2f)" % [m0, m1, change, MAX_MEAN_CHANGE]
	if not ok:
		failures += 1
	print("DOFNAN %s %s black=%d (max %d)%s" % ["PASS" if ok else "FAIL", mode, black, MAX_BLACK, extra])
	if not out_dir.is_empty():
		img.save_png("%s/%s.png" % [out_dir, mode])
		ref.save_png("%s/%s_ref.png" % [out_dir, mode])
	nan_quad.visible = false


func _wants(section: String) -> bool:
	return only.is_empty() or only.has(section)


func _reset() -> void:
	attrs.dof_blur_far_enabled = false
	attrs.dof_blur_near_enabled = false
	attrs.auto_exposure_enabled = false
	env.glow_enabled = false
	env.ssr_enabled = false
	floor_mesh.visible = false
	cam.transform = Transform3D()


func _run() -> void:
	var forward_plus: bool = RenderingServer.get_current_rendering_method() == "forward_plus"
	if _wants("dof"):
		await _run_dof()
	if _wants("glow"):
		await _run_glow()
	if _wants("ssr"):
		if forward_plus:
			await _run_ssr()
		else:
			print("DOFNAN SKIP ssr (Forward+ only)")
	# Last: a poisoned luminance would persist into the cases after it.
	if _wants("exposure"):
		await _run_exposure()
	# Without any effect the quad is just its own few black pixels: the control.
	_reset()
	_place_quad(18.0, Vector2(0.3, 0.2))
	nan_quad.visible = false
	await _frames(4)
	var ref0 := get_viewport().get_texture().get_image()
	nan_quad.visible = true
	await _frames(4)
	print("DOFNAN INFO no_effect black=%d" % _black_count(get_viewport().get_texture().get_image(), ref0))
	print("DOFNAN DONE failures=%d" % failures)
	get_tree().quit(1 if failures > 0 else 0)


# For each bokeh shape, near and far blur, and Godot's two transition modes (a positive transition,
# and -1, the "physical" one that always uses the full blur size).
func _run_dof() -> void:
	var shapes := {"box": RenderingServer.DOF_BOKEH_BOX, "hexagon": RenderingServer.DOF_BOKEH_HEXAGON, "circle": RenderingServer.DOF_BOKEH_CIRCLE}
	for shape_name: String in shapes:
		RenderingServer.camera_attributes_set_dof_blur_bokeh_shape(shapes[shape_name])
		for side in ["far", "near"]:
			for transition in [1.0, -1.0]:
				_reset()
				attrs.dof_blur_far_enabled = side == "far"
				attrs.dof_blur_near_enabled = side == "near"
				attrs.dof_blur_far_distance = 5.0
				attrs.dof_blur_far_transition = transition
				attrs.dof_blur_near_distance = 3.0
				attrs.dof_blur_near_transition = transition
				attrs.dof_blur_amount = 0.3
				# Far: the quad sits in front of the backdrop, deep in the blur. Near: 1 m from the camera.
				_place_quad(18.0 if side == "far" else 1.0, Vector2(0.3, 0.2))
				await _measure("%s_%s_%s" % [shape_name, side, "physical" if transition < 0.0 else "transition"], 4)
	_reset()


# Glow reads the scene color in its first downsample (Forward+: the compute gaussian, Mobile: the
# raster gather) and blurs it over every level, so a NaN there reaches the whole glow radius. A low
# threshold makes the whole bright backdrop glow.
func _run_glow() -> void:
	for blend: int in [Environment.GLOW_BLEND_MODE_ADDITIVE, Environment.GLOW_BLEND_MODE_SOFTLIGHT]:
		_reset()
		env.glow_enabled = true
		env.glow_blend_mode = blend
		env.glow_intensity = 0.4
		env.glow_bloom = 0.1
		env.glow_hdr_threshold = 0.7
		for i in 7:
			env.set_glow_level(i, 1.0)
		_place_quad(10.0, Vector2(0.3, 0.2))
		await _measure("glow_%s" % ("additive" if blend == Environment.GLOW_BLEND_MODE_ADDITIVE else "softlight"), 4, false, MAX_CHANGED_GLOW)
	_reset()


# SSR reads the scene color (last frame's, through RB_LAST_FRAME) at the reflected hit and filters
# rough reflections, so a NaN on screen comes back in the floor's reflection, and blurred with it.
func _run_ssr() -> void:
	for roughness in [0.0, 0.4]:
		_reset()
		env.ssr_enabled = true
		env.ssr_max_steps = 128
		floor_mesh.visible = true
		(floor_mesh.material_override as StandardMaterial3D).roughness = roughness
		# 8 m out and 1 m above the floor: its mirror image is on screen at about 17 degrees down.
		var dist := 8.0
		var px := 2.0 * dist * tan(deg_to_rad(25.0)) / 360.0
		(nan_quad.mesh as QuadMesh).size = Vector2(px, px) * 2.0
		nan_quad.position = Vector3(0.4, -0.5, -dist)
		await _measure("ssr_roughness_%.1f" % roughness, 10, false, MAX_CHANGED_SSR)
	_reset()


# Auto exposure averages the scene color's luminance down to one value that scales every pixel, so
# one NaN in it reaches the whole frame (and, through the previous frame's luminance, every frame
# after it). Forward+ reduces with compute, Mobile with raster passes.
func _run_exposure() -> void:
	_reset()
	attrs.auto_exposure_enabled = true
	attrs.auto_exposure_scale = 0.4
	attrs.auto_exposure_speed = 10.0
	_place_quad(10.0, Vector2(0.3, 0.2))
	await _measure("auto_exposure", 40, true)
	_reset()
