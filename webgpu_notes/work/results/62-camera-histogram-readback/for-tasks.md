# Task 62: for TASKS.md

## §4 entry

### 4.xx Camera3D histogram readback for scripts (October 2026) *(work task 62, camera batch)*

Merged `<sha>`. camera-sim kept its own histogram (a GDScript loop over a 128x72 `get_image()` 4x a second, plus a
second clean 3D render while zebras or false colour were on) because the built-in one could only be drawn. Now:
- **Camera3D:** `monitoring_histogram_draw` (default `true`; off = counted for scripts only, nothing drawn),
  `monitoring_histogram_update_interval` (s, default `0.0` = every frame), `get_monitoring_histogram(channel = LUMA)`
  -> `PackedInt32Array` of 256 counts (`MONITORING_HISTOGRAM_CHANNEL_{LUMA,RED,GREEN,BLUE}`), signal
  `monitoring_histogram_updated`. The readback starts on demand (first `get_monitoring_histogram()` call or a
  connection to the signal) and runs while the histogram is enabled, independent of Visible in Output, which still only
  decides what is drawn. An enabled histogram that nobody reads costs what it did before; drawing off + interval 0.25
  counts only on the 4 readback frames a second.
- **RenderingServer:** `camera_set_monitoring_histogram_readback(camera, enable, interval)`,
  `camera_get_monitoring_histogram(camera, channel)`, `camera_get_monitoring_histogram_serial(camera)`; the getters are
  not queued (a mutex-guarded result store in `RendererSceneRender`, published from the render thread), so a script
  never waits for the render thread.
- **RD `CameraFinish`:** one 4 KiB count buffer per camera (keyed by the camera RID that `render_camera()` now puts in
  `CameraMonitoring::camera`), so two cameras in one frame no longer share `histogram_buffer`; counts the pre-overlay
  image (or, with no overlay pass, the render target itself, without the scratch copies); readback through
  `RD::buffer_get_data_async()`, validated (each channel must sum to width x height, which drops WebGPU's not-yet-mapped
  zeros). Freed with the camera (`RendererSceneRender::camera_monitoring_free()`).
- **Shared readback fix (WebGPU):** `RDD::buffer_is_map_pending()` (default `false`; WebGPU: map state Pending, and
  unmaps a late Mapped one); `RenderingDevice::_staging_buffer_allocate()` does not reuse a download staging block
  whose `mapAsync` is still in flight. Before it, any per-frame `buffer_get_data_async()` on the web logged
  `[Buffer] used in submit while mapped / pending map` validation errors now and then (14 in a 1-minute run).
- Compatibility: nothing (no compute); `get_monitoring_histogram()` returns empty, documented. No shader changed.
- **Verified:** see results/62-camera-histogram-readback/SUMMARY.md (counts vs CPU per channel, overlays, drawing off,
  two cameras, throttling 3.99/s at 0.25 s, web first frames empty then data, frame time, defaults 0 px in 3 renderers).

## §3 trap candidates

- **WebGPU download staging blocks can be reused while their `mapAsync` is still in flight** (2026-10-28, work task
  62). RenderingDevice reuses a staging block once its frame has been waited on, but on WebGPU that wait does not wait
  for the map: a block whose map completes late is copied into again, and Chrome reports `[Buffer] used in submit while
  mapped` / `while pending map` (and that frame's readback is the shadow copy's old contents). Rare per readback, so it
  only shows with a readback every frame. `RDD::buffer_is_map_pending()` now keeps such a block out of reuse. A
  remaining caveat: a readback whose map had not completed at the frame wait still returns the block's previous
  contents, not zeros; a consumer should validate what it gets (the histogram checks its sum).
- **A luma histogram cannot be matched bit-exactly by a CPU recount**: pixels whose Y' x 255 sits within ~1e-3 of a
  .5 tie round differently in GPU float32 (FMA) and CPU double; 7 of 230,400 pixels in the test chart, R'/G'/B'
  exact. Compare luma within a few pixels, the channels exactly.

## Questions for the user

- `histogram-readback-demand` (choice): when does the readback run?
  - **On demand (default, built):** once a script calls `get_monitoring_histogram()` or connects to
    `monitoring_histogram_updated`, while the histogram is enabled. Nothing is read back for a histogram that is only
    drawn.
  - Always while enabled: simpler to explain, but every enabled histogram costs a readback (and, with Visible in
    Output off, a count) per interval even if nothing reads it.
- `histogram-clean-frame` (choice): the optional "clean frame without overlays" for camera-sim's Download Image.
  - **Separate task (default):** not built here. It needs a texture kept per viewport only while requested and an API
    on Viewport or Camera3D; with task 66's `get_image_async()` it becomes a readback of that texture.
  - Skip: camera-sim keeps its second render for Download Image.

## Note for the camera-sim session

The engine's histogram can now be read from a script (work task 62). In `scenes/monitoring_controller.gd`:
- Drop `_analysis`, `_clean_analysis`, `_reading`, the `get_image()` readback and the per-pixel loop in
  `calculate_histogram()`, and the 0.25 s timer that calls it.
- On the camera: `monitoring_histogram_enabled = true`, `monitoring_histogram_draw = false` (nothing drawn; it is
  counted for the script whatever `monitoring_visible_in_output` says, so the zebras can stay on with it),
  `monitoring_histogram_update_interval = 0.25`, and connect `monitoring_histogram_updated`.
- In the handler: `var luma := camera.get_monitoring_histogram()` (256 counts of Rec. 709 Y', display-encoded, counted
  before zebras/false colour/peaking, so no clean view is needed). Re-bin into the panel's 64: `histogram_bins[i / 4]
  += luma[i]`. Clipping border: `highlights_clipping` = any count in bins 250..255 (0.98 x 255 = 249.9),
  `shadows_clipping` = any count in bins 0..5 (0.02 x 255 = 5.1). The total is width x height of the camera's
  SubViewport render (the sum of the 256 counts), so normalize by it, not by 9,216.
- On the web the first two or three readbacks are not there yet: the array is empty until the first signal.
- `main.make_clean_view` is then used only by Download Image (the clean-frame question above).
- Compatibility renderer: the array stays empty (no compute), as the overlay is unsupported there.
