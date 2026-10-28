# Task 37: camera finish pass (in-camera NR + sharpening) and Camera3D monitoring overlays

Branch `work/37`. State: ready-for-integration.

## What changed

- **New pass, not in the tonemapper.** `RendererRD::CameraFinish` (`servers/rendering/renderer_rd/effects/camera_finish.*`)
  runs at the end of `RendererSceneRenderRD::render_scene()` (after `_render_scene()`: tonemap, FSR/SMAA, debug draw,
  compositor effects are all done), on the render target at output resolution, before 2D. It copies the RT into a
  scratch texture (render buffers context `camera_finish`, two textures, ping-ponged), runs NR -> sharpen -> overlay
  passes as enabled, the last one writing the RT. It does nothing (no textures, no passes) unless something is on.
  The tonemap push constant and task 33's CameraEffects UBO are untouched.
- Shaders: `shaders/effects/camera_finish.glsl` (one raster variant; mode in its own 112-byte push constant; set 0 =
  sampler2D + read-only histogram buffer), `camera_histogram.glsl` (compute: count with workgroup-shared atomics, then a
  256-thread max pass; 1028-uint storage buffer), shared math in `camera_finish_inc.glsl` (identical copy in
  `drivers/gles3/shaders/effects/`). Registered in `drivers/webgpu/wgsl_precompile.py`.
- Compatibility: `GLES3::CameraFinish` (`drivers/gles3/effects/camera_finish.*`, shader `camera_finish.glsl` with
  modes NR/sharpen/overlay + `LINEAR_INPUT` specialization), hooked after `_render_post_processing()`. **No histogram**
  in Compatibility (needs compute); documented in the class reference.
