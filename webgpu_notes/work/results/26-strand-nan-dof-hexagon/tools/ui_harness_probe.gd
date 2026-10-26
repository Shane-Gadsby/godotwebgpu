extends SceneTree

# Runs the real main scene natively and drives the UI through a list of steps,
# saving a screenshot of the window (and of the SubViewport) after each one.
# tools/ has a .gdignore, so Godot never imports or exports this. Needs a real
# display; --headless only gives the dummy renderer.
#
#   SCENE=res://scenes/camera_studio.tscn OUT=/some/dir EMBED=1 \
#   STEPS="hide=AcknowledgementsPopup,ApertureSlider=1.4,toggle=ZebraToggle,dump" \
#   <fork editor binary> --path . --rendering-driver vulkan --resolution 1280x720 \
#       --script "$PWD/tools/ui_harness.gd"
#
# Steps (nodes are found by name anywhere in the scene):
#   <Range>=<value>    set a slider/range (emits value_changed)
#   toggle=<Button>    flip a toggle/check button (emits toggled)
#   press=<Button>     emit pressed
#   popup=/hide=<Window>, text=<LineEdit>:<text>, select=<OptionButton>:<item>
#   call=<method on Main>, dump (print settings + UI labels), shot (no-op)
# EMBED=1 embeds popups in the main window like the web build does.

var out_dir := ""
var steps := []
var frame := 0
var next_at := 120
var pending := ""
var probe = null

func _initialize():
	out_dir = OS.get_environment("OUT")
	DirAccess.make_dir_recursive_absolute(out_dir)
	steps = Array(OS.get_environment("STEPS").split(",", false))
	if OS.get_environment("EMBED") == "1":
		root.gui_embed_subwindows = true # what the web build always does
	change_scene_to_file(OS.get_environment("SCENE"))

func n(name: String) -> Node:
	return current_scene.find_child(name, true, false)

func shot(tag: String):
	root.get_texture().get_image().save_png(out_dir + "/" + tag + "_ui.png")
	(n("SubViewport") as SubViewport).get_texture().get_image().save_png(out_dir + "/" + tag + "_vp.png")
	print("SHOT ", tag)

func do_step(s: String):
	var parts = s.split("=")
	var key = parts[0]
	var val = parts[1] if parts.size() > 1 else ""
	match key:
		"shot": pass
		"toggle": var b: BaseButton = n(val); b.button_pressed = !b.button_pressed
		"press": (n(val) as BaseButton).pressed.emit()
		"popup": (n(val) as Window).show()
		"hide": (n(val) as Window).hide()
		"text": (n(val.split(":")[0]) as LineEdit).text = val.split(":")[1]
		"call": current_scene.call(val)
		"select":
			var ob: OptionButton = n(val.split(":")[0])
			for i in ob.item_count:
				if ob.get_item_text(i) == val.split(":")[1]: ob.select(i)
		"dump":
			var st = current_scene.settings
			print("DUMP f=", st.focal_length, " sensor=", st.sensor_name, " N=", st.aperture, " iso=", st.iso, " t=", st.shutter_speed, " wb=", st.white_balance, " key_int=", st.key_intensity,
				" | ui: ", n("ApertureNum").text, " ", n("ISONum").text, " ", n("ShutterNum").text, " ", n("Camera_WBNum").text, " ", n("KeyIntNum").text, " ", n("LensNum").text,
				" APSC=", n("APSC").button_pressed, " FF=", n("FullFrame").button_pressed, " keyE=", n("KeyLight").light_energy, " brightness=", n("WorldEnvironment").environment.adjustment_brightness,
				" presets=", n("PresetMenu").item_count, " info=", n("InfoLabel").text.replace("\n", " / "))
		"nanprobe":
			var cam: Camera3D = (n("SubViewport") as SubViewport).get_camera_3d()
			if cam.compositor == null:
				cam.compositor = Compositor.new()
			probe = load("res://tools/nan_probe.gd").new()
			cam.compositor.compositor_effects = [probe]
		"meshes":
			for mi in current_scene.find_children("*", "MeshInstance3D", true, false):
				var m = mi.get_active_material(0)
				var sn = (m.shader.resource_name if m is ShaderMaterial and m.shader else str(m))
				print("MESH ", mi.name, " vis=", mi.is_visible_in_tree(), " mat=", sn, " surfaces=", mi.mesh.get_surface_count() if mi.mesh else 0)
		"lashprobe":
			# Replace the strand shader's fragment on meshes matching <substr>:<file> with the fragment in tools/<file>.
			var sub = val.split(":")[0]
			var frag = FileAccess.get_file_as_string("res://tools/" + val.split(":")[1])
			for mi in current_scene.find_children("*" + sub + "*", "MeshInstance3D", true, false):
				var m: ShaderMaterial = mi.get_active_material(0)
				var code: String = m.shader.code
				var i = code.find("void fragment()")
				var head = code.substr(0, i)
				var rm = head.find("render_mode")
				var rme = head.find(";", rm)
				var mode = frag.split("\n")[0]
				head = head.substr(0, rm) + mode + head.substr(rme + 1)
				var sh = Shader.new()
				sh.code = head + frag.substr(frag.find("\n") + 1)
				var m2: ShaderMaterial = m.duplicate()
				m2.shader = sh
				mi.material_override = m2
				print("PROBED ", mi.name)
		"meshstat":
			for mi in current_scene.find_children("*" + val + "*", "MeshInstance3D", true, false):
				var a = mi.mesh.surface_get_arrays(0)
				var uv: PackedVector2Array = a[Mesh.ARRAY_TEX_UV]
				var uv2: PackedVector2Array = a[Mesh.ARRAY_TEX_UV2]
				var zero := 0
				var zero_tip := 0
				var nonfinite := 0
				var minpos := 1e9
				for i in uv2.size():
					if not is_finite(uv2[i].x): nonfinite += 1
					if uv2[i].x == 0.0:
						zero += 1
						if uv[i].y > 0.999: zero_tip += 1
					elif uv2[i].x < minpos: minpos = uv2[i].x
				var mat: ShaderMaterial = mi.get_active_material(0)
				print("MESHSTAT ", mi.name, " verts=", uv2.size(), " uv2x==0: ", zero, " (at tip uv.y>0.999: ", zero_tip, ") nonfinite=", nonfinite, " min nonzero uv2x=", minpos,
					" max_width=", mat.get_shader_parameter("max_width"), " tip_scale=", mat.get_shader_parameter("tip_scale"), " coverage_exponent=", mat.get_shader_parameter("coverage_exponent"), " min_pixel_width=", mat.get_shader_parameter("min_pixel_width"))
		"region": probe.region = Vector2i(int(val.split(":")[0]), int(val.split(":")[1]))
		"report": RenderingServer.call_on_render_thread(func(): print(probe.report()))
		"set": var kv = val.split(":"); n(kv[0]).set(kv[1], str_to_var(kv[2]))
		"eval": print("EVAL ", val, " = ", n(val.split(".")[0]).get(val.split(".")[1]))
		_: (n(key) as Range).value = float(val)
	print("STEP ", s)

func _process(_d):
	frame += 1
	if frame < next_at:
		return false
	if pending != "":
		shot("%02d_%s" % [frame, pending.replace("=", "-").replace("/", "_")])
		pending = ""
	if steps.is_empty():
		return true
	var s: String = steps.pop_front()
	do_step(s)
	pending = s
	next_at = frame + 40
	return false
