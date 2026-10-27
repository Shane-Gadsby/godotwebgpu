# 34-camera-motion: motion blur from the shutter, rolling shutter (worker-34)

## What changed (commit on work/34)

- **CameraAttributes** (shared base): `motion_blur_enabled` (off), `motion_blur_shutter_angle` (180°),
  `motion_blur_strength` (1), `motion_blur_max_length` (0.05 of frame height); `rolling_shutter_enabled` (off),
  `rolling_shutter_readout_time` (20 ms). **CameraAttributesPhysical** `motion_blur_use_shutter_speed` (default on):
  exposure time = 1 / `exposure_shutter_speed`, with or without physical light units (the shutter speed is then shown
  in the inspector). Inspector groups "Motion Blur" / "Rolling Shutter" with group-enable check boxes.
- **RS**: `camera_attributes_set_motion_blur(rid, enable, shutter_angle, shutter_time, strength, max_length)`,
  `camera_attributes_set_rolling_shutter(rid, enable, readout_time)`, `camera_attributes_set_motion_blur_quality()`,
  enum `CameraAttributesMotionBlurQuality`; ProjectSettings `rendering/camera/motion_blur/motion_blur_quality`
  (Low 8 / Medium 16 / High 32 taps).
- **Storage** (`camera_attributes_storage.*`, own `/* Motion blur and rolling shutter */` block): the model, cited
  in the comment. Blur = a frame's motion x exposure time / frame time (frame time = the render step; 60 fps if the
  clock is stopped); rolling row y is at readout x (y/H - 0.5) from the frame's time.
- **New pass** `servers/rendering/renderer_rd/effects/camera_motion_blur.{h,cpp}` +
  `shaders/effects/camera_motion_blur.glsl` (own 128-byte push constant; tonemap push constant and CameraEffects UBO
  untouched): prep (velocity buffer + depth -> motion in output px + linear depth; sky gets camera motion from depth via
  the MotionVectorsStore reprojection) -> tile max (workgroup reduction, tile = max blur radius, so resolution-relative)
  -> 3x3 neighbor max -> gather (rolling shutter by fixed-point iteration, then McGuire 2012 tiles with Jimenez 2014
  tap weights) -> copy back. MSAA uses a `resolved_z_source` variant (BokehDOF's trick). RGBA16F everywhere (WebGPU
  storage-legal). Created on first use; nothing runs when off.
- **Wiring**: `renderer_scene_render_rd.cpp` runs it after DOF, before auto exposure/glow (Forward+ only: needs
  storage + velocity buffer). `render_forward_clustered.cpp` forces motion vectors (and their MSAA resolve) when on.
- **Web**: `wgsl_precompile.py` precompiles the 5 variants; shader baker advanced-group markers for the new property and
  RS names (`shader_baker_export_plugin.cpp`, message text, `EditorExportPlatformWeb.xml`).
- **Docs**: CameraAttributes, CameraAttributesPhysical, ProjectSettings, RenderingServer XML (doctool, validate-xml OK).
- Interface references: `interface_reference.json` +5 modules (`camera_motion_blur.glsl_{prep,prep_resolved_depth,
  tile_max,neighbor_max,gather}_comp.wgsl`, source line appended), `scan_reference.txt` +10 lines for the same
  modules. No existing module changed.

## Two filter fixes found by measurement (worth keeping)

- McGuire's cone/cylinder weights are not energy-exact: the moving edge stepped 0.39 -> 0.87 instead of ramping through
  0.5. Jimenez's weighting (behind x own spread + in front x tap spread, unclaimed shares to the center, fixed N) gives
  the exact box-shutter result.
- McGuire's tap spacing (i+1)/(S+1) shortens every blur to S/(S+1): measured 0.941 at S=16. Stratified (i+0.5+j)/S fixes it.

## Verification (numbers; scripts and raw JSON in this folder)

Reference: analytic. A 0.2H white square moving at known speed over black (linear tonemap) must equal the square
convolved with a box of length speed x exposure (`mb_analyze.py blur`: least-squares fit of L, amplitude, position).
A camera yawing at ω under fov 60 must shift a vertical line by ω f R (1 + x²/f²) from top row to bottom row
(`mb_analyze.py skew`, f = (H/2)/tan 30°).

Native Forward+ (Vulkan, 4080 SUPER), `native_blur.jsonl`, `native_skew.jsonl`:

| case | H | expected L px | fitted L | ratio | fit rms | L/H |
|---|---|---|---|---|---|---|
| Practical 180°, 60 fps | 720 | 18.0 | 17.98 | 0.999 | 0.0003 | 0.0250 |
| Practical 180° | 1080 | 27.0 | 27.00 | 1.000 | 0.0003 | 0.0250 |
| Physical 1/50 s | 720 | 43.2 | 43.08 | 0.997 | 0.0004 | 0.0598 |
| Physical 1/50 s | 1080 | 64.8 | 64.78 | 1.000 | 0.0004 | 0.0600 |
| Physical 1/2000 s | 720 / 1080 | 1.08 / 1.62 | 1.04 / 1.46 | sub-2 px | | |
| 1/50 s + MSAA 4x | 720 | 43.2 | 43.08 | 0.997 | | |
| 1/50 s + TAA | 720 | 43.2 | 42.54 | 0.985 | 0.010 (TAA) | |

