# 62-camera-histogram-readback: let scripts read the Camera3D monitoring histogram, without drawing it

State: ready-for-integration
Owner: worker-62
Claimed: 2026-10-28 20:20
Needs: build (editor + web template), browser (Playwright), native Vulkan run
Branch: work/62
Results: webgpu_notes/work/results/62-camera-histogram-readback/

User request, 2026-10-28, from the camera-sim session. camera-sim moved its zebras, false colour, noise and
autofocus to the engine's built-ins, but has to keep its own histogram. Its histogram is a panel in the app's UI,
not an overlay on the image, and the built-in one can only be drawn into a corner of the frame. Nothing can read
its numbers.

## What camera-sim does today (the thing to replace)

`camera-sim/scenes/monitoring_controller.gd` + `histogram.gd`:
- every 0.25 s, it draws the camera's SubViewport texture into a 128×72 SubViewport, then `get_image()` reads it
  back (asynchronous on web, so it reads the previous request's result);
- a GDScript loop over 9,216 pixels builds 64 bins of Rec. 709 luma from the display-encoded values, plus "any
  pixel above 0.98" and "any pixel below 0.02" flags (the panel's red or blue clipping border);
- since the built-in zebras and false colour are drawn into the camera's own image, while either is on it reads
  a second, clean 128×72 render of the scene instead (`main.make_clean_view`): an extra 3D render 4 times a
  second.

The engine already counts the right thing. `CameraFinish::_compute_histogram` builds 256 bins per channel plus
Y' (`camera_histogram.glsl`) from the finished display image, before the zebras, false colour and peaking are
drawn. It only lacks a way out, and a way to count without drawing.

## Goal

### 1. Count without drawing

Today `monitoring_histogram_enabled` both counts and draws, and in a running project only when
`monitoring_visible_in_output` is on. An app with its own histogram UI needs the counts with no overlay. It
also needs `monitoring_visible_in_output` on for its zebras.
- Add a way to count without drawing, e.g. `monitoring_histogram_draw` (bool, default `true`, so existing
  projects are unchanged), or a "data only" value of a mode enum. Pick whichever fits the existing property group.
- Counting should happen in a running project whenever a script needs the data, independent of
  `monitoring_visible_in_output`, which keeps meaning "draw the overlays in the running project".
- No work at all when the histogram is off, as now.

### 2. Read the counts from a script

- `Camera3D.get_monitoring_histogram(channel: MonitoringHistogramChannel = LUMA) -> PackedInt32Array`:
  256 counts for Y', R', G' or B'. It returns the latest completed readback, and an empty array until the
  first one arrives.
- `signal monitoring_histogram_updated` when a new readback lands, so a UI redraws only then.
- **The readback must never stall.** Use the fork's asynchronous readback path (a staging buffer and `mapAsync`
  on WebGPU, the RD equivalent natively). A result one or more frames old is fine. The buffer is 4 KiB per
  frame (`HISTOGRAM_WORDS`).
- `monitoring_histogram_update_interval` (seconds, default `0.0` = every frame) to throttle the readback and
  the JS bridge traffic on web. camera-sim would use 0.25. Counting can stay per frame if that's simpler; it's
  the readback that should be throttled.
- Document that the counts are of the display-encoded image after tonemapping, noise reduction and sharpening,
  and before the monitoring overlays (zebras, false colour, focus peaking, the histogram itself). That's what
  `_compute_histogram` does today; keep it that way and say so. Also document the total, width × height of the
  render, so a script can turn counts into fractions.

### 3. One histogram per camera

`CameraFinish` owns a single `histogram_buffer`. With two cameras rendering histograms in one frame (two
SubViewports, or a SubViewport plus the editor preview), the second overwrites the first before any readback.
Give each viewport's render buffers (or each camera) its own buffer and readback, so
`get_monitoring_histogram()` always returns that camera's counts.

### Not needed

- No change to the drawn overlay itself.
- No CPU fallback for Compatibility. It has no compute, so document that `get_monitoring_histogram()` returns
  empty there, as the overlay is already documented as unsupported.

## Verification

- **Counts:** a scene with a known gradient or test chart. Compare `get_monitoring_histogram()` with a CPU count
  of `get_texture().get_image()` of the same frame: identical, or within ±1 per bin if the readback frame
  differs. Check Y', R', G' and B'.
- **Not affected by the overlays:** with zebras, false colour and focus peaking on, the counts are the same as
  with them off.
- **Count without drawing:** with drawing off, no overlay appears and the counts still update, with
  `monitoring_visible_in_output` both on and off.
- **No stall:** `gpu_time.mjs --uncapped` on a web build with the readback every frame and every 0.25 s; CPU
  frame time within noise of the histogram off. Natively, no `buffer_get_data` sync in a frame capture.
- **Two cameras:** two SubViewports looking at different scenes (one black, one white) each read their own
  histogram.
- **Web:** first frames return empty with no errors, then data, in Chrome, WebGPU Forward+.
- **Throttling:** at 0.25 s the signal fires about 4 times a second.

## camera-sim afterwards

Hand this back to the camera-sim session when it lands:
- `monitoring_controller.gd`: drop `_analysis`, `_clean_analysis`, the `get_image()` readback and the per-pixel
  loop. Set `monitoring_histogram_enabled = true` with drawing off and an update interval of 0.25, then on
  `monitoring_histogram_updated` re-bin the 256 Y' counts into the panel's 64. The clipping border becomes
  "any count in bins ≥ 250" and "any count in bins ≤ 5".
- `main.make_clean_view` then has one user left, Download Image (below).

## Optional, related: a clean frame for screenshots

camera-sim also renders a whole second view of the scene for its "Download Image" button, because the zebras and
false colour are baked into the camera's output. A way to get the frame without the monitoring overlays, for
example `Camera3D` / `Viewport` keeping the pre-overlay image as a readable texture on request
(`get_monitoring_clean_texture()`, only allocated while requested), would remove that too. Separate it into its
own task if it's more than a small addition.

## Added by the lead

- Runs alongside task 62 (histogram readback: CameraFinish, Camera3D) / 63 (physical DOF blur: CameraAttributesPhysical,
  BokehDOFRD); keep changes in their own blocks, the lead merges both.
- camera-sim (`/mnt/109313D2109313D2/godot-game-dev/camera-sim`) is read-only: read its scripts as the reference, never
  modify it; scratch copies only.
- Compile a web `template_release` before `ready-for-integration` (work README, TASKS §3 trap 95).
- Put the "camera-sim afterwards" hand-back, updated with what you actually built (names, defaults), in for-tasks.md
  under "Note for the camera-sim session".
