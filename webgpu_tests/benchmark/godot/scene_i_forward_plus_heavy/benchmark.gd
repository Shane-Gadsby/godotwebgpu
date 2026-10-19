extends Node3D

# Heavy Forward+ load: many shadowed omni/spot lights, a 4-split directional,
# a few hundred casters, and SSAO + SDFGI + glow + volumetric fog on top.
# Exists so godotWebGPUFrameStats has a Forward+ scene that is not at the display rate.
const OMNI_COUNT := 12
const SPOT_COUNT := 6
const MESH_COUNT := 600
const ARENA := 40.0

func _ready() -> void:
	seed(1234)
	var cam := Camera3D.new()
	cam.transform.origin = Vector3(0, 22, 38)
	cam.transform = cam.transform.looking_at(Vector3(0, 2, 0), Vector3.UP)
	cam.far = 150.0
	add_child(cam)

	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_SKY
	e.sky = Sky.new()
	e.sky.sky_material = ProceduralSkyMaterial.new()
	e.sdfgi_enabled = true
	e.sdfgi_use_occlusion = true
	e.ssao_enabled = true
	e.ssil_enabled = true
	e.glow_enabled = true
	e.volumetric_fog_enabled = true
	e.volumetric_fog_density = 0.02
	e.fog_enabled = true
	e.tonemap_mode = Environment.TONE_MAPPER_ACES
	env.environment = e
	add_child(env)

	var fl := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(ARENA * 2.5, ARENA * 2.5)
	fl.mesh = pm
	add_child(fl)

	var box := BoxMesh.new()
	box.size = Vector3(1.5, 3.0, 1.5)
	var sph := SphereMesh.new()
	for i in MESH_COUNT:
		var m := MeshInstance3D.new()
		m.mesh = box if i % 3 else sph
		var mat := StandardMaterial3D.new()
		mat.albedo_color = Color(randf_range(0.3, 1.0), randf_range(0.3, 1.0), randf_range(0.3, 1.0))
		mat.roughness = randf()
		mat.metallic = randf() * 0.5
		m.set_surface_override_material(0, mat)
		m.transform.origin = Vector3(randf_range(-ARENA, ARENA), 1.5, randf_range(-ARENA, ARENA))
		add_child(m)

	for i in OMNI_COUNT:
		var l := OmniLight3D.new()
		var a := float(i) / OMNI_COUNT * TAU
		l.transform.origin = Vector3(cos(a) * ARENA * 0.6, 5.0 + sin(a * 2.0) * 3.0, sin(a) * ARENA * 0.6)
		l.omni_range = 22.0
		l.light_energy = 2.0
		l.shadow_enabled = true
		l.light_volumetric_fog_energy = 1.0
		l.light_color = Color(randf_range(0.6, 1.0), randf_range(0.6, 1.0), randf_range(0.6, 1.0))
		add_child(l)

	for i in SPOT_COUNT:
		var s := SpotLight3D.new()
		s.transform.origin = Vector3(randf_range(-20, 20), 12.0, randf_range(-20, 20))
		s.transform = s.transform.looking_at(Vector3(s.transform.origin.x, 0, s.transform.origin.z), Vector3.RIGHT)
		s.spot_range = 20.0
		s.spot_angle = 40.0
		s.light_energy = 4.0
		s.shadow_enabled = true
		add_child(s)

	var d := DirectionalLight3D.new()
	d.rotation_degrees = Vector3(-50, 30, 0)
	d.shadow_enabled = true
	d.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	add_child(d)
