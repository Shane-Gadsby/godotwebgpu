extends Node3D
## Light temperature check (work tasks 64/65): a gray card (albedo 0.5) lit by one OmniLight3D, Linear tonemapper,
## no ambient. For each case prints "[LT] <case> <tag> r g b" (linear floats from an HDR capture, mean of the
## card's central 21x21 pixels) and saves the frame as EXR. On the web the canvas is screenshotted instead.
## User args: --out=<dir> --set=<legacy|neutral64|neutral65|lum65|plu65|all>

var attrs: CameraAttributesPractical
var light: OmniLight3D
var out_dir := ""
var which := "all"

# Color.from_temperature() from a script loaded on demand, so the script also parses in the pre-change editor.
func ft(t: float, n: int) -> Color:
	return load("res://ft.gd").ft(t, n)


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out_dir = a.substr(6)
		elif a.begins_with("--set="):
			which = a.substr(6)
	if OS.has_feature("web"):
		which = "web"
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

	light = OmniLight3D.new()
	light.position = Vector3(0, 0, 1.5)
	light.omni_range = 20.0
	light.omni_attenuation = 0.0 # Constant falloff: the card is evenly lit.
	light.light_energy = 0.5
	light.light_specular = 0.0
	add_child(light)

	var card := MeshInstance3D.new()
	var q := QuadMesh.new()
	q.size = Vector2(6, 6)
	card.mesh = q
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(0.5, 0.5, 0.5).linear_to_srgb()
	m.roughness = 1.0
	m.metallic_specular = 0.0
	card.material_override = m
	add_child(card)

	print("[LT] renderer ", RenderingServer.get_current_rendering_method(), " ", RenderingServer.get_video_adapter_name(),
			" plu ", ProjectSettings.get_setting("rendering/lights_and_shadows/use_physical_light_units"))
	if OS.has_feature("web"):
		for i in 240:
			await RenderingServer.frame_post_draw

	var cases := []
	# Existing properties only (the pre-change editor has no light_temperature_enabled): bit-identical checks.
	if which in ["legacy", "all"]:
		for t in [2500.0, 4000.0, 6500.0, 9000.0]:
			for wb in [6500.0, 3200.0]:
				cases.append({"tag": "legacy_t%d_wb%d" % [t, wb], "temp": t, "wb": wb, "color": Color(1, 1, 1)})
		cases.append({"tag": "legacy_colored_t3000", "temp": 3000.0, "wb": 6500.0, "color": Color(0.9, 0.6, 0.3)})
	# Task 64: light_color from Color.from_temperature(T), white balance T.
	if which in ["neutral64", "all"]:
		for t in [2500.0, 3200.0, 4300.0, 5600.0, 6500.0, 8000.0]:
			cases.append({"tag": "n64_lum_t%d" % t, "wb": t, "color": ft(t, 0).linear_to_srgb()})
			cases.append({"tag": "n64_max_t%d" % t, "wb": t, "color": ft(t, 1).linear_to_srgb()})
	# Task 65: the opt-in, white balance T.
	if which in ["neutral65", "all", "web"]:
		for t in [2500.0, 3200.0, 4300.0, 5600.0, 6500.0, 8000.0]:
			cases.append({"tag": "n65_t%d" % t, "temp": t, "wb": t, "enabled": true, "color": Color(1, 1, 1)})
	# Task 65: constant luminance, white balance off.
	if which in ["lum65", "all", "web"]:
		for t in [2000.0, 3000.0, 4000.0, 5000.0, 6000.0, 6500.0, 7000.0, 8000.0, 9000.0, 10000.0]:
			cases.append({"tag": "l65_t%d" % t, "temp": t, "wb": 6500.0, "enabled": true, "color": Color(1, 1, 1)})
		cases.append({"tag": "l65_colored_t3000", "temp": 3000.0, "wb": 6500.0, "enabled": true, "color": Color(0.9, 0.6, 0.3)})
	# Opt-in turned on, then off again: must equal a light that never had it.
	if which in ["neutral65", "all"]:
		cases.append({"tag": "off_again_t3000", "temp": 3000.0, "wb": 6500.0, "enabled": false, "color": Color(1, 1, 1)})

	for c in cases:
		light.light_color = c.color
		if "light_temperature_enabled" in light:
			light.set("light_temperature_enabled", c.get("enabled", false))
		light.light_temperature = c.get("temp", 6500.0)
		attrs.set("white_balance_temperature", c.wb)
		for i in 8:
			await RenderingServer.frame_post_draw
		if OS.has_feature("web"):
			print("[LT] READY %s" % c.tag)
			for i in 60:
				await RenderingServer.frame_post_draw
			continue
		var img := get_viewport().get_texture().get_image()
		var w := img.get_width()
		var h := img.get_height()
		var s := Color(0, 0, 0)
		for y in range(h / 2 - 10, h / 2 + 11):
			for x in range(w / 2 - 10, w / 2 + 11):
				s += img.get_pixel(x, y)
		s /= 441.0
		var cc := light.get_correlated_color()
		print("[LT] %s %.7f %.7f %.7f fmt %d corr %.9f %.9f %.9f" % [c.tag, s.r, s.g, s.b, img.get_format(), cc.r, cc.g, cc.b])
		if out_dir != "":
			img.save_exr(out_dir.path_join(c.tag + ".exr"))
	print("[LT] DONE")
	get_tree().quit()
