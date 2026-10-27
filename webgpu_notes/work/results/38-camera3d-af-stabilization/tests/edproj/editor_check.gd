@tool
extends Node
# Runs inside the editor: after 3 s, reads the edited scene's camera (autofocus and shake must be running in the editor),
# saves the scene (the saved focus distance must be the user's 10 m, not autofocus's 2 m), then quits.
var t := 0.0
var done := false
var shakes := {}

func _process(delta: float) -> void:
	if not Engine.is_editor_hint() or done:
		return
	t += delta
	var root = EditorInterface.get_edited_scene_root()
	if root == null:
		if t > 20.0:
			_finish("FAIL no edited scene")
		return
	var cam: Camera3D = root.get_node("Camera3D")
	shakes[str(cam.get_shake_offset())] = true
	if t < 3.0:
		return
	done = true
	var lines := []
	var fd: float = cam.attributes.frustum_focus_distance
	lines.append(("PASS" if absf(fd - 2.0) < 0.02 else "FAIL") + " editor: AF-C focused the edited camera at %.4f m (target 2)" % fd)
	lines.append(("PASS" if shakes.size() > 20 else "FAIL") + " editor: shake offset changed over frames (%d distinct values)" % shakes.size())
	lines.append(("PASS" if cam.transform == Transform3D(Basis(), Vector3()) else "FAIL") + " editor: node transform untouched")
	EditorInterface.save_scene()
	var text := FileAccess.get_file_as_string("res://main.tscn")
	var saved_ok := text.find("frustum_focus_distance") == -1 or text.find("frustum_focus_distance = 10.0") != -1
	lines.append(("PASS" if saved_ok else "FAIL") + " editor: saved scene keeps the user's focus distance (10 m)")
	lines.append(("PASS" if absf(cam.attributes.frustum_focus_distance - 2.0) < 0.02 else "FAIL") + " editor: autofocus value re-applied after save (%.4f)" % cam.attributes.frustum_focus_distance)
	_finish("\n".join(lines) + "\n--- saved main.tscn ---\n" + text)

func _finish(msg: String) -> void:
	var f := FileAccess.open(OS.get_environment("EDCHECK_OUT"), FileAccess.WRITE)
	f.store_string(msg)
	f.close()
	get_tree().quit()
