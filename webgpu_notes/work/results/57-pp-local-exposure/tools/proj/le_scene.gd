# le_scene.gd: the task 57 (local exposure) test scene, shared by the native runner (le_test.gd) and the web main
# scene (web_main.gd). A dark room with a bright window (emissive panes behind mullions, an omni light beside it), and
# a high-contrast chart on the left wall: 13 emissive patches 0.18 * 2^k, k = -6..6, each with an inner square one stop
# brighter. Everything is deterministic (no noise textures, no shadows, no TAA).
extends RefCounted

var cam: Camera3D
var attributes: CameraAttributes

# opts: le (bool), hc, sc, detail, blend, kernel, bias (floats), ae (bool), tonemap ("linear" | "filmic"),
# glow (bool), phys (bool: CameraAttributesPhysical), layout ("room" | "chart").
func build(world: Node3D, opts: Dictionary) -> void:
	var phys: bool = opts.get("phys", false)
	var scale := 1.0
	if phys:
		scale = 2000.0 # Nits and lumens on the order a room under daylight has.

	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(1, 1, 1)
	env.ambient_light_energy = 0.02 * (scale if phys else 1.0)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR if opts.get("tonemap", "linear") == "linear" else Environment.TONE_MAPPER_FILMIC
	env.glow_enabled = opts.get("glow", false)
	var we := WorldEnvironment.new()
	we.environment = env
	world.add_child(we)

	cam = Camera3D.new()
	cam.fov = 75.0
	world.add_child(cam)
	if opts.get("layout", "room") == "chart":
		cam.position = Vector3(-1.0, 1.5, 0.0)
		cam.rotation_degrees = Vector3(0, 90, 0)
	else:
		cam.position = Vector3(1.4, 1.4, 1.5)
		cam.rotation_degrees = Vector3(-3, 22, 0)

	if phys:
		var a := CameraAttributesPhysical.new()
		attributes = a
	else:
		attributes = CameraAttributesPractical.new()
	if opts.get("ae", false):
		attributes.auto_exposure_enabled = true
		attributes.auto_exposure_speed = 100.0
	if opts.get("le", false):
		attributes.set("local_exposure_enabled", true)
		for k in [["hc", "local_exposure_highlight_contrast"], ["sc", "local_exposure_shadow_contrast"], ["detail", "local_exposure_detail_strength"], ["blend", "local_exposure_blurred_luminance_blend"], ["kernel", "local_exposure_blurred_luminance_kernel_size"], ["bias", "local_exposure_middle_gray_bias"]]:
			if opts.has(k[0]):
				attributes.set(k[1], float(opts[k[0]]))
	cam.attributes = attributes

	# The room: 8 m wide (x -4..4), 3 m tall, 7 m deep (z -5..2); the window wall at z = -5.
	var wall_shader := Shader.new()
	wall_shader.code = """
shader_type spatial;
uniform vec3 base : source_color = vec3(0.5);
void fragment() {
	// Plaster with a fine checker, for detail the local exposure must keep.
	vec2 c = floor(UV * 24.0);
	float k = mod(c.x + c.y, 2.0);
	ALBEDO = base * (0.75 + 0.25 * k);
	ROUGHNESS = 1.0;
	SPECULAR = 0.0;
}
"""
	var wall := ShaderMaterial.new()
	wall.shader = wall_shader
	_box(world, Vector3(0, -0.05, -1.5), Vector3(8, 0.1, 7), wall) # Floor.
	_box(world, Vector3(0, 3.05, -1.5), Vector3(8, 0.1, 7), wall) # Ceiling.
	_box(world, Vector3(-4.05, 1.5, -1.5), Vector3(0.1, 3, 7), wall) # Left.
	_box(world, Vector3(4.05, 1.5, -1.5), Vector3(0.1, 3, 7), wall) # Right.
	_box(world, Vector3(0, 1.5, 2.05), Vector3(8, 3, 0.1), wall) # Back.
	_box(world, Vector3(0, 1.5, -5.05), Vector3(8, 3, 0.1), wall) # Window wall.

	# The window: 2.4 x 1.6 m of sky at 400x middle gray (8.6 stops above), with mullions.
	_emissive(world, Vector3(0.8, 1.7, -4.99), Vector2(2.4, 1.6), Color(0.85, 0.92, 1.0) * 72.0 * scale)
	var mullion := StandardMaterial3D.new()
	mullion.albedo_color = Color(0.05, 0.05, 0.05)
	mullion.roughness = 1.0
	_box(world, Vector3(0.8, 1.7, -4.95), Vector3(0.08, 1.6, 0.06), mullion)
	_box(world, Vector3(0.8, 1.7, -4.95), Vector3(2.4, 0.08, 0.06), mullion)

	var light := OmniLight3D.new()
	light.position = Vector3(0.8, 1.7, -4.3)
	light.omni_range = 12.0
	light.omni_attenuation = 2.0
	if phys:
		light.light_intensity_lumens = 2000.0 * 2.5
	else:
		light.light_energy = 2.5
	world.add_child(light)

	# The chart on the left wall: 13 patches, 0.18 * 2^k, k = -6..6.
	for i in 13:
		var k := i - 6
		var v := 0.18 * pow(2.0, k) * scale
		var z := -4.4 + i * 0.5
		_emissive(world, Vector3(-3.99, 1.5, z), Vector2(0.4, 0.4), Color(v, v, v), true)
		_emissive(world, Vector3(-3.985, 1.5, z), Vector2(0.14, 0.14), Color(v, v, v) * 2.0, true)

func _box(world: Node3D, pos: Vector3, size: Vector3, mat: Material) -> void:
	var mi := MeshInstance3D.new()
	var m := BoxMesh.new()
	m.size = size
	mi.mesh = m
	mi.material_override = mat
	mi.position = pos
	world.add_child(mi)

# An emissive quad facing +z (or +x on the left wall): black albedo, so its light is exactly the emission.
func _emissive(world: Node3D, pos: Vector3, size: Vector2, color: Color, left_wall := false) -> void:
	var mi := MeshInstance3D.new()
	var m := QuadMesh.new()
	m.size = size
	mi.mesh = m
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0, 0, 0)
	mat.roughness = 1.0
	mat.metallic_specular = 0.0
	mat.emission_enabled = true
	mi.material_override = mat
	# The color carries the hue, the energy multiplier the HDR level.
	var peak := maxf(color.r, maxf(color.g, color.b))
	mat.emission = Color(color.r / peak, color.g / peak, color.b / peak)
	mat.emission_energy_multiplier = peak
	mi.position = pos
	if left_wall:
		mi.rotation_degrees = Vector3(0, 90, 0)
	world.add_child(mi)
