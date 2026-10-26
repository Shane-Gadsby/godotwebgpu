extends Node
# Web side of the 42-position sweep (scratch copy only). With #sweep in the URL: distance 4.70-4.90 m at
# height 1.64 m, then height 1.55-1.75 m at 4.80 m. Per position prints
#   SWEEP d=<d> h=<h> nonfinite_preDOF=<n> black_face_px=<n>
# nonfinite_preDOF from a read-only CompositorEffect after the transparent pass (before DOF), black_face_px
# from the SubViewport image in the same crop the native sweep uses.

var probe


func _ready() -> void:
	if not OS.has_feature("web"):
		return
	var hash: String = str(JavaScriptBridge.eval("location.hash"))
	if not hash.begins_with("#sweep"):
		return
	_run()


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _n(name: String) -> Node:
	return get_tree().current_scene.find_child(name, true, false)


func _black(vp: SubViewport) -> int:
	var img := vp.get_texture().get_image()
	img.convert(Image.FORMAT_RGB8)
	var n := 0
	for y in range(220, 460):
		for x in range(500, 760):
			var c := img.get_pixel(x, y)
			if c.r8 + c.g8 + c.b8 < 6:
				n += 1
	return n


func _at(d: float, h: float) -> void:
	(_n("DistanceSlider") as Range).value = d
	(_n("HeightSlider") as Range).value = h
	await _frames(40)
	var nonfinite: int = probe.count
	# A web texture readback can come back all zero (asynchronous, TASKS.md §4.28); an all-black crop is
	# that, never a picture (the hexagon is ~4700 of its 62400 pixels), so read again.
	var black := _black(_n("SubViewport"))
	var rereads := 0
	while black == 62400 and rereads < 10:
		await _frames(5)
		black = _black(_n("SubViewport"))
		rereads += 1
	print("SWEEP d=%.2f h=%.2f nonfinite_preDOF=%d black_face_px=%d rereads=%d" % [d, h, nonfinite, black, rereads])


func _run() -> void:
	await _frames(120)
	var popup := _n("AcknowledgementsPopup") as Window
	if popup:
		popup.hide()
	(_n("DistanceSlider") as Range).step = 0.01
	(_n("HeightSlider") as Range).step = 0.01
	var cam: Camera3D = (_n("SubViewport") as SubViewport).get_camera_3d()
	probe = load("res://sweep/nan_probe.gd").new()
	if cam.compositor == null:
		cam.compositor = Compositor.new()
	cam.compositor.compositor_effects = [probe]
	# The first frames on the web are black while pipelines compile: wait for a real picture.
	(_n("DistanceSlider") as Range).value = 4.70
	for tries in 100:
		await _frames(30)
		if _black(_n("SubViewport")) < 1000:
			break
	print("SWEEP ready after %d frames" % Engine.get_frames_drawn())
	for i in 21:
		await _at(4.70 + i * 0.01, 1.64)
	for i in 21:
		await _at(4.80, 1.55 + i * 0.01)
	print("SWEEP DONE")
