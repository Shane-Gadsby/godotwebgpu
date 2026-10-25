extends Node
# Task 18 capture autoload, added to a SCRATCH copy of the cameraSim project only (never the user's project).
# Steps through fixed views of the main scene, each held for the same number of frames on both backends, and
# captures the root viewport after SETTLE frames (TAA history and the hair simulation settle by then).
#   native: saves <T18_OUT>/<view>.png (env var T18_OUT); quits after the last view.
#   web:    hands the PNG to the page with JavaScriptBridge.download_buffer (Playwright saves it).
# Readbacks on WebGPU are asynchronous, so get_image() is retried on later frames until it returns data;
# the scene is static once settled, so a capture one or two frames later shows the same thing.
# Each view's capture frame number is printed, so the alignment can be checked from both logs.

const FIRST_SETTLE := 360  # first view: shader compiles + TAA + hair settle
const SETTLE := 240

var views := ["default", "face_200mm", "dof_f1_4", "hair_side_200mm", "face_200mm_lights"]
var view_i := -1
var wait := 0
var capturing := false
var tries := 0
var main: Node3D
var is_web := OS.has_feature("web")
var finished := false


func _ready() -> void:
	Engine.max_fps = 60
	print("[t18] ready, web=", is_web, " frame=", Engine.get_process_frames())


func _head_position() -> Vector3:
	var sk: Skeleton3D = main.get_node_or_null("ModelAva/Ava/Skeleton3D")
	if sk == null:
		return Vector3(0, 0.25, 0)
	for name in ["head", "FACIAL_C_FacialRoot", "neck_02"]:
		var b := sk.find_bone(name)
		if b >= 0:
			return sk.global_transform * sk.get_bone_global_pose(b).origin
	return Vector3(0, 0.25, 0)


func _apply(view: String) -> void:
	var ec = main.get_node("ExposureController")
	var head := _head_position()
	print("[t18] head=", head, " subject=", main.subject.global_position)
	match view:
		"default":
			pass
		"face_200mm":
			# The 200mm button at face height (camera height slider), default distance.
			main.camera_height = head.y + 0.06 - main.subject.global_position.y
			main.update_camera_position()
			main.set_lens(200)
		"dof_f1_4":
			# Back to the default framing, aperture slider at its minimum (f/1.4, which the app stores in an int: f/1)
			# for the shallowest depth of field the UI offers; shutter at 1/4000 so the exposure stays close to the default.
			main.camera_height = 0.4
			main.update_camera_position()
			main.set_lens(50)
			ec.aperture_slider.value = 1.4
			ec.shutter_slider.value = 12
		"hair_side_200mm":
			# Not reachable from the UI (no camera-angle slider): the camera orbits 50 degrees to the side, 200mm at face
			# height. main.update_camera_position() aims correctly only at its default 90 degrees, so aim with look_at().
			ec.aperture_slider.value = 8
			ec.shutter_slider.value = 7
			main.camera_angle = 40
			main.camera_height = head.y + 0.06 - main.subject.global_position.y
			main.update_camera_position()
			main.camera.look_at(Vector3(head.x, main.camera.global_position.y, head.z))
			main.set_lens(200)
		"face_200mm_lights":
			# Not reachable from the UI (the scene ships its key/fill/rim spots hidden): three-point lighting on.
			main.camera_angle = 90
			main.update_camera_position()
			# Shutter 1/500: at the default exposure the 5-energy spots blow the frame out to white.
			ec.shutter_slider.value = 9
			for l in ["KeyLight", "FillLight", "RimLight"]:
				main.get_node(l).visible = true
			main.update_lights()


func _process(_delta: float) -> void:
	if finished:
		return
	if main == null:
		main = get_tree().current_scene
		if main == null:
			return
		# The FPS counter label differs by design (frame pacing), hide it so it does not count in the difference.
		main.get_node("CanvasLayer/Label").visible = false
		view_i = 0
		wait = FIRST_SETTLE
		_apply(views[0])
		return
	if capturing:
		var img := get_viewport().get_texture().get_image()
		tries += 1
		if img == null or img.is_empty():
			if tries > 30:
				print("[t18] capture FAILED view=", views[view_i])
				_next()
			return
		var frame := Engine.get_process_frames()
		print("[t18] captured view=", views[view_i], " frame=", frame, " tries=", tries, " size=", img.get_size())
		if is_web:
			JavaScriptBridge.download_buffer(img.save_png_to_buffer(), views[view_i] + ".png", "image/png")
		else:
			img.save_png(OS.get_environment("T18_OUT").path_join(views[view_i] + ".png"))
		_next()
		return
	wait -= 1
	if wait <= 0:
		capturing = true
		tries = 0
		print("[t18] capture view=", views[view_i], " at frame=", Engine.get_process_frames())


func _next() -> void:
	capturing = false
	view_i += 1
	if view_i >= views.size():
		finished = true
		print("[t18] done")
		if not is_web:
			get_tree().quit()
		return
	wait = SETTLE
	_apply(views[view_i])