- **CameraAttributes** (shared base): groups Noise Reduction (`noise_reduction_enabled/strength/chroma_only`) and
  Sharpening (`sharpening_enabled/amount/radius/halo`). RS `camera_attributes_set_noise_reduction()`,
  `camera_attributes_set_sharpening()`; `RendererCameraAttributes::camera_attributes_get_finish()` turns them into
  per-output-height settings (model and sources in the "Camera finish" comment in `camera_attributes_storage.cpp`:
  bilateral filter, Tomasi-Manduchi 1998; Rec. 709 Y'CbCr; unsharp mask with the FidelityFX CAS ringing clamp).
- **Camera3D** group Monitoring: `monitoring_visible_in_output`, subgroups Zebras (level 95 IRE, second zebra = +-5 IRE
  window at 70), Focus Peaking (color, sensitivity), False Color (ARRI ALEXA bands), Histogram (Luma/RGB, corner,
  size, opacity). RS `camera_set_monitoring_{zebras,focus_peaking,false_color,histogram}()` + enums
  `RS.CameraMonitoringHistogramMode/Position`; `RendererSceneCull::Camera::monitoring` -> `CameraData::monitoring`
  (new `RendererSceneRender::CameraMonitoring` struct). Camera3D sends the overlays only when
  `Engine::is_editor_hint()` or Visible in Output is on; otherwise it sends "off", so nothing runs in the game.
- Class reference: Camera3D, CameraAttributes, RenderingServer (generated with `--doctool`, descriptions filled; no
  unrelated files touched).
- Interface references regenerated, **additions only**: the 4 new modules (camera_finish vert/frag, camera_histogram
  count/max) in `interface_reference.json` (source string appended) and 8 lines in `scan_reference.txt`. No existing
  module changed.

## Verification (all measured; scripts in `verify/`, raw results in `verify/{fp,mb,gl}_results.txt`)

Scenes come from `verify/proj/scene_builder.gd`: an unshaded full-frame pattern from SCREEN_UV with the linear
tonemapper, so display values are exact (a 0-1 horizontal ramp + 8 patches; a 0.35|0.65 step over a flat 0.5), and a
checker at 3 m beside one at 12 m behind a far DOF. References are independent numpy code (`verify/analyze.py`).

| Check | Forward+ (Vulkan) | Mobile (Vulkan) | Compatibility (GL) | WebGPU (Chrome, 4080S, Forward+) |
|---|---|---|---|---|
| Default render vs pre-change editor (ramp, step) | 0 px differ | 0 px | 0 px | = native, 0 px |
| Zebra 95: fraction above level / 2x stripe coverage | 0.0470 / 0.0471, 0 stray px, 0 column mismatch | 0.0478 / 0.0479, 0 / 0 | 0.0204 / 0.0212, 1 boundary column* | 0.0470 / 0.0468, 0 / 0 |
| Zebra 2 (70 +-5 window) | 0.1027 / 0.1026, 0 / 0 | 0.1034 / 0.1035 | 0.0556 / 0.0550, 1 boundary column* | 0.1027 / 0.1028 |
| False color vs ARRI table on the ramp | 100% within 2/255 | 100% | 99.14%, every miss < 0.15 IRE from a band edge* | pixel-identical to native |
| Histogram bar heights vs CPU histogram (luma; RGB) | 640/640 and 1920/1920 columns exact | exact | not supported (documented) | exact; pixel-identical to native |
| Focus peaking vs CPU Sobel, same threshold | IoU 1.000; 23690 px marked in focus, 0 behind DOF | IoU 1.000; 0 behind DOF | IoU 0.997 (no DOF in Compatibility) | IoU 1.000; identical to native |
| Game run, overlays on, Visible in Output off | 0 px differ from no overlays | 0 px | 0 px | 0 px |
| Editor-hint run (`--editor -s`, SubViewport attached to the camera as the inspector preview does), Visible in Output off | overlays drawn, false color 100% vs table | same | same (99.14%*) | n/a |

\* Compatibility's render target holds more than 8 bits, so pixels within one 8-bit step of a threshold read back on
the other side; every miss is within 0.15 IRE of a band edge (one code value = 0.39 IRE).

Noise reduction, digital grain at ISO 6400 (`grain_intensity` 4 so the noise is well above 8-bit quantization),
1080p, flat 0.5 patch; the 0.35|0.65 step for edge preservation:

| NR strength | 0 | 0.5 | 1 | 2 | 4 | 1, chroma only |
|---|---|---|---|---|---|---|
| luma sigma (8-bit), Forward+ | 5.60 | 3.15 | 1.95 | 0.98 | 0.66 | 5.60 (untouched) |
| Cb sigma (8-bit), Forward+ | 5.90 | 2.21 | 1.43 | 0.92 | 0.67 | 1.44 |
| step 10-90% width (px) | 0 | 0 | 0 | 0 | 4 (deliberate smear at max) | 0 |
| Compatibility luma / Cb sigma | 7.16 / 7.87 | 5.00 / 4.26 | 3.27 / 2.30 | 1.45 / 1.27 | 0.86 / 0.91 | 7.17 / 2.30 |

Mobile matches Forward+ within 0.003. WebGPU at 720p: luma sigma 3.64 -> 1.28 at strength 1, chroma-only leaves luma
at 3.63; pixel-identical to native Forward+ at 720p. (Compatibility's grain is that renderer's own, hence the different
starting sigma.)

Sharpening on the step; the numpy reference is a dense-Gaussian unsharp mask with the same 3x3 halo clamp, clipped to
[0,1]:

| Case (1080p, radius 2 -> sigma 2 px) | overshoot / ref | halo width px / ref | max error |
|---|---|---|---|
| amount 0.5, halo 1 | 0.195 / 0.200 | 2 / 2 | 0.63/255 |
| amount 1, halo 1 | 0.403 / 0.400 | 2 / 2 | 0.27/255 |
| amount 2, halo 1 | 0.792 / 0.801 | 2 / 2 | 0.64/255 |
| amount 1, halo 0.25 | 0.247 / 0.250 | 2 / 2 | 0.27/255 |
| amount 1, halo 0 | 0 / 0 | 0 / 0 | 0 |
| amount 1, halo 1 at **2160p** (sigma 4 px) | 0.403 / 0.450 | **4 / 4** (doubles with resolution) | 3.8/255 |

At 2160p the taps stride 2 px (bilinear, at most 9x9), so the peak is lower than a dense Gaussian's; the output
overshoot is the same at both resolutions, which is the frame-relative behavior wanted. Mobile is identical;
Compatibility within 0.8/255. WebGPU step and sharpened step are pixel-identical to native.

WebGPU: exported with the worktree's `template_debug` nothreads WebGPU template; system Chrome, headed, adapter
nvidia/lovelace, maxSampledTexturesPerShaderStage 48 (Forward+). The frame is read back in-engine from a SubViewport
(the composited canvas screenshot is not the RT's values: it was 2/255 off on the patches). Every case except the
zebras is pixel-identical to native Forward+; the zebras differ only by stripe phase (they crawl with time) and their
coverage matches exactly.

Smoke (`webgpu_notes/work/smoke.sh`, final run):
```
[WGSL Precompile] Results: 280 compiled, 0 glsl failures, 0 conversion failures
identical interface: 280/280, mismatched: 0, missing in candidate: 0, not in reference: 0
against the stored reference: 560/560 identical, 0 differ, 0 missing, 0 not in reference
validation: 280 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261028-085935-297489)
```

Screenshots (`screenshots/`): zebras 95, zebra 2 at 70, false color, luma and RGB histograms, focus peaking, all
overlays together (all WebGPU), false color in Compatibility, NR off vs strength 1 and sharpening off vs on (3x crops).

## What the lead must run

- After merging: regenerate the interface references again (`compare_interface.py --write`, `WRITE_REFERENCE=1
  run_tests.sh`); mine adds 4 modules only.
- `./webgpu_tests/local_ci.sh --dev-mode` was **not** run (werror). My incremental builds are warning-free in the
  camera finish files (one `-Wenum-conversion` in the GLES3 file was fixed), but the first full build's log was lost
  from the shared scratchpad, so warnings in the other touched files were not re-checked.
- `verify/matrix.sh <renderer> <prefix>` re-runs every native check (paths inside point at my scratch folder and
  worktree; adjust `W`/`E`). `verify/capture.mjs` + `verify/proj` (export with its Web preset) for the web side.
- The real editor UI was not driven (3D viewport Preview checkbox, Camera3D inspector preview); the editor-hint case
  was checked with `--editor -s` rendering through a SubViewport attached to the camera. A by-eye look in the editor
  is worth doing.

## Risks / notes

- Merge conflicts: shared files (`rendering_server*.h/.cpp`, `camera_attributes*`, `camera_3d*`,
  `renderer_scene_render_rd.cpp`, `rasterizer_scene_gles3.cpp`) are touched in my own blocks; the RS and
  CameraAttributes additions sit before the grain/LUT blocks rather than after them, away from where tasks 33-36 append.
- Cost when on: one RT copy plus one full-screen pass per enabled stage; NR and sharpening use up to 9x9 taps (81
  fetches) per pixel; the histogram is one compute dispatch over the frame plus a 256-thread pass. Off = zero.
- Histogram bars are normalized to the tallest bin, so a frame dominated by one value (a black background) shows the
  rest as tiny bars, as on many cameras; a log scale would be a follow-up.
- Single-view only: skipped in XR/multiview. NR/sharpening are skipped in debug draw modes and reflection probes.
- With HDR 2D the pass works on sRGB-encoded values and converts back; with HDR output (values > 1) the overlays
  measure the clamped signal.
- Scratchpad: before the lead's "use w37/" note I created `<scratchpad>/proj/` without noticing it already held
  another session's files. I overwrote its `project.godot` and `main.tscn` and deleted `grain.gd` (probably the grain
  verification project of TASKS.md 4.47, already committed). Everything of mine is now under `<scratchpad>/w37/`.
