# panini_test.gd: scene for task 47 (Panini projection). Usage:
#   godot --path proj --resolution 1600x900 -s panini_test.gd -- <case> <out_dir> <layout> <WxH>
# case: off | d<distance>s<compression> (e.g. d1s1, d0.5s0.5) | default (sky, sun, glow, Filmic, no attributes changes)
# layout: target (dots, poles, rod; default) or default.
# Writes <out_dir>/<case>.png and <case>.json (dot directions, Camera3D unproject results, round-trip errors).
extends SceneTree

var frames := 0
var scene = preload("res://panini_scene.gd").new()
var cam: Camera3D
var dots := []
var case_name := ""
var out_dir := ""

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	case_name = args[0]
	out_dir = args[1]
	var layout := "target" if args.size() < 3 else args[2]
	var world := Node3D.new()
	root.add_child(world)
	root.mesh_lod_threshold = 0.0 # See panini_scene.gd.
	# The window's size, passed as WxH: root.size is not the window's yet here.
	var size := Vector2(root.size)
	if args.size() >= 4:
		var wh := args[3].split("x")
		size = Vector2(float(wh[0]), float(wh[1]))
	scene.build(world, case_name, layout, size)
	cam = scene.cam
	dots = scene.dots

func _process(_delta: float) -> bool:
	frames += 1
	if frames == 12:
		_capture()
		return true
	return false

func _px(p: Vector2) -> Array:
	return [p.x, p.y]

func _capture() -> void:
	var img := root.get_texture().get_image()
	var size := Vector2(root.size)
	if img: # null with --headless, where only the Camera3D functions are checked.
		img.save_png(out_dir.path_join(case_name + ".png"))
		size = Vector2(img.get_width(), img.get_height())
	var out := {"case": case_name, "size": [size.x, size.y], "fov": cam.fov, "dots": []}
	var cam_xf := cam.get_camera_transform()
	for d in dots:
		var local: Vector3 = cam_xf.affine_inverse() * d.pos
		out.dots.append({"az": d.az, "el": d.el, "dir": [local.x, local.y, local.z], "unproject": _px(cam.unproject_position(d.pos))})
	# Round trips over a 33 x 19 grid of screen points (edges included): ray -> world point -> unproject, and
	# project_position at two depths -> unproject. project_ray_origin must stay the camera position (perspective).
	var max_ray := 0.0
	var max_pos := 0.0
	var max_origin := 0.0
	var max_dir := 0.0
	for j in 19:
		for i in 33:
			var p := Vector2(i / 32.0 * (size.x - 1), j / 18.0 * (size.y - 1))
			var origin := cam.project_ray_origin(p)
			max_origin = max(max_origin, origin.distance_to(cam_xf.origin))
			var n := cam.project_ray_normal(p)
			max_ray = max(max_ray, cam.unproject_position(origin + n * 7.0).distance_to(p))
			for depth in [0.5, 20.0]:
				var w := cam.project_position(p, depth)
				max_pos = max(max_pos, cam.unproject_position(w).distance_to(p))
				# project_position(depth) lies on the ray project_ray_normal returns.
				max_dir = max(max_dir, (w - origin).normalized().angle_to(n))
	out["big"] = []
	for b in scene.big:
		var local: Vector3 = cam_xf.affine_inverse() * b.pos
		out.big.append({"az": b.az, "el": b.el, "dir": [local.x, local.y, local.z]})
	out["roundtrip"] = {"ray_px": max_ray, "position_px": max_pos, "origin_m": max_origin, "position_vs_ray_rad": max_dir}
	var f := FileAccess.open(out_dir.path_join(case_name + ".json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(out, "\t"))
	f.close()
	print("captured ", case_name, " ", out.roundtrip)
