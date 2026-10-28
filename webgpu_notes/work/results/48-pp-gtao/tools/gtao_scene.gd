extends Node3D
# GTAO test scene: shared by the native SceneTree script (gtao_test.gd) and the web export (main.tscn).
# The geometry and camera are mirrored exactly in rt_reference.py (the ray traced reference).
# Configuration comes from a dictionary: method (gtao|assao|off), quality, half, blur, taa, view (ao|final),
# radius, intensity, power, sharpness.

const W := 960
const H := 540

var cfg := {}

func setup(p_cfg: Dictionary, p_viewport: Viewport = null) -> void:
	cfg = p_cfg
	var method: String = cfg.get("method", "gtao")
	# "default" leaves the method alone (and runs on editors that predate it): the bit-identical check.
	if method != "default" and method != "off":
		RenderingServer.call("environment_set_ssao_method", 1 if method == "gtao" else 0)
	RenderingServer.environment_set_ssao_quality(int(cfg.get("quality", "2")), cfg.get("half", "0") == "1", 0.5, int(cfg.get("blur", "2")), 50.0, 300.0)

	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.0, 0.0, 0.0)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(1, 1, 1)
	env.ambient_light_energy = 1.0
	env.ssao_enabled = method != "off"
	env.ssao_radius = float(cfg.get("radius", "1.0"))
	env.ssao_intensity = float(cfg.get("intensity", "2.0"))
	env.ssao_power = float(cfg.get("power", "1.5"))
	env.ssao_sharpness = float(cfg.get("sharpness", "0.98"))
	# Full AO on ambient light, so the final image shows the AO directly (a white unlit-by-lights scene).
	env.ssao_light_affect = 0.0
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)

	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(1, 1, 1)
	mat.roughness = 1.0

	var plane := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(20, 20)
	plane.mesh = pm
	plane.material_override = mat
	add_child(plane)

	_box(Vector3(1, 1, 1), Vector3(0, 0.5, 0), 30.0, mat)
	_box(Vector3(5, 2, 0.3), Vector3(0, 1.0, -1.6), 0.0, mat)
	_box(Vector3(0.3, 0.6, 0.3), Vector3(-1.2, 0.3, 0.6), 0.0, mat)

	var sphere := MeshInstance3D.new()
	var sm := SphereMesh.new()
	sm.radius = 0.5
	sm.height = 1.0
	sm.radial_segments = 256
	sm.rings = 128
	sphere.mesh = sm
	sphere.position = Vector3(1.25, 0.5, 0.55)
	sphere.material_override = mat
	add_child(sphere)

	var cam := Camera3D.new()
	cam.fov = 60.0
	cam.near = 0.05
	cam.far = 100.0
	add_child(cam)
	cam.transform = Transform3D(Basis(), Vector3(2.6, 1.9, 3.4)).looking_at(Vector3(0, 0.45, 0), Vector3.UP)
	cam.current = true

	# In a SceneTree script's _init the node is not inside the tree yet: fall back to the root viewport.
	var vp: Viewport = p_viewport if p_viewport else get_viewport()
	vp.use_taa = cfg.get("taa", "0") == "1"
	vp.msaa_3d = Viewport.MSAA_DISABLED
	vp.screen_space_aa = Viewport.SCREEN_SPACE_AA_DISABLED
	if cfg.get("view", "ao") == "ao":
		vp.debug_draw = Viewport.DEBUG_DRAW_SSAO
	elif cfg.get("view", "ao") == "normal":
		vp.debug_draw = Viewport.DEBUG_DRAW_NORMAL_BUFFER

func _box(p_size: Vector3, p_pos: Vector3, p_yaw_deg: float, p_mat: Material) -> void:
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = p_size
	mi.mesh = bm
	mi.position = p_pos
	mi.rotation_degrees = Vector3(0, p_yaw_deg, 0)
	mi.material_override = p_mat
	add_child(mi)
