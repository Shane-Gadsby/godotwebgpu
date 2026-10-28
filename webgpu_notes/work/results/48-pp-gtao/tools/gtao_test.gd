extends SceneTree
# Native driver: renders gtao_scene.gd and saves the viewport image(s) and GPU timings.
# Args (after --): out=<png prefix> frames=<n> save=<k last frames to save> time=1 (+ gtao_scene.gd keys)
var args := {}
var frame := 0
var frames := 12
var save := 1
var times: Array[float] = []

func _init() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			args[kv[0]] = kv[1]
	frames = int(args.get("frames", "12"))
	save = int(args.get("save", "1"))
	var scene: Node3D = load("res://gtao_scene.gd").new()
	root.add_child(scene)
	scene.setup(args, root)
	if args.get("time", "0") == "1":
		RenderingServer.viewport_set_measure_render_time(root.get_viewport_rid(), true)
	process_frame.connect(_on_frame)

func _on_frame() -> void:
	frame += 1
	if args.get("time", "0") == "1" and frame > 30:
		times.append(RenderingServer.viewport_get_measured_render_time_gpu(root.get_viewport_rid()))
	if frame > frames - save and args.has("out"):
		var img := root.get_viewport().get_texture().get_image()
		img.save_png("%s_%03d.png" % [args["out"], frame])
	if frame >= frames:
		if times.size() > 0:
			times.sort()
			var med := times[times.size() / 2]
			print("GPU_MS median=%.4f min=%.4f n=%d" % [med, times[0], times.size()])
		quit()
