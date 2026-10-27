extends SceneTree
# args: out.png shake(0/1) [use_new_api]
func _initialize() -> void:
	run.call_deferred()
func run() -> void:
	var a := OS.get_cmdline_user_args()
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.2, 0.3, 0.4)
	root.add_child(env)
	var light := DirectionalLight3D.new()
	light.rotation = Vector3(-0.8, 0.5, 0)
	root.add_child(light)
	for i in 5:
		var m := MeshInstance3D.new()
		m.mesh = BoxMesh.new()
		m.position = Vector3(i * 1.5 - 3, 0, -4 - i)
		root.add_child(m)
	var cam := Camera3D.new()
	cam.attributes = CameraAttributesPhysical.new()
	root.add_child(cam)
	cam.make_current()
	if a.size() > 2:
		cam.set("autofocus_mode", 2)
		cam.set("autofocus_points", PackedVector2Array([Vector2(0.5, 0.5)]))
	if a[1] == "1":
		cam.set("shake_enabled", true)
		cam.set("shake_rotation_amplitude", Vector3(1, 1, 0.5))
	for i in 20:
		await process_frame
	await RenderingServer.frame_post_draw
	root.get_texture().get_image().save_png(a[0])
	quit()
