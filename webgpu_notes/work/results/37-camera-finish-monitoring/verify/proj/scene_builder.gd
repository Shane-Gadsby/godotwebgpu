extends RefCounted
# Builds the verification scenes for task 37 (camera finish pass) from a dictionary of options.

static func srgb_decode_shader() -> String:
	return """
shader_type spatial;
render_mode unshaded, cull_disabled;
uniform int pattern = 0;
float dec(float c) { return c < 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
void fragment() {
	// UV.y runs down the quad. Display-referred values: the linear tonemapper with sRGB output shows ALBEDO = dec(v) as v.
	float v = 0.0;
	if (pattern == 0) {
		// ramp: top 70% a horizontal ramp 0..1, bottom 30% patches.
		if (SCREEN_UV.y < 0.7) { v = SCREEN_UV.x; } else { v = floor(SCREEN_UV.x * 8.0) / 7.0; }
	} else if (pattern == 1) {
		// step: top half a vertical step 0.35 | 0.65, bottom half flat 0.5.
		if (SCREEN_UV.y < 0.5) { v = SCREEN_UV.x < 0.5 ? 0.35 : 0.65; } else { v = 0.5; }
	}
	ALBEDO = vec3(dec(v));
}
"""

static func build(root: Node, opts: Dictionary) -> Camera3D:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var we := WorldEnvironment.new()
	we.environment = env
	root.add_child(we)

	var attrs := CameraAttributesPractical.new()
	if opts.has("grain_iso"):
		attrs.grain_enabled = true
		attrs.grain_type = CameraAttributes.GRAIN_TYPE_DIGITAL
		attrs.exposure_sensitivity = float(opts["grain_iso"])
		attrs.grain_intensity = float(opts.get("grain_int", 1.0))
	if opts.has("nr"):
		attrs.set("noise_reduction_enabled", true)
		attrs.set("noise_reduction_strength", float(opts["nr"]))
		attrs.set("noise_reduction_chroma_only", opts.get("nr_chroma", "0") == "1")
	if opts.has("sharpen"):
		attrs.set("sharpening_enabled", true)
		attrs.set("sharpening_amount", float(opts["sharpen"]))
		attrs.set("sharpening_radius", float(opts.get("radius", 1.0)))
		attrs.set("sharpening_halo", float(opts.get("halo", 0.25)))

	var cam := Camera3D.new()
	cam.attributes = attrs
	var scene: String = opts.get("scene", "ramp")
	if scene == "checker":
		var dof := attrs
		dof.dof_blur_far_enabled = true
		dof.dof_blur_far_distance = 6.0
		dof.dof_blur_far_transition = 1.0
		dof.dof_blur_amount = 0.2
		cam.position = Vector3(0, 0, 0)
		var img := Image.create(256, 256, false, Image.FORMAT_RGB8)
		for y in 256:
			for x in 256:
				var c := 0.85 if ((x / 16) + (y / 16)) % 2 == 0 else 0.1
				img.set_pixel(x, y, Color(c, c, c))
		var tex := ImageTexture.create_from_image(img)
		for p in [[Vector3(-1.1, 0, -3.0), 1.0], [Vector3(2.2, 0, -12.0), 4.0]]:
			var mi := MeshInstance3D.new()
			var q := QuadMesh.new()
			q.size = Vector2(2, 2) * p[1]
			mi.mesh = q
			var m := StandardMaterial3D.new()
			m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			m.albedo_texture = tex
			m.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
			mi.material_override = m
			mi.position = p[0]
			root.add_child(mi)
	else:
		cam.projection = Camera3D.PROJECTION_ORTHOGONAL
		cam.size = 2.0
		cam.keep_aspect = Camera3D.KEEP_HEIGHT
		var mi := MeshInstance3D.new()
		var q := QuadMesh.new()
		var vp: Viewport = root if root is Viewport else root.get_viewport()
		var vs := vp.get_visible_rect().size
		q.size = Vector2(100.0, 100.0) # Patterns come from SCREEN_UV.
		mi.mesh = q
		var sm := ShaderMaterial.new()
		var sh := Shader.new()
		sh.code = srgb_decode_shader()
		sm.shader = sh
		sm.set_shader_parameter("pattern", 1 if scene == "step" else 0)
		mi.material_override = sm
		mi.position = Vector3(0, 0, -1)
		root.add_child(mi)

	if opts.has("zebra"):
		cam.set("monitoring_zebras_enabled", true)
		cam.set("monitoring_zebras_level", float(opts["zebra"]))
	if opts.has("zebra2"):
		cam.set("monitoring_zebras_second_enabled", true)
		cam.set("monitoring_zebras_second_level", float(opts["zebra2"]))
	if opts.has("false_color"):
		cam.set("monitoring_false_color_enabled", true)
	if opts.has("peaking"):
		cam.set("monitoring_focus_peaking_enabled", true)
		cam.set("monitoring_focus_peaking_sensitivity", float(opts["peaking"]))
		cam.set("monitoring_focus_peaking_color", Color(1, 0, 0))
	if opts.has("histogram"):
		cam.set("monitoring_histogram_enabled", true)
		cam.set("monitoring_histogram_size", float(opts["histogram"]))
		cam.set("monitoring_histogram_opacity", float(opts.get("hist_opacity", 1.0)))
		cam.set("monitoring_histogram_mode", 1 if opts.get("hist_rgb", "0") == "1" else 0)
		cam.set("monitoring_histogram_position", int(opts.get("hist_pos", 3)))
	if opts.get("vio", "0") == "1":
		cam.set("monitoring_visible_in_output", true)
	root.add_child(cam)
	cam.make_current()
	return cam
