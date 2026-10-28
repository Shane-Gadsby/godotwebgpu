# 75-dynamic-resolution-controller: per-viewport dynamic 3D resolution (phases 3-4)

State: ready-for-integration
Owner: worker-75
Claimed: 2026-10-29 00:14
Needs: build, gpu, browser
Branch: work/75
Results: webgpu_notes/work/results/75-dynamic-resolution-controller/

The build of task 68's design (`webgpu_notes/work/results/68-dynamic-resolution/DESIGN.md`, merged), on top of phase 1
(task 73, §4.87 when recorded: GPU time on WebGPU) and phase 2 (task 74, §4.86: scale changes keep auto exposure,
FSR 2 and TAA). User answers to the design's nine questions (2026-10-28): Q1 every change (done in 74), Q2 FSR 1 on
WebGPU: warn and use Bilinear, Q3 resample TAA (done in 74), Q4 volumetric fog as today, Q5 several dynamic viewports
without GPU time: one at a time, largest first, **Q6 use the submit-to-done queue depth to skip the CPU-bound probe
when it is clear**, Q7 target in fps (`scaling_3d_dynamic_target_fps`, default 60), Q8 max scale above 1.0 allowed
(default 1.0), **Q9 opt-in `scaling_3d_dynamic_remember`** saving the settled scale per machine in `user://`.

## Goal (DESIGN.md §3, §5, §8 and the outline's items 3-4)

- The controller in `RendererViewport` (§3): GPU time where available (task 73 on the web, native timestamps), the
  `a + b·scale²` cost model with the fixed part estimated, hysteresis, the frame-time fallback with failed-scale
  memory and growing back-off, CPU-bound detection by one probe step (skipped when the queue-depth signal is clear,
  Q6), reporting `CPU_LIMITED`; FSR 2 context created at the dynamic max (task 74's question); off by default.
- API on `Viewport` / `RenderingServer` and project settings (§5): enable, target fps, min/max scale, the remember
  option (Q9), a read-only current scale / state; class docs; the §8 persistence pattern documented.
- Off = bit-identical to today.

## Verification (the outline's item 4)

Settles without oscillation (< 2 changes/min once settled) on a GPU-bound scene natively, in Chrome and in Firefox;
a CPU-bound scene and camera-sim (its existing web build under `/mnt/109313D2109313D2/godot-game-dev/camera-sim/builds/`,
read-only, copy to scratch) stay at full scale and report CPU-limited; the web fallback with `timestamp-query` withheld;
off = bit-identical (and perf_guard once with it off); the FSR 2 capture around a controller-driven change (task 74's
bench); remember round-trips across a restart; dev_mode editor and web template_release builds; smoke.sh only if a
shader changes.

## Rules (lead)

- Read `webgpu_notes/work/README.md` (browser launch rule, release-template rule) and CLAUDE.md first; DESIGN.md in
  full; task 73's and 74's results.
- Test scope (user): only this task's checks plus the builds. camera-sim read-only.
