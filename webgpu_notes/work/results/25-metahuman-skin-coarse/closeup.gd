extends SceneTree
# Close-ups of Ava's face skin: closeup.gd <out_dir> <prefix> [param=value ...]
# Params go to every skin ShaderMaterial (detail_normal_strength, detail_normal_tiling, ...).
var out_dir := "res://closeups"
var prefix := "x"
var cam: Camera3D

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	out_dir = args[0]
	prefix = args[1]
	DirAccess.make_dir_recursive_absolute(out_dir)
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.32, 0.34, 0.38)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.55, 0.58, 0.65)
	env.ambient_light_energy = 0.6
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	var we := WorldEnvironment.new()
	we.environment = env
	root.add_child(we)
	var key := DirectionalLight3D.new()
	key.rotation_degrees = Vector3(-25, 35, 0)
	key.light_energy = 2.2
	key.shadow_enabled = true
	root.add_child(key)
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(-10, -140, 0)
	fill.light_energy = 0.6
	root.add_child(fill)
	var ava: Node3D = (load("res://MH/head.dna") as PackedScene).instantiate()
	root.add_child(ava)
	var n := 0
	for mi in ava.find_children("*", "MeshInstance3D", true, false):
		if mi.mesh == null:
			continue
		for s in mi.mesh.get_surface_count():
			var mat := mi.mesh.surface_get_material(s) as ShaderMaterial
			if mat and mat.shader and mat.shader.resource_name == "MetaHuman_skin" or (mat and mat.shader and mat.shader.code.contains("detail_normal_map")):
				for i in range(2, args.size()):
					var kv := args[i].split("=")
					mat.set_shader_parameter(kv[0], float(kv[1]))
				n += 1
	print("skin materials touched: ", n)
	cam = Camera3D.new()
	cam.fov = 30
	root.add_child(cam)
	root.size = Vector2i(1280, 1280)
	# Left cheek of the face (character's right), lit by the key light at a grazing angle.
	await _shot("cheek_0.25m", Vector3(0.1, 1.60, 0.27), Vector3(0.035, 1.585, 0.06))
	await _shot("face_0.45m", Vector3(0.12, 1.62, 0.45), Vector3(0.0, 1.60, 0.04))
	await _shot("face_0.9m", Vector3(0.22, 1.63, 0.88), Vector3(0.0, 1.60, 0.04))
	await _shot("arm_0.5m", Vector3(0.55, 1.25, 0.35), Vector3(0.25, 1.15, 0.0))
	quit()

func _shot(name: String, from: Vector3, to: Vector3) -> void:
	cam.look_at_from_position(from, to)
	for i in 20:
		await process_frame
	await RenderingServer.frame_post_draw
	var img := root.get_texture().get_image()
	img.save_png(out_dir.path_join(prefix + "_" + name + ".png"))
	print("saved ", name)
