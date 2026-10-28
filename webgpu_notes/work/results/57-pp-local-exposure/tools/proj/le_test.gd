# le_test.gd: native runner for task 57 (local exposure). Usage:
#   godot --path proj --rendering-method <m> --resolution WxH -s le_test.gd -- <name> <out_dir> [key=value ...]
# Keys as le_scene.gd's opts, plus hdr=1 (capture the HDR 2D viewport: raw RGBA half floats in <name>.f16 with
# <name>.json) and frames=N (default 12). Always writes <name>.png.
extends SceneTree

var frames := 0
var wait := 12
var name := ""
var out_dir := ""
var hdr := false

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	name = args[0]
	out_dir = args[1]
	var opts := {}
	for i in range(2, args.size()):
		var kv: PackedStringArray = args[i].split("=")
		var v: Variant = kv[1]
		if kv[1] == "1" and kv[0] in ["le", "ae", "glow", "phys", "hdr"]:
			v = true
		elif kv[1] == "0" and kv[0] in ["le", "ae", "glow", "phys", "hdr"]:
			v = false
		elif kv[1].is_valid_float():
			v = float(kv[1])
		opts[kv[0]] = v
	hdr = opts.get("hdr", false)
	wait = int(opts.get("frames", 12))
	root.use_hdr_2d = hdr
	var world := Node3D.new()
	root.add_child(world)
	var scene = preload("res://le_scene.gd").new()
	scene.build(world, opts)

func _process(_delta: float) -> bool:
	frames += 1
	if frames == wait:
		var img := root.get_texture().get_image()
		if hdr:
			var f := FileAccess.open(out_dir.path_join(name + ".f16"), FileAccess.WRITE)
			f.store_buffer(img.get_data())
			f.close()
			var j := FileAccess.open(out_dir.path_join(name + ".json"), FileAccess.WRITE)
			j.store_string(JSON.stringify({"w": img.get_width(), "h": img.get_height(), "format": img.get_format()}))
			j.close()
			img.convert(Image.FORMAT_RGBA8) # A linear clip, for looking at only.
		img.save_png(out_dir.path_join(name + ".png"))
		print("captured ", name, " ", img.get_width(), "x", img.get_height())
		return true
	return false
