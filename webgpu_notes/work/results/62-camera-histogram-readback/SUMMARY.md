# Task 62: Camera3D histogram readback: SUMMARY

## What changed

**Camera3D** (`scene/3d/camera_3d.*`, Monitoring > Histogram):
- `monitoring_histogram_draw` (bool, default `true`): off = nothing drawn, counted only for scripts.
- `monitoring_histogram_update_interval` (float s, default `0.0` = every frame).
- `get_monitoring_histogram(channel = MONITORING_HISTOGRAM_CHANNEL_LUMA) -> PackedInt32Array` (256 counts of Y', R',
  G' or B'; empty while disabled, until the first readback, and always in Compatibility).
- signal `monitoring_histogram_updated`; enum `MonitoringHistogramChannel`.
- The readback runs **on demand**: once a script calls `get_monitoring_histogram()` or connects to the signal, while
  `monitoring_histogram_enabled` is on, regardless of `monitoring_visible_in_output` (which still only decides what is
  drawn in a running project). Internal process runs while the histogram is enabled, to notice a new connection and
  emit the signal when the result's serial changes. Mode/position/size/opacity hide in the inspector when drawing is off.

**RenderingServer**: `camera_set_monitoring_histogram_readback(camera, enable, interval)` (queued),
`camera_get_monitoring_histogram(camera, channel)` and `camera_get_monitoring_histogram_serial(camera)` (not queued:
they read a mutex-guarded result store in `RendererSceneRender`, so a script never waits for the render thread).
`RSE::CameraMonitoringHistogramChannel`. `CameraMonitoring` gains `histogram_readback`, `_interval` and the `camera`
RID (set by `RendererSceneCull::render_camera()`), and `is_overlay_active()` (what is drawn) next to `is_active()`.

**RD `CameraFinish`** (`effects/camera_finish.*`):
- One 4 KiB count buffer **per camera** (`camera_histograms`, keyed by camera RID; freed via
  `RendererSceneRender::camera_monitoring_free()` from `RendererSceneCull::free()`). The shared `histogram_buffer`
  stays only as the binding for draws without a histogram.
- Counting: every frame when drawn; otherwise only on frames a readback is due (the schedule steps by the interval, so
  0.25 s gives 4.0/s at any frame rate; it restarts after a hitch or a time rollover). Pre-overlay image as before;
  with no overlay pass, straight from the render target after NR/sharpening with **no scratch copies**. The max pass
  only runs when drawn.
- **The readback mechanism (for task 66 to reconcile):** one clearly separated block in `camera_finish.cpp`,
  `_request_histogram_readback()` + static `_histogram_readback_done()`: `RD::buffer_get_data_async(buffer,
  callable_mp_static(...).bind(camera, pixel_count), 0, 4096)`. The callback runs on the render thread from
  `_stall_for_frame()`, rejects the result unless every channel sums to width x height (this drops WebGPU's
  not-yet-mapped zeros), de-interleaves R'G'B'Y' into the channel enum's order and publishes it. No sync
  `buffer_get_data()` anywhere.
- **Shared readback fix, WebGPU:** `RDD::buffer_is_map_pending()` (default `false`, header-inline next to
  `buffer_initiate_async_map()`); the WebGPU driver returns true while `mapAsync` is Pending and unmaps a late Mapped
  one; `RenderingDevice::_staging_buffer_allocate()` does not reuse an old staging block that is map-pending (it takes a
  new block instead). Without it, a readback every frame logged `[Buffer] used in submit while mapped / pending map`
  validation errors (14 in one suite run, `logs/web_suite_debug_before_staging_fix.log`); with it, 0. Native drivers
  are unaffected (default false). **This touches the same path as task 66** (`texture_get_data_async` uses the same
  `download_staging_buffers`), which benefits from it.
- No shader changed (camera_histogram.glsl and camera_finish.glsl untouched), so no interface reference changes and
  smoke.sh was not run (lead instruction: only if a shader interface changed).
- Docs: `Camera3D.xml`, `RenderingServer.xml` (doctool run; only these two changed).

## Verification (actual numbers)

Test project: `tools/hist_test_main.gd` (main scene of a scratch project; native `--mode=`, web URL hash). A 256x256
chart (R = x, G = y, B = (7x+13y) & 255) on an unshaded quad filling a 640x360 frame, linear tonemapper. Logs in `logs/`.

