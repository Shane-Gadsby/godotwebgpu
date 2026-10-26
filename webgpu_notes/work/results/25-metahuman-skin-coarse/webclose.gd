extends Node3D
# Web counterpart of closeup.gd: the same lights and cameras; skin parameters from the URL hash
# (#detail_normal_tiling=32&detail_normal_strength=0.25). Prints "SHOT <name>" when a shot is on screen.
var cam: Camera3D
func _ready() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.32, 0.34, 0.38)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.55, 0.58, 0.65)
	env.ambient_light_energy = 0.6
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	var key := DirectionalLight3D.new()
	key.rotation_degrees = Vector3(-25, 35, 0)
	key.light_energy = 2.2
	key.shadow_enabled = true
	add_child(key)
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(-10, -140, 0)
	fill.light_energy = 0.6
	add_child(fill)
	var ava: Node3D = (load("res://MH/head.dna") as PackedScene).instantiate()
	add_child(ava)
	var hash := ""
	if OS.has_feature("web"):
		hash = str(JavaScriptBridge.eval("location.hash.substring(1)"))
	var n := 0
	for mi in ava.find_children("*", "MeshInstance3D", true, false):
		if mi.mesh == null:
			continue
		for s in mi.mesh.get_surface_count():
			var mat := mi.mesh.surface_get_material(s) as ShaderMaterial
			if mat and mat.shader and mat.shader.code.contains("detail_normal_map"):
				for kv in hash.split("&", false):
					var p := kv.split("=")
					mat.set_shader_parameter(p[0], float(p[1]))
				n += 1
	print("skin materials touched: ", n, " params: ", hash)
	cam = Camera3D.new()
	cam.fov = 30
	add_child(cam)
	var shots := [["cheek_0.25m", Vector3(0.1, 1.60, 0.27), Vector3(0.035, 1.585, 0.06)],
		["face_0.45m", Vector3(0.12, 1.62, 0.45), Vector3(0.0, 1.60, 0.04)],
		["face_0.9m", Vector3(0.22, 1.63, 0.88), Vector3(0.0, 1.60, 0.04)],
		["arm_0.5m", Vector3(0.55, 1.25, 0.35), Vector3(0.25, 1.15, 0.0)]]
	for s in shots:
		cam.look_at_from_position(s[1], s[2])
		await get_tree().create_timer(2.5).timeout
		print("SHOT ", s[0])
		await get_tree().create_timer(1.5).timeout
	print("SHOTS DONE")
