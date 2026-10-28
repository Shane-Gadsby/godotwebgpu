extends Control

var signals := 0

func _ready() -> void:
	get_tree().root.size_changed.connect(_on_size_changed)
	$Button.pressed.connect(func(): DisplayServer.window_set_size(Vector2i(800, 600)))
	_out("ready")

func _on_size_changed() -> void:
	signals += 1
	_out("size_changed")

func _process(_delta: float) -> void:
	var cmd: String = str(JavaScriptBridge.eval("window.__cmd || ''"))
	if cmd.is_empty():
		return
	JavaScriptBridge.eval("window.__cmd = ''")
	var p := cmd.split(" ")
	if p[0] == "set":
		DisplayServer.window_set_size(Vector2i(int(p[1]), int(p[2])))
		_out("after_set")
	elif p[0] == "wset":
		get_window().size = Vector2i(int(p[1]), int(p[2]))
		_out("after_wset")
	elif p[0] == "report":
		_out("report")

func _out(tag: String) -> void:
	var d := {
		"tag": tag,
		"signals": signals,
		"window": [get_window().size.x, get_window().size.y],
		"visible_rect": [get_viewport().get_visible_rect().size.x, get_viewport().get_visible_rect().size.y],
		"ds": [DisplayServer.window_get_size().x, DisplayServer.window_get_size().y],
		"control": [size.x, size.y],
		"corner_label": [$Corner.global_position.x + $Corner.size.x, $Corner.global_position.y + $Corner.size.y],
	}
	JavaScriptBridge.eval("(window.__log = window.__log || []).push(%s)" % JSON.stringify(d))
	$Info.text = JSON.stringify(d)
