# Task 68: dynamic 3D resolution, design round

Branch `work/68`, worker-68 (two sessions), 2026-10-28. **Design only: no engine code changed.** The design is
`DESIGN.md`; the questions for the user, the build task's outline and the camera-sim hand-back are in `for-tasks.md`.

## What was done

- Read the code paths a scale change goes through (DESIGN.md §1): every `scaling_3d_scale` change is a full
  `RenderSceneBuffersRD` reconfigure that destroys the FSR 2 context, reseeds TAA and clears the auto-exposure
  luminance; the WebGPU driver's timestamp readback is switched off (engine GPU time = 0 on the web).
- Built a throwaway bench (`proto/`: `main.gd` with sweep / hitch / capture / controller modes, `run_web.mjs` with a
  page-side GPU-time source) and measured, under the `gpu` lease, on the RTX 4080 SUPER: sweeps, change costs and the
  prototype controller in Chrome, Firefox and natively (Vulkan, windowed), a native capture around a change with
  FSR 2, TAA and auto exposure, and camera-sim's own web export for the CPU-bound case. Raw results in `data/`.

## Findings that shaped the design

1. GPU time is `a + b * scale^2` with a fixed part of 14-35 %: the controller fits `a` instead of assuming `scale^2`.
2. With GPU time the controller settles in 1-2 s and changes 1.3 times a minute; CPU-bound frames are recognized
   with zero scale changes (native and Chrome).
3. Frame time alone oscillates (5-8 changes/min in Chrome) because vsync quantizes it; the fallback needs
   camera-sim's "remember what failed" with exponential backoff, and CPU-bound detection by a large verified step
   (each probe ~1.4 s at a lower scale).
4. A scale change costs 0.4-2.3 ms CPU; on the web an occasional single 25-70 ms frame.
5. On screen today: auto exposure flashes +34 % for ~10 frames on every change; FSR 2 loses its history (a 4-5x
   temporal-change pulse over ~8 frames); TAA one mild frame. The design keeps one FSR 2 context sized for the max
   scale, resamples TAA history and keeps the 1x1 luminance.
6. camera-sim on the 4080 SUPER: GPU 2.4-5.8 ms in a 12-30 ms frame, and its Auto had settled at 700p, its minimum.
   It is the CPU-bound case the spec predicted, and the reason the CPU-bound check is the core of the design.
7. Both Chrome and Firefox expose `timestamp-query`; the driver's `writeTimestamp()` exists only in Chrome. GPU time
   on the web needs the driver fix in DESIGN.md §7.1 first.

## Skipped, and why

- No engine change, so no `smoke.sh`, no dev_mode editor build, no web `template_release` compile, no perf guard,
  scene smoketest or Forward+ matrix (user's test scope, 2026-10-28: only what this design needs). Those belong to
  the build task.
- No Safari, Android or throttled-GPU runs; the CPU-bound case is a busy-wait plus camera-sim, the GPU-bound case a
  shader loop.
- The Firefox CPU-bound controller run stopped responding at 23 s (timed out); its first two probe/revert cycles
  match Chrome's and native, so it was not repeated.
- `webgpu_tests/perf_guard/gpu_time.mjs` could not measure camera-sim: the export waits behind a Start button the
  tool never clicks (it times out waiting for frames). The bench's hook with a click measured it instead.
- The bench's own GPU-time hook adds ~4 ms per frame in Chrome (it times every encoder); its GPU numbers are
  relative, not a cost estimate for the engine version.
