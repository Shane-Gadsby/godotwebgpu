# Task 75: for TASKS.md (lead copies in)

## §4 entry

### 4.xx Dynamic 3D resolution: the controller, API and settings (October 2026) *(work task 75; dynamic resolution phases 3-4, task 68 DESIGN, user answers Q2, Q5-Q9)*

A per-viewport controller in `RendererViewport` picks the 3D scale each frame to hold a target frame rate. It is
**off by default**, and off it is bit-identical to before. It runs once per frame, before the viewports draw, and has
two paths:

- **GPU time** (`vp_begin`/`vp_end` timestamps; on WebGPU task 73's readback):
  - Lowers the scale when the median over 30 samples is above 95% of the budget, straight to the scale the
    `a + b·scale²` model puts at 85%.
  - Raises one step when the model says the next step stays under 80%. Between 80% and 95% nothing changes.
  - The fixed part `a` is refitted from two settled points at least 0.1 apart.
  - Frames that miss while the GPU is under 75% of the budget make the state `CPU_LIMITED`; the scale is never
    lowered for them.
- **Frame time**, where there is no GPU time:
  - A step down must deliver a quarter of the gain the model predicts, or it is reverted and the state becomes
    `CPU_LIMITED`. The retry back-off starts at 5 s and doubles up to 60 s.
  - A step up is tried only when the frame fits under 80% and the model agrees. A step up that misses is reverted,
    and that scale is not retried for 10 s, doubling up to 120 s, unless the load has dropped by a quarter since.
  - **Q6:** on WebGPU, `RDD::get_submit_to_done_latency_usec()` (one `onSubmittedWorkDone` per frame, in its own
    in-order FIFO) gives the frames in flight. At 2.5 or fewer it reports CPU-bound without probing. At 4 or more it
    steps down without verifying.
  - **Q5:** frame-time viewports take turns, one per settle, largest first.

Shared rules for both paths:
- Settle after a change: max(0.25 s, 8 frames), plus twice the queue latency.
- Startup: 3 s ignored, and the same after a scene change.
- Hitches (over 4x both the budget and the running average) are skipped.
- Scales are quantized to 0.05.

API and settings:
- **API:** `Viewport.scaling_3d_dynamic`, `_target_fps` (60), `_min_scale` (0.5), `_max_scale` (1.0; above 1.0 with
  Bilinear only), `_remember`, `get_scaling_3d_effective_scale()`, `get_scaling_3d_dynamic_state()` /
  `_source()`, and the signal `scaling_3d_effective_scale_changed`. `RenderingServer` has the same as
  `viewport_set_scaling_3d_dynamic*` / `viewport_get_scaling_3d_*`. The getters read atomics, with no render-thread
  sync.
- **Project settings:** `rendering/scaling_3d/dynamic*`, applied to the root viewport only and never in the editor.
- **`scaling_3d_scale`** stays the user's value and is the starting scale.
- **Q9 remember:** saves the scale on `STABLE` to `user://scaling_3d_dynamic.cfg` (per renderer + GPU, keyed by
  viewport path) and starts from it next run.
- **FSR 2** gets its context at `target × max_scale` (`RenderSceneBuffersConfiguration::max_internal_size`), so
  controller changes never grow it (task 74's question).
- **Q2:** FSR 1 + dynamic on WebGPU falls back to Bilinear with a warning.
- **XR:** not supported (warning).

Worker results (RTX 4080 SUPER, task 68's scene at 1920x1080, target 60):
- **GPU-bound:** 0 changes/min once settled natively, in Chrome and in Firefox, each settled window 30-45 s. Back at
  1.0 within 1.5-1.6 s of a load drop.
- **CPU-bound:** 1.0 and `CPU_LIMITED`, 0 changes, on all three. camera-sim's SubViewport: `STABLE` at 1.0 at
  60 fps, `CPU_LIMITED` at 1.0 with a 240 fps target.
- **`timestamp-query` withheld:** 0 changes/min settled in Chrome and Firefox. Q6 skipped the CPU-bound probe in
  both browsers (1.6-2.0 frames in flight).
- **Off:** bit-identical (9/9 configurations against the pre-change editor), and perf_guard PASS 4/4.
- **FSR 2:** no pulse across controller changes (largest d after the change at or under steady).
- **Remember:** round-trips natively and on a web reload.
- **Builds:** dev_mode editor, web debug and release, 0 warnings.
Results: `results/75-dynamic-resolution-controller/`.

## §3 trap candidates

- **A hitch filter in absolute terms freezes a controller on a slow scene.** Skipping frames over 4x the budget
  skipped every frame of a 56 ms scene at a 10 ms budget (a 15 fps game with a 60 fps target is the same), so no
  window ever filled and the scale never moved. Measure a hitch against the running average as well.
- **A WebGPU frame fence cannot time its own submit.** `fence_wait()` force-signals (trap 102), so the frame fence is
  resubmitted while its previous `onSubmittedWorkDone` can still be pending, and a callback would pair with the newer
  submit time. Submit-to-done latency needs its own callback per submit and a FIFO (callbacks resolve in order).
- **Frame-time probing under vsync oscillates unless the up-step also asks the model.** At 165 Hz a frame is 12.1 or
  18.2 ms, so a scale that "fits" one step below often cannot take the next. A failure memory that outlives a load
  drop also kept the scale low for 34 s after the load fell.
- **camera-sim renders its 3D into a SubViewport.** The root viewport's project setting does not reach it. A project
  like that enables `scaling_3d_dynamic` on the SubViewport. To test an existing web export without rebuilding it,
  preload an `override.cfg` that autoloads a script from an absolute path (`DynOn="*/dyn_on.gd"`, both files given
  to `engine.preloadFile()`).

## Questions for the user (with defaults)

1. **Remember key**: the saved scale is keyed by renderer + GPU name + viewport path, not by window size or
   fullscreen. camera-sim's old Auto saved per window mode. **Default: as built**; a project that switches between
   very different window sizes can clear the saved value on a mode change.
2. **Step size** is fixed at 0.05 (the design's `scaling_3d_dynamic_step` was not exposed, to keep the API small).
   **Default: keep it internal.**
3. **Frame-time fallback rate**: with no GPU time (WebGL, or WebGPU without `timestamp-query`) the fallback probes a
   failed scale again after 10, 20, 40... up to 120 s, so it changes more often than the GPU path while it learns
   (see SUMMARY). **Default: as built**; the alternative is a longer first back-off (fewer probes, slower to use
   freed headroom).

## Note for the camera-sim session (adopting it)

- **camera-sim draws its 3D in `%SubViewport`**, so turn the controller on there, not with the project setting:
  ```gdscript
  viewport.scaling_3d_dynamic_target_fps = 60
  viewport.scaling_3d_dynamic_remember = true   # or save get_scaling_3d_effective_scale() yourself on STABLE
  viewport.scaling_3d_dynamic = true
  ```
- **Its old Auto**, which resized the SubViewport through a list, should not run at the same time. Every SubViewport
  resize restarts the controller's settle and its cost model.
- **Show "Auto (NNNp)"** from `get_scaling_3d_effective_scale() * viewport.size.y`, plus "limited by CPU" when
  `get_scaling_3d_dynamic_state() == Viewport.SCALING_3D_DYNAMIC_STATE_CPU_LIMITED`. Measured in its current web
  export, it stays at full scale and reports CPU-limited (SUMMARY.md).
- **The derived views** (`make_clean_view()` copies `scaling_3d_scale`) should copy `get_scaling_3d_effective_scale()`
  if they are meant to match what is on screen.
