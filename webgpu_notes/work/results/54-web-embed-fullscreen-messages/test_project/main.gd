extends Control

var signals := 0
var mode_changes := 0
var last_mode := -1

func _ready() -> void:
	get_tree().root.size_changed.connect(_on_size_changed)
	$Fullscreen.pressed.connect(func(): DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_FULLSCREEN); _out("pressed_fullscreen"))
	$Windowed.pressed.connect(func(): DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED); _out("pressed_windowed"))
	last_mode = DisplayServer.window_get_mode()
	_out("ready")

func _on_size_changed() -> void:
	signals += 1
	_out("size_changed")

func _process(_delta: float) -> void:
	var mode := DisplayServer.window_get_mode()
	if mode != last_mode:
		last_mode = mode
		mode_changes += 1
		_out("mode_changed")
	var cmd: String = str(JavaScriptBridge.eval("window.__cmd || ''"))
	if cmd.is_empty():
		return
	JavaScriptBridge.eval("window.__cmd = ''")
	if cmd == "windowed":
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
		_out("after_windowed")
	elif cmd == "report":
		_out("report")

func _out(tag: String) -> void:
	var d := {
		"tag": tag,
		"t": Time.get_ticks_msec(),
		"mode": DisplayServer.window_get_mode(),
		"mode_changes": mode_changes,
		"signals": signals,
		"window": [get_window().size.x, get_window().size.y],
		"visible_rect": [get_viewport().get_visible_rect().size.x, get_viewport().get_visible_rect().size.y],
		"ds": [DisplayServer.window_get_size().x, DisplayServer.window_get_size().y],
		"corner_label": [$Corner.global_position.x + $Corner.size.x, $Corner.global_position.y + $Corner.size.y],
	}
	JavaScriptBridge.eval("(window.__log = window.__log || []).push(%s)" % JSON.stringify(d))
	$Info.text = JSON.stringify(d)
