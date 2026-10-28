# 73-webgpu-gpu-time

State: ready-for-integration
Owner: worker-73
Claimed: 2026-10-28
Needs: build, browser
Branch: work/73
Results: webgpu_notes/work/results/73-webgpu-gpu-time/

Phase 1 of the dynamic-resolution build (DESIGN.md's outline item 1, §7.1): **GPU time on WebGPU.** Today the driver's
timestamp readback is off (`rendering_device_driver_webgpu.cpp`, ~line 1341) and it writes timestamps with
`writeTimestamp()` on the command encoder, which is not in the WebGPU spec (Chrome only; Firefox lacks it), so
`RenderingServer.viewport_get_measured_render_time_gpu()` is 0 on the web.

- Write timestamps through pass `timestampWrites` (as `webgpu_tests/perf_guard/gpu_time.mjs` does: empty compute passes
  where a pass boundary is not available), when the adapter offers `timestamp-query` (request it at device creation
  if not already); resolve into a small ring of readback buffers polled with `RDD::buffer_get_readback_status()`
  (task 66, §4.82), never stalling; re-enable `viewport_get_measured_render_time_gpu()` and the Visual Profiler's GPU
  times on the web.
- No `timestamp-query`: GPU time stays 0 and nothing else changes.

Verification: DESIGN.md's bench sweep reports engine GPU time in Chrome and Firefox within 10% of the page hook; the
Visual Profiler (or a script) shows GPU times on the web; with `timestamp-query` withheld, 0 and no errors; no stall
(frame time with and without the timestamps within noise); native unchanged.

## Rules (lead)

- The design is task 68's `results/68-dynamic-resolution/DESIGN.md` (merged into webgpu-4.8 once the user's build ends;
  until then read it at `/mnt/109313D2109313D2/godot-editors/godotwebgpu-68/webgpu_notes/work/results/68-dynamic-resolution/DESIGN.md`), with its `data/` and `proto/` bench. The user answered its nine
  questions on 2026-10-28: Q1 **every change** (manual scale changes stop flashing too), Q2 warn and use Bilinear for FSR 1
  on WebGPU, Q3 **resample** the TAA history, Q4 leave volumetric fog as today, Q5 one viewport at a time largest first,
  **Q6 use the submit-to-done queue depth** to skip the CPU-bound probe when it is clear, Q7 target in **fps**, Q8 max
  scale above 1.0 allowed (default 1.0), **Q9 an opt-in `scaling_3d_dynamic_remember`** saving the settled scale per
  machine in `user://`.
- Read `webgpu_notes/work/README.md` (browser launch rule, release-template rule) and CLAUDE.md first.
- **Test scope (user):** only this task's checks, the default/bit-identical check, the dev_mode editor build and the web
  `template_release` compile; no full Forward+ matrix, scene smoketest or perf_guard unless listed; smoke.sh only if a
  shader interface changes. Say what you skipped.
- camera-sim is read-only; scratch copies only.
