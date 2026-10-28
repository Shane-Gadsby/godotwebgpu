extends SceneTree
# Usage: godot --path proj --resolution WxH -s verify.gd -- out=<png> scene=ramp|step|checker [options]
# sub=WxH renders into a SubViewport of that size instead of the root (as the Camera3D inspector preview does), for
# editor-hint runs, where the root window is covered by the editor.

var frames := 0
var opts := {}
var target: Viewport

func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		opts[kv[0]] = kv[1] if kv.size() > 1 else "1"
	print("EDITOR_HINT=", Engine.is_editor_hint())
	target = root
	if opts.has("sub"):
		var sv := SubViewport.new()
		var wh: PackedStringArray = opts["sub"].split("x")
		sv.size = Vector2i(int(wh[0]), int(wh[1]))
		sv.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		sv.own_world_3d = true
		root.add_child(sv)
		target = sv
	var builder = load("res://scene_builder.gd")
	builder.build(target, opts)

func _process(_delta: float) -> bool:
	frames += 1
	if frames == int(opts.get("frames", 12)):
		var img := target.get_texture().get_image()
		img.save_png(opts["out"])
		print("SAVED ", opts["out"], " ", img.get_size())
		return true
	return false
