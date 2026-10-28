extends Node3D
# Web entry: options come from the page URL's query string (?scene=ramp&zebra=95...), as verify.gd's arguments do.
# After 12 frames the viewport's own texture is read back and left in window.__shot (base64 PNG), so the capture
# compares the render target's values rather than the browser's composited canvas.

var frames := 0
var sv: SubViewport
var done := false

func _ready() -> void:
	var opts := {}
	if OS.has_feature("web"):
		var q: String = JavaScriptBridge.eval("location.search.substring(1)", true)
		for a in q.split("&", false):
			var kv := a.split("=", true, 1)
			opts[kv[0]] = kv[1].uri_decode() if kv.size() > 1 else "1"
	print("EDITOR_HINT=", Engine.is_editor_hint(), " opts=", opts)
	# Rendered into a 1280x720 SubViewport (shown through a container) because the root viewport of a web export
	# cannot be read back.
	var container := SubViewportContainer.new()
	sv = SubViewport.new()
	sv.size = Vector2i(1280, 720)
	sv.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	container.add_child(sv)
	add_child(container)
	load("res://scene_builder.gd").build(sv, opts)

func _process(_delta: float) -> void:
	frames += 1
	if frames >= 12 and not done and OS.has_feature("web"):
		# WebGPU reads textures back asynchronously: the first requests return nothing until the copy lands.
		var img := sv.get_texture().get_image()
		if img == null:
			return
		done = true
		var b64 := Marshalls.raw_to_base64(img.save_png_to_buffer())
		JavaScriptBridge.eval("window.__shot = '%s';" % b64, true)
