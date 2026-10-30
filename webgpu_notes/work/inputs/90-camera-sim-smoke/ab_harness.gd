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
#   hover=<Control>    move the mouse over a control's centre
#   expr=<expression>  print a GDScript Expression evaluated on Main (no "=" or ",")
#   call=<method on Main>, dump (print settings + UI labels), shot (no-op)
# EMBED=1 embeds popups in the main window like the web build does.

var out_dir := ""
var steps := []
var frame := 0
var next_at := 120
var pending := ""

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
		"capture":
			var img: Image = await current_scene.capture_camera_image()
			if img == null:
				print("CAPTURE null")
			else:
				img.save_png(out_dir + "/capture_%d.png" % frame)
				print("CAPTURE ", img.get_size(), " ", img.get_format())
		"shotdl":
			var img2: Image = await root.get_texture().get_image_awaitable()
			print("SCREENSHOT ", img2.get_size() if img2 else null)
		"hist":
			var m = current_scene.get_tree().get_first_node_in_group("monitoring")
			var total = 0
			for v in m.histogram_bins: total += v
			print("HIST total=", total, " hi=", m.highlights_clipping, " lo=", m.shadows_clipping, " bins=", m.histogram_bins)
		"toggle": var b: BaseButton = n(val); b.button_pressed = !b.button_pressed
		"press": (n(val) as BaseButton).pressed.emit()
		"hover":
			var motion := InputEventMouseMotion.new()
			motion.position = (n(val) as Control).get_global_rect().get_center()
			motion.global_position = motion.position
			root.push_input(motion)
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
		"eval": print("EVAL ", val, " = ", n(val.split(".")[0]).get(val.split(".")[1]))
		"expr":
			var e := Expression.new()
			e.parse(val)
			print("EXPR ", val, " = ", e.execute([], current_scene))
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