| Check | Forward+ native | Mobile native | Compatibility | WebGPU Chrome (debug + release) |
|---|---|---|---|---|
| Counts vs CPU count of `get_image()`, R'/G'/B' | exact (0) | exact (0) | empty array, no errors | exact (0) |
| Y' vs CPU (sum abs diff over 256 bins) | 14 (7 px) | 16 (8 px) | | 14 (7 px) |
| Sum of every channel = 640x360 | 230400 | 230400 | | 230400 |
| Zebras (50 IRE) + false colour + peaking on: counts vs off | identical | identical | | identical |
| Drawn histogram on: counts vs off | identical | identical | | identical |
| Drawing off, Visible in Output off / on: image vs histogram-off image | 0 px / 0 px, signals keep coming | 0 / 0 | | `get_image()` null on web (trap 93); signals keep coming |
| Two SubViewports (own worlds), black / white, 320x180 | bin0 = 57600 / bin255 = 57600 | same | empty | same |
| Signal rate at 0.25 s | 3.99/s | 4.00/s | 0 | 4.00/s |
| Signal rate at 0.0 | = frame rate (107-121/s) | = frame rate | 0 | = frame rate (165/s) |
| First frames empty, then data | 1 frame | 1 frame | | 4 frames, 0 console errors/warnings |

- The Y' differences are rounding ties: 169 pixels of the chart have Y' x 255 within 1e-3 of .5, where GPU float32
  (FMA) and the CPU's double round apart; the moved pixels go to the adjacent bin.
- **No stall, native** (`logs/native_timing_320x180.log`, Forward+, uncapped, xvfb, 3 rounds alternating, 600 frames
  each): histogram off 1.81 / 1.91 / 2.00 ms, readback every frame 1.87 / 1.88 / 2.00 ms (600 readbacks in 600 frames),
  every 0.25 s 1.86 / 1.82 / 1.92 ms. Within noise at ~530 fps, where a per-frame fence wait would show. (A 1080p xvfb
  run is present-bound at 40 ms/frame and says nothing; not used.)
- **No stall, web** (`gpu_time.mjs --uncapped --size 1920x1080`, release export, adapter nvidia/lovelace, 48 sampled
  textures, 2 rounds): histogram off 1.12 / 1.12 ms/frame, readback every frame 0.88 / 1.01, every 0.25 s 1.13 / 1.74
  (the last a single noisy round; GPU busy 0.03 ms/frame in all). CPU-bound at ~900 fps and no slower with the readback.
- **Defaults bit-identical** vs the pre-change editor (`tools/bitcheck.sh`, 960x540, lit scene with sky and shadows):
  0 px in Forward+, Mobile and Compatibility for (a) defaults, (b) histogram enabled + drawn with Visible in Output
  (no readback), (c) noise reduction on (`logs/bitcheck.log`).
- Builds: editor (plain and `dev_mode=yes`, werror), web `template_debug` and `template_release` (dlink no, threads no,
  raycast): all compile clean. `pre-commit` clean on all changed files.

## Skipped (lead instruction 2026-10-28: only this task's own checks)

Full Forward+ matrix, scene smoketest, perf_guard, other tiers, smoke.sh (no shader interface changed). No Firefox run
(the task's Verification names Chrome). No RenderDoc capture natively: the "no `buffer_get_data` sync" item rests on
the code (only `buffer_get_data_async`) and the frame times above.

## What the lead must still run

- After merging with task 66: reconcile the staging-block change with theirs (both in the download staging path), and
  re-run `tools/web_suite.mjs <web export> suite` for 0 console errors.
- A by-eye look in the editor: Monitoring > Histogram shows Draw and Update Interval; with Draw off the drawing
  properties hide.

## Risks

- A WebGPU readback whose map has not completed when RD waits for its frame still returns the block's previous shadow
  contents, not zeros (pre-existing driver behavior). The histogram rejects anything whose sums do not match the pixel
  count, so a stale result from a camera of a different size is dropped; one of the same size could pass (one interval
  late; the same camera in practice, since offsets repeat). Not seen in testing.
- A camera rendered into two viewports in one frame (editor preview and a SubViewport) returns whichever was read back
  last; documented.
- `has_connections()` per frame per camera with the histogram enabled: one hash lookup.
- Optional clean pre-overlay frame for screenshots: not built (it needs a per-viewport texture kept on request and new
  Viewport/Camera3D API, more than a small addition); question `histogram-clean-frame` in for-tasks.md proposes it as
  its own task.
