extends SceneTree

# Renders Ava from a few cameras to PNGs: shot.gd <out_dir> [expression=value ...]

var out_dir := "res://shots"
var ava: Node3D
var cam: Camera3D

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		out_dir = args[0]
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

	ava = (load("res://Ava/head.dna") as PackedScene).instantiate()
	root.add_child(ava)
	var rig: MetaHumanRig = ava.find_child("MetaHumanRig", true, false)
	for i in range(1, args.size()):
		var kv := args[i].split("=")
		rig.set_control(kv[0], float(kv[1]))

	cam = Camera3D.new()
	cam.fov = 30
	root.add_child(cam)
	root.size = Vector2i(900, 1100)

	await _shot("face", Vector3(0.0, 1.63, 0.62), Vector3(0, 1.61, 0.0))
	await _shot("face34", Vector3(0.38, 1.64, 0.5), Vector3(0, 1.61, 0.02))
	await _shot("body", Vector3(0.0, 1.0, 4.2), Vector3(0, 0.92, 0.0))
	await _shot("back", Vector3(0.0, 1.2, -3.0), Vector3(0, 1.0, 0.0))
	quit()

func _shot(name: String, from: Vector3, to: Vector3) -> void:
	cam.look_at_from_position(from, to)
	for i in 12:
		await process_frame
	await RenderingServer.frame_post_draw
	var img := root.get_texture().get_image()
	img.save_png(out_dir.path_join(name + ".png"))
	print("saved ", name)
