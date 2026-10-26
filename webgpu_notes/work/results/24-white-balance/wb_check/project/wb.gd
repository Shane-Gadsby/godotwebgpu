extends Node3D
## White balance check: a gray card (albedo 0.5) under a white directional light, linear tonemap, no ambient.
## Prints "[WB] <temp> <tint> r g b" (0-255, center of the card) for each setting and saves the frames.
## Left half of the frame: the card. Right half: an emissive patch of linear (1, 0.5, 0.25) to show a
## saturated colour through the same transform.

# Tint is -150..150 at 1/3000 Duv per unit (task 29); 75 is the old 0.5, 30 the old 0.2 of the Forward+ arm.
const SETTINGS := [[6500.0, 0.0], [3200.0, 0.0], [10000.0, 0.0], [6500.0, 75.0], [6500.0, -75.0], [3200.0, 30.0]]
var attrs: CameraAttributesPractical
var out_dir := ""

func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out_dir = a.substr(6)
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0, 0, 0)
	env.ambient_light_energy = 0.0
	env.reflected_light_source = Environment.REFLECTION_SOURCE_DISABLED
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)

	var cam := Camera3D.new()
	attrs = CameraAttributesPractical.new()
	cam.attributes = attrs
	cam.position = Vector3(0, 0, 3)
	add_child(cam)

	var light := DirectionalLight3D.new()
	light.light_color = Color(1, 1, 1)
	light.light_energy = 1.0
	light.rotation = Vector3(0, 0, 0) # Shines along -Z, straight at the card.
	add_child(light)

	var card := MeshInstance3D.new()
	var q := QuadMesh.new()
	q.size = Vector2(2.4, 2.4)
	card.mesh = q
	card.position = Vector3(-1.4, 0, 0)
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(0.5, 0.5, 0.5).linear_to_srgb()
	m.roughness = 1.0
	m.metallic_specular = 0.0
	card.material_override = m
	add_child(card)

	var patch := MeshInstance3D.new()
	patch.mesh = q
	patch.position = Vector3(1.4, 0, 0)
	var pm := StandardMaterial3D.new()
	pm.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	pm.albedo_color = Color(1.0, 0.5, 0.25).linear_to_srgb()
	patch.material_override = pm
	add_child(patch)

	print("[WB] renderer ", RenderingServer.get_current_rendering_method(), " ", RenderingServer.get_video_adapter_name())
	if OS.has_feature("web"):
		# Let startup (pipeline compiles, the loading overlay) finish before the first capture.
		for i in 240:
			await RenderingServer.frame_post_draw
	for s in SETTINGS:
		attrs.set("white_balance_temperature", s[0])
		attrs.set("white_balance_tint", s[1])
		for i in 6:
			await RenderingServer.frame_post_draw
		if OS.has_feature("web"):
			# Viewport readback on the web returns the previous (blank) frame; the runner screenshots the canvas.
			print("[WB] READY wb_%d_%s.png" % [s[0], str(s[1])])
			for i in 90:
				await RenderingServer.frame_post_draw
			continue
		var img := get_viewport().get_texture().get_image()
		var w := img.get_width()
		var h := img.get_height()
		var c := img.get_pixel(w / 4, h / 2)
		var p := img.get_pixel(3 * w / 4, h / 2)
		print("[WB] %d %.2f card %d %d %d patch %d %d %d" % [s[0], s[1], roundi(c.r * 255), roundi(c.g * 255), roundi(c.b * 255), roundi(p.r * 255), roundi(p.g * 255), roundi(p.b * 255)])
		if OS.has_feature("web"):
			print("[WBPNG] wb_%d_%s.png %s" % [s[0], str(s[1]), Marshalls.raw_to_base64(img.save_png_to_buffer())])
		if out_dir != "":
			img.save_png(out_dir.path_join("wb_%d_%s.png" % [s[0], str(s[1])]))
	print("[WB] DONE")
	get_tree().quit()
