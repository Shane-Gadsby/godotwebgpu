extends Node
# Web entry: configuration from the page URL (?method=gtao&quality=2...), defaults otherwise.
func _ready() -> void:
	var cfg := {}
	if OS.has_feature("web"):
		var q = JavaScriptBridge.eval("window.location.search", true)
		if typeof(q) == TYPE_STRING and q.length() > 1:
			for kv in q.substr(1).split("&"):
				var p = kv.split("=", true, 1)
				if p.size() == 2:
					cfg[p[0]] = p[1]
	var scene: Node3D = load("res://gtao_scene.gd").new()
	add_child(scene)
	scene.setup(cfg)
