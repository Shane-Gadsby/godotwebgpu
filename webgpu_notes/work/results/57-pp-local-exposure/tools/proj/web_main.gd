extends Node3D
# Web entry for task 57: ?le=1&hc=0.6&...&hdr=1 builds le_scene.gd's scene with those options (the same keys as
# le_test.gd). After 30 frames it sets document.title to "ready"; with hdr=1 it first stores the HDR 2D viewport's
# raw RGBA half floats in window.__le_hdr (base64) and its size in window.__le_size.

var frames := 0
var hdr := false

func _ready() -> void:
	var opts := {}
	var query: String = str(JavaScriptBridge.eval("location.search")).trim_prefix("?")
	for a in query.split("&", false):
		var kv := a.split("=", true, 1)
		if kv.size() != 2:
			continue
		var v: Variant = kv[1]
		if kv[0] in ["le", "ae", "glow", "phys", "hdr"]:
			v = kv[1] == "1"
		elif kv[1].is_valid_float():
			v = float(kv[1])
		opts[kv[0]] = v
	hdr = opts.get("hdr", false)
	get_viewport().use_hdr_2d = hdr
	var scene = preload("res://le_scene.gd").new()
	scene.build(self, opts)

func _process(_delta: float) -> void:
	frames += 1
	if frames == 30:
		if hdr:
			var img := get_viewport().get_texture().get_image()
			JavaScriptBridge.eval("window.__le_size = [%d, %d, %d]" % [img.get_width(), img.get_height(), img.get_format()])
			JavaScriptBridge.eval("window.__le_hdr = '" + Marshalls.raw_to_base64(img.get_data()) + "'")
		JavaScriptBridge.eval("document.title = 'ready'")
