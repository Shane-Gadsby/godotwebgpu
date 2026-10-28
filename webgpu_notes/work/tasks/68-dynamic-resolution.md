# 68-dynamic-resolution: dynamic 3D resolution with a frame-time target

State: open
Owner: worker-68
Claimed:
Needs: build (editor + web template), browser (Playwright, perf guard), native Vulkan run; design first
Branch: work/68
Results: webgpu_notes/work/results/68-dynamic-resolution/

User request, 2026-10-28, from the camera-sim session. A feature proposal. Write the design up for the user
before building it.

## Why

Web builds run on everything from a gaming PC to a school laptop, and a fixed render resolution is either blurry
on the first or too slow on the second. Godot has no dynamic resolution. Every project that wants "as sharp as
this machine can keep at 60 fps" writes its own, and camera-sim did: about 100 lines of `scripts/main.gd`
(`RESOLUTIONS`, `start_auto_resolution`, `update_auto_resolution`).

camera-sim's version, as a working reference:
- the camera renders into a SubViewport shown in a 1244×700 UI panel; "Match screen" sizes the SubViewport to the
  panel's real screen pixels;
- "Auto" tries Match screen first, then steps down through fixed sizes (1440p, 1080p, 900p, 720p, never below
  720p) until one keeps up;
- it times a size only after a settle period (1 s and 10 frames after each change, 3 s after startup, because
  shader compiles and page loading would fail every size);
- a size passes if the median of 60 frames is ≤ 1/55 s and no more than a quarter of them take over 25 ms (a
  missed 60 Hz refresh). It fails early once a quarter have missed, so a slow machine doesn't sit through every
  size;
- the result (most pixels that passed) is saved per machine and browser in `user://display.cfg`, separately for
  windowed and fullscreen; the next start uses it at once, and picking Auto again re-tests.

It changes the SubViewport's **size**, which reallocates its render targets on every step. An engine version
changing `scaling_3d_scale` would be cheaper and smoother, and keeps 2D crisp.

## Goal

A per-Viewport dynamic resolution, off by default:
- `Viewport.scaling_3d_dynamic` (bool, default `false`), plus
  - `scaling_3d_dynamic_target_fps` (or a frame time in ms; default 60),
  - `scaling_3d_dynamic_min_scale` / `max_scale` (e.g. 0.5 / 1.0),
  - optionally `scaling_3d_dynamic_step` (quantised steps, e.g. 0.05, so the resolution doesn't drift every
    frame).
  It adjusts the effective 3D render scale within those bounds, up and down, to hold the target.
- **Measuring.** Use GPU time where available (`viewport_set_measure_render_time` / timestamp queries).
  On WebGPU without the `timestamp-query` feature, fall back to CPU frame pacing (frame delta), and document that
  the fallback can't tell a CPU-bound frame from a GPU-bound one. Lowering the resolution doesn't help a
  CPU-bound frame, so the controller should notice when scaling down didn't help and stop. camera-sim's frame is
  CPU-bound on a fast GPU, so it's a good test of that.
- **Stability.** Hysteresis and a settle time after each change; ignore the startup period and hitches (shader
  compiles, loading). Use camera-sim's numbers as a starting point: median plus missed-frame fraction over a
  window.
- **Upscalers.** It must work with FSR 2 (which supports a varying input size: check Godot's FSR 2 path doesn't
  reset its history on every change) and Bilinear. Not with FSR 1, which renders black on WebGPU. Document
  what TAA and FSR 2 do when the scale changes.
- **Readable state:** `Viewport.get_scaling_3d_effective_scale()` (or similar) and a signal when it changes, so
  a UI can show "Auto (1080p)" as camera-sim's does.
- Persisting a result across runs stays the project's job. Document the pattern (save the scale it settled on,
  start from it next time).

## Verification

- A heavy test scene on a fast and a throttled GPU (or Chrome's CPU throttling for the CPU-bound case): it settles
  at a scale that holds the target, without oscillating (count scale changes per minute after settling).
- **Off:** bit-identical to today.
- **FSR 2:** no flash or history reset visible when the scale changes (screen capture around a change).
- **Web:** works without `timestamp-query`, using the fallback.
- **Perf guard** unchanged with it off; the controller's own cost is negligible with it on.

## camera-sim afterwards

Hand this back to the camera-sim session when it lands. Its "Auto" would become: SubViewport sized to the
panel's real screen pixels (Match screen) with `scaling_3d_dynamic` on, a target of 60 fps and a minimum scale
that keeps 720p. Its own timing code, the candidate list and most of `display.cfg` would go; it would only save
and restore the settled scale per machine, if the engine doesn't.

## Added by the lead

- **Design first, as the spec says.** Deliverable of this round: `results/68-dynamic-resolution/DESIGN.md` (what to
  measure and how on WebGPU and natively, the control loop, the CPU-bound detection, FSR 2 / scaling_3d interaction,
  API, defaults, per-renderer behaviour, costs) with measurements that back its choices, and the open choices as
  questions for the user in for-tasks.md with defaults. **No engine implementation in this round**; the lead files
  the build as a follow-up task once the user has answered. A throwaway prototype in your scratch dir to measure is fine.
- camera-sim (`/mnt/109313D2109313D2/godot-game-dev/camera-sim`) is read-only: read it as the reference, never modify it;
  scratch copies only.
- **Test scope (user, 2026-10-28):** run only the tests directly related to this task (its Verification items, the
  default/bit-identical check, the dev_mode editor build, the web `template_release` compile); no full Forward+
  matrix, scene smoketest or perf_guard. smoke.sh only if a shader interface changes. Say in SUMMARY.md what you skipped.
- Put the "camera-sim afterwards" hand-back, updated with what you built, in for-tasks.md under "Note for the
  camera-sim session".