| rolling 30 ms, ω 2 rad/s | H | expected px | measured | ratio | line fit rms |
|---|---|---|---|---|---|
| geometry bars | 720 | 41.46 | 41.00 | 0.989 | 0.03 px |
| geometry bars | 1080 | 62.17 | 61.46 | 0.989 | 0.03 px |
| sky stripes (camera motion from depth) | 720 | 38.12 | 37.95 | 0.996 | 0.03 px |
| sky stripes | 1080 | 57.19 | 56.94 | 0.996 | 0.07 px |
| sky + MSAA 4x | 720 | 38.12 | 37.95 | 0.996 | |

Direction: a right pan shifts the top rows right (earlier rows saw the scene before the camera turned), as on real CMOS.

WebGPU, system Chrome headed, adapter nvidia/lovelace, maxSampledTexturesPerShaderStage 48, 165 Hz (delta 6.06 ms,
so it also checks frame-rate independence of the shutter-speed mode), `web_results.jsonl`, 0 console errors:

| case | expected | measured | ratio | native same case |
|---|---|---|---|---|
| Physical 1/20 s, 10 u/s | 36.0 px | 36.00 | 1.000 | 35.98 (0.999) at 60 fps |
| same + MSAA 4x (resolved-depth variant) | 36.0 | 35.94 | 0.998 | |
| rolling, geometry | 37.84 | 37.88 | 1.001 | |
| rolling, sky | 39.83 | 39.69 | 0.997 | |
| rolling, sky + MSAA 4x | 38.69 | 38.76 | 1.002 | |

Default off, bit-identical: the pre-change editor (base 2d1865d69c, built in a scratch worktree) and this one render
the same frame with 0 differing pixels: Forward+ (lit look scene, blur scene, sky scene, bar scene), Mobile and
Compatibility (lit look scene).

Mobile and Compatibility with the effect on: image unchanged, no errors (documented as Forward+ only).

GPU cost, in-process A/B (effect toggled every 50 frames, 1920x1080 lit scene; `timing_ab.txt`). The GPU stayed at
210 MHz of 3150 under this light load, so absolute times are inflated; TAA measured the same way is the yardstick:
TAA +3.01 ms; motion blur Low +4.07, Medium +4.25, High +6.24; blur + rolling +5.14; rolling only +2.62 (each includes
the motion vectors it turns on). So Medium is about 1.4x TAA's cost. Most of it is the fixed passes (prep, tiles, gather,
copy), not the taps; merging prep into the tile pass and dropping the copy are the obvious optimizations if needed.

Smoke (`webgpu_notes/work/smoke.sh`), last lines:

```
[WGSL Precompile] Results: 281 compiled, 0 glsl failures, 0 conversion failures
identical interface: 281/281, mismatched: 0, missing in candidate: 0, not in reference: 0
against the stored reference: 562/562 identical, 0 differ, 0 missing, 0 not in reference
validation: 281 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261028-092438-352449)
```

Look: `look_montage.jpg` (off / 180° / 360° + rolling 30 ms, native), `look_off_vs_blur_rolling.gif` (sequence, off
left, 360° + rolling right), `web_look_chrome_webgpu.jpg` (WebGPU, Chrome).

## What the lead must run after merging

- Rebuild editor + web templates; `smoke.sh`, and regenerate the interface/scan references after merging all camera
  branches (mine add 5 modules only).
- Optionally re-run `web_suite.sh` / `mb_analyze.py` (the scene is `mbproj/`, export with a custom_template/debug preset).
- `./webgpu_tests/local_ci.sh --dev-mode`: I did not build with `dev_mode=yes` (werror).

## Risks

- Merge conflicts: shared files (`camera_attributes.*`, storage, RS headers, `renderer_scene_render_rd.cpp` right after
  the DOF block, `render_forward_clustered.cpp` motion-vector condition, `wgsl_precompile.py`, baker markers); every
  addition is its own block.
- Enabling it turns on Forward+'s motion-vector (advanced) pipelines: first use compiles them, and web exports bake
  them only if the scan sees the property (markers added).
- Known screen-space limits (documented): transparent objects, crossing motions in one tile neighborhood, disocclusion,
  rotation blurs along straight lines. TAA smears the blur's noise slightly (fit rms 0.01).
- Editor viewport shows the blur while navigating when the WorldEnvironment's attributes have it on (as DOF does);
  question in for-tasks.md.
- I removed my scratch baseline worktree (`../godotwebgpu-34base`, detached, untracked build output) with
  `git worktree remove --force`; no branch was involved.
