extends Node
# Task 75 probe for camera-sim (scratch copy only): turns dynamic 3D resolution on for the SubViewport the 3D view
# renders into, and logs what the controller does once a second.
const STATES := ["OFF", "STARTING", "ADJUSTING", "STABLE", "CPU_LIMITED", "AT_MIN_SCALE"]
const SOURCES := ["NONE", "GPU_TIME", "FRAME_TIME"]
var vp: SubViewport
var t0 := 0
var tick := 1.0
var ft := []
var last := 0
var changes := []
var states := {}

func _ready() -> void:
	t0 = Time.get_ticks_usec()
	last = t0
	print("[dr] " + JSON.stringify({"kind": "probe_loaded"}))

func _process(_delta: float) -> void:
	var now := Time.get_ticks_usec()
	var t := (now - t0) / 1e6
	ft.append((now - last) / 1000.0)
	last = now
	if vp == null:
		var best: SubViewport = null
		for n in get_tree().root.find_children("*", "SubViewport", true, false):
			var sv := n as SubViewport
			if sv.render_target_update_mode == SubViewport.UPDATE_ALWAYS and (best == null or sv.size.x * sv.size.y > best.size.x * best.size.y):
				best = sv
		if best == null:
			return
		vp = best
		RenderingServer.viewport_set_measure_render_time(vp.get_viewport_rid(), true)
		var q := str(JavaScriptBridge.eval("location.search"))
		for kv in q.trim_prefix("?").split("&", false):
			if kv.begins_with("target="):
				vp.scaling_3d_dynamic_target_fps = float(kv.split("=")[1])
		vp.scaling_3d_dynamic = true
		vp.scaling_3d_effective_scale_changed.connect(func(s): changes.append([snappedf(t, 0.01), s]))
		print("[dr] " + JSON.stringify({"kind": "start", "t": t, "viewport": str(vp.get_path()), "size": [vp.size.x, vp.size.y],
				"mode": vp.scaling_3d_mode, "target": vp.scaling_3d_dynamic_target_fps, "user_scale": vp.scaling_3d_scale, "start_scale": vp.get_scaling_3d_effective_scale()}))
		return
	var st: String = STATES[vp.get_scaling_3d_dynamic_state()]
	states[st] = states.get(st, 0) + 1
	if t > tick:
		tick += 1.0
		var b := ft.duplicate()
		b.sort()
		print("[dr] " + JSON.stringify({"kind": "tick", "t": snappedf(t, 0.01), "scale": vp.get_scaling_3d_effective_scale(), "state": st,
				"source": SOURCES[vp.get_scaling_3d_dynamic_source()], "frame_med": snappedf(b[b.size() / 2], 0.01),
				"frame_p90": snappedf(b[mini(int(b.size() * 0.9), b.size() - 1)], 0.01),
				"gpu": snappedf(RenderingServer.viewport_get_measured_render_time_gpu(vp.get_viewport_rid()), 0.01),
				"size": [vp.size.x, vp.size.y], "changes": changes.size()}))
		ft.clear()
		if int(t) % 10 == 0:
			print("[dr] " + JSON.stringify({"kind": "summary", "t": snappedf(t, 0.01), "changes": changes, "states_frames": states,
					"final_scale": vp.get_scaling_3d_effective_scale(), "final_state": st, "source": SOURCES[vp.get_scaling_3d_dynamic_source()]}))
