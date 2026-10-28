extends Node3D
# Web entry for task 47: ?case=d1s1&layout=target builds panini_scene.gd's scene and sets the title to "ready".

var frames := 0

func _ready() -> void:
	var args := {"case": "off", "layout": "target"}
	var query: String = str(JavaScriptBridge.eval("location.search")).trim_prefix("?")
	for a in query.split("&", false):
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			args[kv[0]] = kv[1]
	var scene = preload("res://panini_scene.gd").new()
	scene.build(self, args["case"], args["layout"], Vector2(get_viewport().size))

func _process(_delta: float) -> void:
	frames += 1
	if frames == 30:
		JavaScriptBridge.eval("document.title = 'ready'")
