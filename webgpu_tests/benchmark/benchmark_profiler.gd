extends Node
## Autoload benchmark profiler. Collects per-frame CPU and GPU render times
## using RenderingServer.viewport_get_measured_render_time_cpu/gpu().
##
## These measure actual render work, NOT padded by vsync — so they give
## meaningful comparisons even when vsync-locked at 60fps.
##
## GPU time requires timestamp queries:
##   - Metal: returns 0 (known Godot bug #102968)
##   - WebGPU: requires chrome://flags/#enable-webgpu-developer-features
##   - WebGL: returns 0 (no timer queries)

# Both a frame count AND a wall-clock minimum: uncapped, 180 frames pass in well under a second, which is still the
# page's startup phase (pipeline warm-up, GPU clocks ramping) and made uncapped numbers swing by 2x (TASKS.md 2.17).
const WARMUP_FRAMES := 180
const WARMUP_SECONDS := 6.0
const COLLECT_FRAMES := 300  # at least this many frames...
const COLLECT_SECONDS := 4.0 # ...and at least this long

var _frame := 0
var _vp_rid: RID
var _deltas: PackedFloat64Array
var _render_cpu: PackedFloat64Array
var _render_gpu: PackedFloat64Array
var _draw_calls: PackedFloat64Array
var _primitives: PackedFloat64Array

var _t_start := 0.0
var _t_collect := 0.0

func _ready() -> void:
	_vp_rid = get_viewport().get_viewport_rid()
	# NOTE: On WebGPU, GPU timestamp queries cause "buffer used in submit while
	# mapped" errors under heavy load (emdawnwebgpu async readback limitation).
	# Only enable on non-web platforms where buffer mapping is synchronous.
	if OS.get_name() != "Web":
		RenderingServer.viewport_set_measure_render_time(_vp_rid, true)
	_t_start = Time.get_ticks_msec() / 1000.0
	process_priority = -100

func _process(delta: float) -> void:
	_frame += 1
	var now := Time.get_ticks_msec() / 1000.0

	if _frame <= WARMUP_FRAMES or now - _t_start < WARMUP_SECONDS:
		if _frame == 1:
			print("[BENCHMARK] warming up (%d frames, %.0f s)..." % [WARMUP_FRAMES, WARMUP_SECONDS])
		return

	if _deltas.is_empty():
		_t_collect = now
		print("[BENCHMARK] collecting (>= %d frames and >= %.0f s)..." % [COLLECT_FRAMES, COLLECT_SECONDS])
	_deltas.push_back(delta)
	_render_cpu.push_back(RenderingServer.viewport_get_measured_render_time_cpu(_vp_rid))
	_render_gpu.push_back(RenderingServer.viewport_get_measured_render_time_gpu(_vp_rid))
	_draw_calls.push_back(Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME))
	_primitives.push_back(Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME))
	if _deltas.size() >= COLLECT_FRAMES and now - _t_collect >= COLLECT_SECONDS:
		_print_results()
		set_process(false)
		# Scripted native runs (run_benchmark.sh with BENCH_AUTOQUIT=1) exit by themselves once the results are out.
		if OS.get_environment("BENCH_AUTOQUIT") == "1" and OS.get_name() != "Web":
			get_tree().quit()

func _print_results() -> void:
	var n := _deltas.size()

	var sorted_delta := _deltas.duplicate()
	sorted_delta.sort()
	var sorted_cpu := _render_cpu.duplicate()
	sorted_cpu.sort()
	var sorted_gpu := _render_gpu.duplicate()
	sorted_gpu.sort()

	var avg_delta := _avg(_deltas)
	var avg_cpu := _avg(_render_cpu)
	var avg_gpu := _avg(_render_gpu)
	var avg_dc := _avg(_draw_calls)
	var avg_prims := _avg(_primitives)

	var gpu_available := avg_gpu > 0.001

	print("")
	print("[BENCHMARK] ============ RESULTS (%d frames) ============" % n)
	print("[BENCHMARK] fps_avg=%.1f" % (1.0 / avg_delta))
	print("[BENCHMARK]")
	print("[BENCHMARK] --- Frame Time (wall clock) ---")
	print("[BENCHMARK] frame_avg=%.2f ms" % (avg_delta * 1000.0))
	print("[BENCHMARK] frame_p50=%.2f ms" % (sorted_delta[n / 2] * 1000.0))
	print("[BENCHMARK] frame_p95=%.2f ms" % (sorted_delta[int(n * 0.95)] * 1000.0))
	print("[BENCHMARK] frame_p99=%.2f ms" % (sorted_delta[int(n * 0.99)] * 1000.0))
	print("[BENCHMARK] frame_min=%.2f ms" % (sorted_delta[0] * 1000.0))
	print("[BENCHMARK] frame_max=%.2f ms" % (sorted_delta[n - 1] * 1000.0))
	print("[BENCHMARK]")
	print("[BENCHMARK] --- CPU Render Time ---")
	print("[BENCHMARK] cpu_avg=%.2f ms" % avg_cpu)
	print("[BENCHMARK] cpu_p50=%.2f ms" % sorted_cpu[n / 2])
	print("[BENCHMARK] cpu_p95=%.2f ms" % sorted_cpu[int(n * 0.95)])
	print("[BENCHMARK] cpu_p99=%.2f ms" % sorted_cpu[int(n * 0.99)])
	print("[BENCHMARK]")
	if gpu_available:
		print("[BENCHMARK] --- GPU Render Time (timestamp queries) ---")
		print("[BENCHMARK] gpu_avg=%.2f ms" % avg_gpu)
		print("[BENCHMARK] gpu_p50=%.2f ms" % sorted_gpu[n / 2])
		print("[BENCHMARK] gpu_p95=%.2f ms" % sorted_gpu[int(n * 0.95)])
		print("[BENCHMARK] gpu_p99=%.2f ms" % sorted_gpu[int(n * 0.99)])
		print("[BENCHMARK] gpu_min=%.2f ms" % sorted_gpu[0])
		print("[BENCHMARK] gpu_max=%.2f ms" % sorted_gpu[n - 1])
	else:
		print("[BENCHMARK] --- GPU Render Time ---")
		print("[BENCHMARK] gpu=N/A (timestamp queries unavailable)")
		print("[BENCHMARK] (Metal: known bug #102968. WebGPU: enable chrome://flags/#enable-webgpu-developer-features)")
	print("[BENCHMARK]")
	print("[BENCHMARK] --- Rendering ---")
	print("[BENCHMARK] draw_calls_avg=%.0f" % avg_dc)
	print("[BENCHMARK] primitives_avg=%.0f" % avg_prims)
	print("[BENCHMARK] ================================================")
	print("")

func _avg(arr: PackedFloat64Array) -> float:
	var sum := 0.0
	for v in arr:
		sum += v
	return sum / arr.size()
