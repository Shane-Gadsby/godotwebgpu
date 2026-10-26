extends Node3D
## One NaN pixel through the bokeh depth of field (webgpu_notes/TASKS.md §3, the NaN x 0 DOF
## spread). The DOF filters weight every sample with mix(center, sample, m); NaN * 0 is NaN, so one
## non-finite pixel used to blank everything within the blur radius in the bokeh's shape. The DOF
## passes now treat a non-finite color as black (dof_finite() in bokeh_dof_inc.glsl).
##
## For each bokeh shape, near and far blur, and Godot's two transition modes (a positive transition,
## and -1, the "physical" one that always uses the full blur size), it renders a bright scene with
## and without a tiny quad that writes NaN, and counts the pixels the quad turned black. Before the
## fix that was a hexagon, box or disc of hundreds of pixels; now it is the quad itself, a few
## pixels, darkened neighbors aside (a black sample in their blur is the correct result).
##
##   godot --path . [--rendering-method mobile] --resolution 640x360 -- --out=<dir>
##
## Prints "DOFNAN <mode> black=<n>" per case and "DOFNAN DONE failures=<n>". Needs a real renderer
## (not --headless). Forward+ runs the compute DOF, Mobile the raster one (bokeh_dof_raster.glsl).
## The viewport is HDR so that Mobile's color buffer is float and can hold the NaN at all.

# The quad covers about 2x2 pixels; anything well past that is the spread this test exists for.
const MAX_BLACK := 16

var cam: Camera3D
var attrs: CameraAttributesPractical
var nan_quad: MeshInstance3D
var out_dir := ""


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out_dir = a.substr(6)
	if not out_dir.is_empty():
		DirAccess.make_dir_recursive_absolute(out_dir)
	# Forward Mobile renders 3D into RGB10A2 unless the viewport is HDR, and a unorm target stores a
	# NaN as 0, so without this its raster DOF would never see one. Forward+ is float either way.
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
	var env := Environment.new()
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


func _run() -> void:
	var failures := 0
	var shapes := {"box": RenderingServer.DOF_BOKEH_BOX, "hexagon": RenderingServer.DOF_BOKEH_HEXAGON, "circle": RenderingServer.DOF_BOKEH_CIRCLE}
	for shape_name: String in shapes:
		RenderingServer.camera_attributes_set_dof_blur_bokeh_shape(shapes[shape_name])
		for side in ["far", "near"]:
			for transition in [1.0, -1.0]:
				attrs.dof_blur_far_enabled = side == "far"
				attrs.dof_blur_near_enabled = side == "near"
				attrs.dof_blur_far_distance = 5.0
				attrs.dof_blur_far_transition = transition
				attrs.dof_blur_near_distance = 3.0
				attrs.dof_blur_near_transition = transition
				attrs.dof_blur_amount = 0.3
				# Far: the quad sits in front of the backdrop, deep in the blur. Near: 1 m from the camera.
				var dist := 18.0 if side == "far" else 1.0
				# About 2 px across at 640x360 and 50 degrees.
				var px := 2.0 * dist * tan(deg_to_rad(25.0)) / 360.0
				(nan_quad.mesh as QuadMesh).size = Vector2(px, px) * 2.0
				nan_quad.position = Vector3(0.3 * dist / 18.0, 0.2 * dist / 18.0, -dist)
				nan_quad.visible = false
				await _frames(4)
				var ref := get_viewport().get_texture().get_image()
				nan_quad.visible = true
				await _frames(4)
				var img := get_viewport().get_texture().get_image()
				var mode := "%s_%s_%s" % [shape_name, side, "physical" if transition < 0.0 else "transition"]
				var black := _black_count(img, ref)
				var ok := black <= MAX_BLACK
				if not ok:
					failures += 1
				print("DOFNAN %s %s black=%d (max %d)" % ["PASS" if ok else "FAIL", mode, black, MAX_BLACK])
				if not out_dir.is_empty():
					img.save_png("%s/%s.png" % [out_dir, mode])
	# Without DOF the quad is just its own few black pixels: the control.
	attrs.dof_blur_far_enabled = false
	attrs.dof_blur_near_enabled = false
	nan_quad.visible = false
	await _frames(4)
	var ref0 := get_viewport().get_texture().get_image()
	nan_quad.visible = true
	await _frames(4)
	print("DOFNAN INFO no_dof black=%d" % _black_count(get_viewport().get_texture().get_image(), ref0))
	print("DOFNAN DONE failures=%d" % failures)
	get_tree().quit(1 if failures > 0 else 0)
