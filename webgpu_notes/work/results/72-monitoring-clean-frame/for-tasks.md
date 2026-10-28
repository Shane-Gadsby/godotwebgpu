# Task 72: for TASKS.md

## §4 entry

### 4.xx Camera3D clean frame: the image without the monitoring overlays (October 2026) *(work task 72; camera-sim spec)*

Merged `<sha>`. camera-sim rendered a second view of the scene for "Download Image" because zebras, false color and
focus peaking are drawn into the camera's image. Now `Camera3D.monitoring_clean_texture_enabled` (default off) and
`get_monitoring_clean_texture()` (a Texture2D, `null` while off): while enabled, each frame the camera renders is copied
before the overlays and before 2D (after tonemapping, NR and sharpening) into a texture of the camera's; disabling frees
it. Readable with `get_image_async()` / `get_image_awaitable()` (task 71) or drawn like any texture; enable + read +
disable in one function works (the read is after the frame's draw). RS `camera_set_monitoring_clean_texture(camera,
texture)`; `CameraMonitoring::clean_texture`; per-camera copy in RD `CameraFinish` (`texture_copy` from the scratch the
overlay pass reads, or from the render target) and `GLES3::CameraFinish` (blit); `render_target_copy_texture_set()` in
both texture storages makes the server texture read back like the viewport texture (same Image format). The returned
texture is an internal class (`CameraMonitoringCleanTexture`). All three renderers and WebGPU; not XR. Verified
(worker): clean vs the overlays-off frame 0 px in Forward+, Mobile, Compatibility, WebGPU debug/release (4 overlay
configs, NR+sharpening, the one-function pattern, two viewports, a resize); memory +1 texture while on, back to
baseline off; defaults and the output with it enabled 0 px vs the pre-change editor in 3 renderers. No shader changed.

## §3 trap candidates

- **`texture_copy` from an RD scratch texture needs `CAN_COPY_FROM` in its usage** (2026-10-28, task 72): camera
  finish's scratch textures had only `CAN_COPY_TO`, so the first clean-frame copy failed with "Source texture requires
  TEXTURE_USAGE_CAN_COPY_FROM_BIT" and silently left the copy stale (the test read the previous configuration's frame,
  230,400 px off). Check every new copy source's usage bits; RD's error does not stop the frame.
- **GLES3 `glBlitFramebuffer` is scissored**: a blit after a pass that left the scissor test on copies only the
  scissor rectangle. Disable `GL_SCISSOR_TEST` before blitting.

## Questions for the user

- `clean-frame-inspector` (choice): where does the switch live?
  - **Camera3D property under Monitoring, in the inspector (default, built):** `monitoring_clean_texture_enabled`,
    saved with the scene, so a scene can keep a clean feed (e.g. for a TextureRect) without a script.
  - Script-only (not shown in the inspector, not saved): `get_monitoring_clean_texture()` only; avoids an inspector
    option most users never need.

## Note for the camera-sim session

"Download Image" no longer needs the second render (work task 72). In `scripts/main.gd`:
- Replace `capture_camera_image()` (the `_recorder` SubViewport, `RECORDER_FRAMES`, `read_back()`) with:
  ```gdscript
  func capture_camera_image() -> Image:
      camera.monitoring_clean_texture_enabled = true
      var image: Image = await camera.get_monitoring_clean_texture().get_image_awaitable()
      camera.monitoring_clean_texture_enabled = false
      return image
  ```
  (or `get_image_async(callback)` if task 71 is not in your engine build). The image is the camera view's own frame:
  same resolution, TAA/FSR 2 already settled (it is the live view's frame, not a cold re-render), exposure, DOF, white
  balance, noise, NR/sharpening, and none of the zebras, false colour, peaking or histogram. 2D drawn over the
  SubViewport (guides) is not in it. Same format as `viewport.get_texture()` (RGB8 for an opaque viewport).
- With task 62's histogram readback, `make_clean_view()` and `sync_clean_view()` can then go entirely.
- Works on the web (WebGPU) the same as on desktop, no retry loop needed.
