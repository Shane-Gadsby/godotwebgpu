@tool
extends Node
# Task 39, inside a windowed editor: the edited scene's camera with Visible in Editor ticked shakes, the unticked one
# holds still (offset identity, camera transform = node transform). In a game run (the same scene run with the
# project, not the editor) both shake. Writes PASS/FAIL lines to $EDCHECK_OUT, then quits.
var t := 0.0
var done := false
var seen := {"Ticked": {}, "Unticked": {}}
var xf := {"Ticked": {}, "Unticked": {}}

func _process(delta: float) -> void:
	if done:
		return
	t += delta
	var root: Node = EditorInterface.get_edited_scene_root() if Engine.is_editor_hint() else get_tree().current_scene
	if root == null:
		if t > 20.0:
			_finish("FAIL no scene")
		return
	for n in seen:
		var cam: Camera3D = root.get_node(n)
		seen[n][str(cam.get_shake_offset())] = true
		xf[n][str(cam.get_camera_transform())] = true
	if t < 3.0:
		return
	done = true
	var where := "editor" if Engine.is_editor_hint() else "game"
	var lines := []
	var ticked_ok: bool = seen["Ticked"].size() > 20 and xf["Ticked"].size() > 20
	lines.append(("PASS" if ticked_ok else "FAIL") + " %s: ticked camera shakes (%d distinct offsets, %d camera transforms)" % [where, seen["Ticked"].size(), xf["Ticked"].size()])
	var un_moves: bool = seen["Unticked"].size() > 20 and xf["Unticked"].size() > 20
	var un_still: bool = seen["Unticked"].size() == 1 and xf["Unticked"].size() == 1 and seen["Unticked"].has(str(Transform3D()))
	var un_ok := un_still if Engine.is_editor_hint() else un_moves
	lines.append(("PASS" if un_ok else "FAIL") + " %s: unticked camera %s (%d distinct offsets, %d camera transforms)" % [where, "holds still" if Engine.is_editor_hint() else "shakes", seen["Unticked"].size(), xf["Unticked"].size()])
	_finish("\n".join(lines))

func _finish(msg: String) -> void:
	var f := FileAccess.open(OS.get_environment("EDCHECK_OUT"), FileAccess.WRITE)
	f.store_string(msg + "\n")
	f.close()
	get_tree().quit()
