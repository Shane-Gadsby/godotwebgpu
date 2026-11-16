extends SceneTree
# Key light as a SpotLight3D (VARIANT=spot) or an AreaLight3D softbox (VARIANT=area, AREA=w,h in m, ENERGY=x).
# Saves 12 consecutive eye crops, a face crop and the full SubViewport at a close-up and at the default framing,
# and prints the GPU render time of the SubViewport and the mean luminance of a cheek patch.
var frame := 0
var out := ""
var variant := ""
var main
var cam: Camera3D
var vp: SubViewport
var crops := 0
var gpu := []
var area: AreaLight3D

func _initialize():
	out = OS.get_environment("OUT")
	variant = OS.get_environment("VARIANT")
	DirAccess.make_dir_recursive_absolute(out)
	root.gui_embed_subwindows = true
	change_scene_to_file("res://scenes/camera_studio.tscn")

func framing(focal: float, dist: float):
	main.settings.focal_length = focal
	main.settings.camera_distance = dist
	main.apply_settings()

func setup():
	main = current_scene
	cam = main.camera
	vp = main.viewport
	seed(1)
	vp.size = Vector2i(1920, 1080)
	main.find_child("AcknowledgementsPopup", true, false).hide()
	RenderingServer.viewport_set_measure_render_time(vp.get_viewport_rid(), true)
	if variant == "area":
		var key: SpotLight3D = main.key_light
		area = AreaLight3D.new()
		var s = OS.get_environment("AREA").split(",")
		area.area_size = Vector2(float(s[0]), float(s[1]))
		area.light_energy = float(OS.get_environment("ENERGY"))
		area.area_range = 10.0
		area.light_temperature_enabled = true
		area.light_temperature = key.light_temperature
		area.shadow_enabled = OS.get_environment("NOSHADOW") != "1"
		if OS.get_environment("DEFAULTBIAS") != "1":
			area.shadow_bias = key.shadow_bias
			area.shadow_normal_bias = key.shadow_normal_bias
		print("area bias ", area.shadow_bias, " normal bias ", area.shadow_normal_bias)
		area.shadow_blur = key.shadow_blur
		main.add_child(area)
		area.global_transform = key.global_transform
		var hybrid = OS.get_environment("HYBRID")
		if hybrid == "":
			key.visible = false
		else:
			# Spot keeps lighting and shadows; the area light only lights the eyes' wet layer (layer 20).
			const EYE_LAYER := 1 << 19
			area.shadow_enabled = false
			area.light_cull_mask = EYE_LAYER
			key.light_cull_mask &= ~EYE_LAYER
			cam.cull_mask |= EYE_LAYER
			for mi in main.find_child("Subject", true, false).find_children("*", "MeshInstance3D", true, false):
				if mi.name.begins_with("eyeshell") or (mi.name.begins_with("eyeEdge") and hybrid == "edge"):
					mi.layers = EYE_LAYER
					print("eye layer: ", mi.name)
				elif mi.name.begins_with("eyeEdge") and hybrid == "noedge":
					mi.layers = 1 << 18 # lit by neither key light
					cam.cull_mask |= 1 << 18
					print("unlit by key: ", mi.name)
		print("area size ", area.area_size, " energy ", area.light_energy, " normalize ", area.area_normalize_energy, " attenuation ", area.get_param(Light3D.PARAM_ATTENUATION))
	framing(200.0, 2.5)

func head_px() -> Vector2:
	return cam.unproject_position(main.head_target.global_position) * Vector2(vp.size) / Vector2(vp.get_visible_rect().size)

func _process(_d):
	frame += 1
	if frame == 60:
		setup()
	if frame >= 200 and frame < 212:
		var img: Image = vp.get_texture().get_image()
		var p := head_px()
		img.get_region(Rect2i(int(p.x) - 160, int(p.y) - 110, 320, 160)).save_png(out + "/f%02d.png" % (frame - 200))
		if frame == 200:
			img.get_region(Rect2i(int(p.x) - 330, int(p.y) - 380, 660, 820)).save_png(out + "/face.png")
			img.save_png(out + "/full_closeup.png")
			# cheek patch (her left cheek, lit side) mean luminance, to match exposure between variants
			var sum := 0.0
			for y in range(int(p.y) + 40, int(p.y) + 80):
				for x in range(int(p.x) + 40, int(p.x) + 90):
					sum += img.get_pixel(x, y).get_luminance()
			print("cheek luminance ", sum / 2000.0)
	if frame >= 212 and frame < 332:
		gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(vp.get_viewport_rid()))
	if frame == 332:
		gpu.sort()
		print("gpu ms median ", gpu[gpu.size() / 2])
		framing(50.0, 5.0)
	if frame == 400:
		vp.get_texture().get_image().save_png(out + "/full_default.png")
		return true
	return false
