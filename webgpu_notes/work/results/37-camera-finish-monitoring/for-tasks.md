# For TASKS.md (task 37: camera finish pass and monitoring overlays)

## §4 entry (suggested: §4.5x "Camera finish: in-camera NR and sharpening; Camera3D monitoring overlays")

A new **camera finish pass** runs after the 3D image is complete (after tonemapping, FSR/SMAA, debug draw and
compositor effects), at the output resolution, on the render target, before 2D. It is skipped entirely when nothing
in it is on (`CameraFinish::is_active()`), so existing projects render exactly as before.

- **CameraAttributes** (shared base): `noise_reduction_{enabled,strength,chroma_only}` (bilateral filter on
  display-encoded Y'CbCr, Tomasi-Manduchi 1998; chroma range sigma 2x luma's) and
  `sharpening_{enabled,amount,radius,halo}` (unsharp mask on luma, radius in px of a 1080-line frame, halo = CAS-style
  clamp to the 3x3 range). RS: `camera_attributes_set_noise_reduction()`, `camera_attributes_set_sharpening()`;
  model in `camera_attributes_storage.cpp` ("Camera finish").
- **Camera3D** group Monitoring: `monitoring_visible_in_output`, zebras (level 95 IRE, optional second zebra as a
  +-5 IRE window at 70), focus peaking (Sobel on display luma; color, sensitivity), false color (ARRI ALEXA bands),
  histogram (luma/RGB, 256 bins, GPU compute, corner/size/opacity). RS: `camera_set_monitoring_{zebras,focus_peaking,
  false_color,histogram}()`. Camera3D sends them only when `Engine::is_editor_hint()` or Visible in Output; otherwise
  it sends "off", so a game with editor-only overlays runs nothing. RendererSceneCull's Camera holds them and copies
  them into `CameraData::monitoring`.
- RD (Forward+, Mobile, WebGPU): `RendererRD::CameraFinish` (`effects/camera_finish.*`), shaders
  `camera_finish.glsl` (one raster variant, mode in its own 112-byte push constant: NR, sharpen, overlay passes
  ping-ponged through two scratch copies in render buffers context `camera_finish`, the last writing the render target)
  and `camera_histogram.glsl` (count with workgroup-shared atomics, then a 256-thread max pass, 4 KiB storage buffer).
  Shared math in `camera_finish_inc.glsl`; the Compatibility copy is identical.
- Compatibility: `GLES3::CameraFinish` (`drivers/gles3/effects/camera_finish.*`): NR, sharpening, zebras, peaking,
  false color. **No histogram** (no compute; documented in the class reference).
- Not in the tonemap pass, so the tonemap push constant and the CameraEffects UBO of task 33 are untouched.
- Single-view only (XR/multiview skipped). NR/sharpening skipped in debug draw modes and reflection probes.
- Interface references: 4 new modules (camera_finish vert/frag, camera_histogram count/max) added; no existing module
  changed.

## README engine additions line

- **Camera finish and monitoring**: in-camera noise reduction and sharpening on CameraAttributes; zebras, focus
  peaking, false color and a GPU histogram on Camera3D, editor-only or also in the running game.

## Questions for the user

1. Zebra stripe colors: zebra 1 draws **black** stripes (visible over clipped white), the second zebra **white**
   stripes the other way. Default: keep. Alternative: both white like Sony, or user-selectable colors.
2. Second zebra as a +-5 IRE **window** around its level (Sony "70 +- 5" style) rather than "above level".
   Default: window.
3. Focus peaking from the image's luma gradient, not the DOF circle of confusion (CoC is not available in
   Compatibility or when DOF is off, and peaking on real cameras is image-based). Default: image-based.
