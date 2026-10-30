extends SceneTree
# Close-up of Ava's eyes: VARIANT picks an experiment, then FRAMES consecutive crops are saved.
var frame := 0
var out := ""
var variant := ""
var main
var cam: Camera3D
var vp: SubViewport
var crops := 0

func _initialize():
	out = OS.get_environment("OUT")
	variant = OS.get_environment("VARIANT")
	DirAccess.make_dir_recursive_absolute(out)
	root.gui_embed_subwindows = true
	change_scene_to_file("res://scenes/camera_studio.tscn")

func setup():
	main = current_scene
	cam = main.camera
	vp = main.viewport
	seed(1)
	vp.size = Vector2i(1920, 1080)
	var st = main.settings
	st.focal_length = 200.0
	st.camera_distance = 2.5
	main.apply_settings()
	main.find_child("AcknowledgementsPopup", true, false).hide()
	for v in variant.split("+", false):
		match v:
			"bilinear": vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR; vp.scaling_3d_scale = 1.0
			"notaa": vp.use_taa = false
			"taa": vp.use_taa = true
			"nofxaa": vp.screen_space_aa = Viewport.SCREEN_SPACE_AA_DISABLED
			"pause": main.idle_animation.pause()
			"noca": cam.attributes.chromatic_aberration_enabled = false
			"nograin": cam.attributes.grain_enabled = false
			"nodof": cam.attributes.dof_physical_blur_size = false
			"nossr": main.find_child("WorldEnvironment", true, false).environment.ssr_enabled = false
			"noglow": main.find_child("WorldEnvironment", true, false).environment.glow_enabled = false
			"nokeyshadow": main.key_light.shadow_enabled = false
			"nospec": main.key_light.light_specular = 0.0
			"nosunspec": main.find_child("DirectionalLight3D", true, false).light_specular = 0.0
			_:
				if v.begins_with("wet"): # wet<roughness x100>: the eye shell / lacrimal edge layer
					for mi in main.find_child("Subject", true, false).find_children("*", "MeshInstance3D", true, false):
						for i in mi.get_surface_override_material_count():
							var m = mi.get_active_material(i)
							if m is ShaderMaterial and m.shader and m.shader.code.contains("ALPHA = 0.0;"):
								print("wet surface ", mi.name, " #", i, " roughness was ", m.get_shader_parameter("roughness"))
								m.set_shader_parameter("roughness", float(v.substr(3)) / 100.0)
	print("variant ", variant, " scaling ", vp.scaling_3d_mode, " taa ", vp.use_taa, " aa ", vp.screen_space_aa)

func _process(_d):
	frame += 1
	if frame == 60:
		setup()
	if frame >= 200 and crops < 12:
		var img: Image = vp.get_texture().get_image()
		var head: Vector3 = main.head_target.global_position
		var p := cam.unproject_position(head) * Vector2(vp.size) / Vector2(vp.get_visible_rect().size)
		if crops == 0: print("head at ", p, " vp ", vp.size)
		img.get_region(Rect2i(int(p.x) - 160, int(p.y) - 110, 320, 160)).save_png(out + "/f%02d.png" % crops)
		if crops == 0: img.save_png(out + "/full.png")
		crops += 1
	return crops >= 12
