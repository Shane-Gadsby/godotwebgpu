extends Node3D

# Task 19 probe: reports what the web DisplayServer says about keep-screen-on and orientation,
# and toggles fullscreen on a tap (fullscreen is where the browser allows the orientation lock).

const FEATURE_KEEP_SCREEN_ON := 17 # Not bound in DisplayServer's Feature enum.

@onready var label: Label = $UI/Label
@onready var cube: MeshInstance3D = $Cube
var elapsed := 0.0
var info := {}


func _ready() -> void:
	info = {
		"keep_screen_on_feature": DisplayServer.has_feature(FEATURE_KEEP_SCREEN_ON),
		"orientation_feature": DisplayServer.has_feature(DisplayServer.FEATURE_ORIENTATION),
		"kept_on": DisplayServer.screen_is_kept_on(),
		"orientation": DisplayServer.screen_get_orientation(),
		"orientation_setting": ProjectSettings.get_setting("display/window/handheld/orientation"),
		"keep_screen_on_setting": ProjectSettings.get_setting("display/window/energy_saving/keep_screen_on"),
	}
	print("WAKE_ORIENT_PROBE ", JSON.stringify(info))
	if OS.has_feature("web"):
		JavaScriptBridge.eval("window.__wake_orient_probe = %s;" % JSON.stringify(info), true)


func _process(delta: float) -> void:
	elapsed += delta
	cube.rotate_y(delta)
	var size := DisplayServer.window_get_size()
	var lines := PackedStringArray()
	for k in info:
		lines.append("%s: %s" % [k, info[k]])
	label.text = "%s\nwindow %dx%d %s\nfullscreen %s\nrunning %d s\ntap to toggle fullscreen" % [
		"\n".join(lines), size.x, size.y, "portrait" if size.y > size.x else "landscape",
		DisplayServer.window_get_mode() == DisplayServer.WINDOW_MODE_FULLSCREEN, int(elapsed)]


func _unhandled_input(event: InputEvent) -> void:
	var tap: bool = (event is InputEventScreenTouch and event.pressed) or (event is InputEventMouseButton and event.pressed)
	if tap:
		if DisplayServer.window_get_mode() == DisplayServer.WINDOW_MODE_FULLSCREEN:
			DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
		else:
			DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_FULLSCREEN)
