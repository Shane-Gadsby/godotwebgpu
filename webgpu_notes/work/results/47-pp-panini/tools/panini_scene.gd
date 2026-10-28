# panini_scene.gd: the task 47 test scene, shared by panini_test.gd (native) and web_main.gd (web export).
extends RefCounted

const HFOV := 120.0
var cam: Camera3D
var dots := []
var big := []

# case: off | d<distance>s<compression>; layout: target | default.
func build(world: Node3D, case_name: String, layout: String, size: Vector2) -> void:
	# Automatic mesh LOD picks coarse spheres for small dots in Forward+ and Mobile (Compatibility has none), which
	# shifts their centroids and shapes by fractions of a pixel: off, so all renderers see the same geometry.
	if world.is_inside_tree():
		world.get_viewport().mesh_lod_threshold = 0.0
	cam = Camera3D.new()
	cam.fov = rad_to_deg(2.0 * atan(tan(deg_to_rad(HFOV / 2.0)) / (size.x / size.y)))
	cam.near = 0.05
	cam.far = 200.0
	world.add_child(cam)
	cam.current = true
	var attrs := CameraAttributesPractical.new()
	if case_name.begins_with("d"):
		var parts := case_name.substr(1).split("s")
		# set(), so the script also parses in the pre-change editor (the bit-identical check).
		attrs.set("panini_enabled", true)
		attrs.set("panini_distance", float(parts[0]))
		attrs.set("panini_vertical_compression", float(parts[1]))
	cam.attributes = attrs
	var env := Environment.new()
	if layout == "default":
		_build_default(world, env)
	else:
		env.background_mode = Environment.BG_COLOR
		env.background_color = Color(0, 0, 0)
		env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
		_build_target(world)
	var we := WorldEnvironment.new()
	we.environment = env
	world.add_child(we)

func _unshaded(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.albedo_color = c
	return m

func _build_target(world: Node3D) -> void:
	# Green dots on a sphere of radius 10 around the eye, every 8 degrees of azimuth and 10 of elevation.
	var dot_mat := _unshaded(Color(0, 1, 0))
	var r := 10.0
	var dot_radius := r * tan(deg_to_rad(0.5))
	for az in range(-56, 57, 8):
		for el in range(-40, 41, 10):
			var dir := Vector3(sin(deg_to_rad(az)) * cos(deg_to_rad(el)), sin(deg_to_rad(el)), -cos(deg_to_rad(az)) * cos(deg_to_rad(el)))
			var mi := MeshInstance3D.new()
			var sm := SphereMesh.new()
			sm.radius = dot_radius
			sm.height = dot_radius * 2.0
			sm.radial_segments = 32
			sm.rings = 16
			mi.mesh = sm
			mi.material_override = dot_mat
			mi.position = dir * r
			world.add_child(mi)
			dots.append({"az": az, "el": el, "pos": dir * r})
	# White spheres of 2 degrees radius at elevation 5 (between the dot rows), large enough to measure the shape the
	# projection gives them: rectilinear stretches them sideways by 1 / cos(azimuth), the Panini much less.
	var big_mat := _unshaded(Color(1, 1, 1))
	for az in [0, -28, 28, -44, 44, -56, 56]:
		var el := 5.0
		var dir := Vector3(sin(deg_to_rad(az)) * cos(deg_to_rad(el)), sin(deg_to_rad(el)), -cos(deg_to_rad(az)) * cos(deg_to_rad(el)))
		var mi := MeshInstance3D.new()
		var sm := SphereMesh.new()
		sm.radius = r * sin(deg_to_rad(2.0))
		sm.height = sm.radius * 2.0
		sm.radial_segments = 64
		sm.rings = 32
		mi.mesh = sm
		mi.material_override = big_mat
		mi.position = dir * r
		world.add_child(mi)
		big.append({"az": az, "el": el, "pos": dir * r})
	# Red vertical poles (straight vertical lines in the world) at these azimuths, 8 m away horizontally.
	var pole_mat := _unshaded(Color(1, 0, 0))
	for az in [-52, -36, -20, 20, 36, 52]:
		var mi := MeshInstance3D.new()
		var bm := BoxMesh.new()
		bm.size = Vector3(0.05, 16.0, 0.05)
		mi.mesh = bm
		mi.material_override = pole_mat
		mi.position = Vector3(sin(deg_to_rad(az)), 0, -cos(deg_to_rad(az))) * 8.0
		mi.rotation.y = -deg_to_rad(az)
		world.add_child(mi)
	# A blue horizontal rod 6 m ahead, 2.5 m below the eye, across the whole field.
	var rod := MeshInstance3D.new()
	var rm := BoxMesh.new()
	rm.size = Vector3(60.0, 0.05, 0.05)
	rod.mesh = rm
	rod.material_override = _unshaded(Color(0, 0, 1))
	rod.position = Vector3(0, -2.5, -6.0)
	world.add_child(rod)

func _build_default(world: Node3D, env: Environment) -> void:
	# The bit-identical check's scene: sky, sun with shadows, glow, Filmic.
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.glow_enabled = true
	var sun := DirectionalLight3D.new()
	sun.shadow_enabled = true
	sun.rotation_degrees = Vector3(-40, 30, 0)
	world.add_child(sun)
	var floor_mi := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(40, 40)
	floor_mi.mesh = pm
	floor_mi.position = Vector3(0, -1.5, 0)
	world.add_child(floor_mi)
	for i in 7:
		var mi := MeshInstance3D.new()
		mi.mesh = BoxMesh.new() if i % 2 == 0 else SphereMesh.new()
		var m := StandardMaterial3D.new()
		m.albedo_color = Color.from_hsv(i / 7.0, 0.7, 0.9)
		if i == 3:
			m.emission_enabled = true
			m.emission = Color(1, 0.6, 0.2)
			m.emission_energy_multiplier = 8.0
		mi.material_override = m
		mi.position = Vector3((i - 3) * 1.6, -0.8 + 0.2 * i, -5.0 - 0.5 * i)
		world.add_child(mi)
