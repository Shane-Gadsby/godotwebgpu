# Task 33: lens optics and the CameraEffects uniform buffer: SUMMARY

Branch `work/33` on 2d1865d69c. Worker-33, 2026-10-28.

## What changed

**CameraEffects uniform buffer** (the thing tasks 35 and 36 build on):
- Layout (std140, 512 bytes), declared in three places that must stay in step:
  `CameraEffectsUBO` in `servers/rendering/storage/camera_attributes_storage.h` (static_assert on the size),
  `servers/rendering/renderer_rd/shaders/effects/camera_effects_inc.glsl`, `drivers/gles3/shaders/effects/camera_effects_inc.glsl`.

  ```
  uvec4 header;      // x: lens bits (task 33), y: glare bits (task 35), z: exposure-automation bits (task 36), w: free
  vec4 lens[8];      // task 33, documented at the top of camera_lens_inc.glsl
  vec4 glare[6];     // task 35
  vec4 exposure[2];  // task 36
  vec4 reserved[15]; // free; grow a region into it, never reorder
  ```
- Bound at: Forward+ `tonemap.glsl` **set 1 binding 1** (beside the exposure texture; set 3 is shared with
  push-constant emulation on WebGPU), Mobile `tonemap_mobile.glsl` **set 0 binding 5**, Compatibility `post.glsl`
  UBO binding **16** (`CAMERA_EFFECTS_UNIFORM_LOCATION`, used by nothing else) behind the `USE_CAMERA_EFFECTS`
  specialization (so its default shader is unchanged).
- Filled on the CPU per tonemap pass by `RendererCameraAttributes::camera_attributes_fill_effects_buffer(rid,
  CameraEffectsContext, CameraEffectsUBO&)`: one entry point that calls `_fill_effects_lens()`,
  `_fill_effects_glare()` and `_fill_effects_exposure()` (the last two are empty stubs). It returns false (buffer
  all-zero) when every header word is 0. `CameraEffectsContext` carries the pass's source size, output size and
  frame number; **35/36 add members there** (e.g. the Environment RID for glare), not parameters.
- `camera_attributes_uses_effects_buffer(rid)` is the cheap "is anything on" test (currently the lens bits). **35/36
  must OR their own requested bits into it**: Mobile uses it to turn off the subpass post-process and Compatibility
  to force the post pass.
- Ownership (RD): `ToneMapper::get_camera_effects_buffer(render_buffers, attributes, context)`. Each viewport's
  `RenderSceneBuffersRD` gets its own buffer (`CameraEffectsBufferRD` custom data "camera_effects"), uploaded with
  `buffer_update` only when its bytes change (memcmp against a CPU copy); passes with nothing on bind a shared
  all-zero buffer owned by ToneMapper. Called from `_render_buffers_post_process_and_tonemap()` before the draw list
  begins, so several viewports per frame never share a changing buffer. The Mobile subpass path always binds the
  zero buffer (no update inside the render pass), which is why the subpass is skipped when the buffer is in use.
  Compatibility: `scene_state.camera_effects_buffer`, uploaded in `_render_post_processing()`.
- Every effect sits behind a uniform branch on its header word: zero header = bit-identical (measured below).

**How 35/36 extend it:** fill your region and header word in your stub; add your bits to
`camera_attributes_uses_effects_buffer()`; write `camera_<feature>_inc.glsl` reading `camera_effects.glare[]` /
`.exposure[]` behind `camera_effects.header.y/z != 0u`, included after `camera_effects_inc.glsl` in tonemap.glsl,
tonemap_mobile.glsl and the Compatibility post.glsl (which includes the buffer under `USE_CAMERA_EFFECTS`; set that
bit for your effect too). Task 35's oval bokeh / starbursts can read the lens: `lens[0].w` = anamorphic squeeze,
`lens[6]` = sensor width, height (mm), focal length (mm, with breathing), f-number (zeros without a physical lens),
`lens[0].xy` = frame half extents (corner at |q| = 1). The lens region is filled whenever there is a physical lens,
even with no lens bit set. The DOF pass is a different pass: bind the same per-viewport buffer there
(`rb->get_custom_data("camera_effects")`) rather than duplicating values. A new binding means regenerating the
interface references (`compare_interface.py --write`, `WRITE_REFERENCE=1 run_tests.sh`).

**Lens optics** (all off by default):
- CameraAttributes (both kinds): `vignetting_*` (enabled, strength, falloff [Practical]), `distortion_*` (enabled,
  automatic [Physical], k1, k2, auto_crop), `chromatic_aberration_*` (enabled, strength).
- CameraAttributesPhysical: `sensor_format` + `sensor_size` (replaces the hard-coded 36x24 in `_update_frustum()`:
  FOV and CoC = diagonal / 1500; grain now uses the sensor height instead of 24 mm), `lens_max_aperture`,
  `lens_transmission_loss` (T-stop), `lens_anamorphic_squeeze`, `lens_focus_breathing`, `exposure_nd_filter`,
  `diffraction_*`, `is_fov_horizontal()`, static `get_sensor_format_size()`.
- Camera3D: keeps the horizontal FOV (KEEP_WIDTH, and tells the RenderingServer) for an anamorphic lens.
- RenderingServer: `camera_attributes_set_lens / _set_vignetting / _set_lens_distortion /
  _set_chromatic_aberration / _set_diffraction`.
- Shader `camera_lens_inc.glsl` (+ Compatibility copy): the source is resampled at the pass's first read (distortion by
  Newton inversion of per-axis Brown-Conrady; lateral CA as per-channel magnification; diffraction as a 5x5 Gaussian
  whose tap spacing the CPU solves for the true kernel variance including bilinear blending); FXAA searches around the
  lens-mapped point and its final read is redone through the lens when CA/diffraction is on; glow is read at the
  lens-mapped point (glow map stays in screen space); vignetting multiplies after pre-tonemap glow; all before white
  balance, grain, tonemapping and the LUT.
- Models and sources: comment block above `_fill_effects_lens()` (cos^4 law: Kingslake / Ray; optical vignetting as
  pupil/barrel circle overlap, gone two stops down: Ray 6.3; Airy 1.22 lambda N: Born & Wolf; Gaussian sigma =
  0.42 lambda N: Zhang et al. 2007; Imatest's 0.1% lateral CA; automatic distortion is a stated look-choice fit to
  published lens tests; thin-lens breathing and working f-number).
- Class reference XML for everything (doctool, purely additive).
- Interface references regenerated: the only change is `(1, 2, '<uniform>')` in the four tonemap fragment variants
  and `(0, 10, '<uniform>')` in the two tonemap_mobile ones (bindings after combined-sampler splitting);
  scan_reference renumbered around them.

## Verification (numbers in `numbers.txt`, FOV in `fov.json`, scripts in `tools/`)

References are computed independently in Python from the optics formulas in sensor millimeters
(`tools/analyze.py`), not from the shader's frame units. Native: xvfb, RTX 4080 SUPER, Vulkan. 8-bit sRGB output,
linear tonemapper, decoded with the sRGB EOTF.

- **Bit-identical default**: base editor (2d1865d69c, built in this worktree) vs work/33, scene with sky, shadows,
  glow and Filmic; Physical / Practical / no attributes: **max diff 0, 0 differing pixels in all 9 renders**
  (Forward+, Mobile, Compatibility).
- **Identity resample** (CA strength 1e-4 runs the whole resample path): max diff 0 vs off in all three renderers
  (Compatibility after the exact re-encode fix in commit 3; before it, its fast-sRGB round trip was off by up to
  6.8/255 near black).
- **FOV per sensor preset** (50 mm): all 7 formats within 2e-6 deg of 2 atan(h/2f) (FF 26.99147, APS-C 17.73335,
  S35 21.13966, M43 14.81382, MF 36.52578, 1-inch 10.05815, custom 30x20 22.61986); anamorphic 2x Super 35:
  horizontal 52.92824 = 2 atan(2*24.89/100), KEEP_WIDTH; breathing at 0.5 m: 1.0 -> 24.37726 (expected 24.37727),
  -0.5 -> 28.51614 (28.51615).
- **Vignetting** vs cos^4 x numerically integrated pupil overlap, 11 points along the diagonal (24 mm FF, 1080p):
  f/1.4 corner 0.1560 measured vs 0.1552 expected (-2.68 stops); f/5.6 (optical gone) 0.2416 vs 0.2406 (cos^4 only);
  max |error| 0.004-0.014 in all three renderers (one 8-bit code at the 0.5 background is ~0.009). Practical (1.5
  stops, falloff 3): corner 0.359 vs 0.354.
- **Distortion**: 135 dots, centroids vs Brown-Conrady forward of the undistorted dots. Barrel k1 -0.1, k2 0.02 with
  auto-crop (crop 0.9200, found independently): max error **0.035 px** Forward+/Mobile, 0.12 px Compatibility, dots
  moving up to 34 px; without crop 0.061 / 0.12 px; pincushion k1 0.08: 0.034 / 0.094 px.
- **Chromatic aberration** strength 5: red-blue spread / radius **0.005000** at 1080p and **0.005001** at 540p
  (expected 0.005), i.e. the same fraction of the frame at both (3.79 px vs 1.89 px at the outermost dot);
  Compatibility 0.00502.
- **Diffraction**, Micro Four Thirds, f/64 (working f/64.2), edge spread function: 4K (6.02 um pixels) sigma 2.455 px
  vs 2.465 px expected (0.42 lambda N / pitch; the Airy diameter 2.44 lambda N is 14.3 px); 1080p 1.279 vs 1.2325
  (the step edge alone measures 0.31 px; in quadrature 1.24). Compatibility 2.34 / 1.30 px (its bilinear filter
  blends sRGB-encoded values, ~5% narrower; documented).
- **WebGPU** (system Chrome, headed, Vulkan; adapter nvidia/lovelace, maxSampledTexturesPerShaderStage 48, engine log
  "WebGPU 1.0 - Forward+"): 8 cases at 1280x720 (both vignettes, dots off/barrel, CA, edge off/f/64, default scene)
  vs native Vulkan Forward+: **pixel-identical** (max diff 0; default scene max 1, 0 pixels above 2). The web numbers
  repeat the native ones (barrel 0.046 px, CA 0.0049997, vignette max |err| 0.014).
- **Smoke** (final tree):
  ```
  [WGSL Precompile] Results: 276 compiled, 0 glsl failures, 0 conversion failures
  identical interface: 276/276, mismatched: 0, missing in candidate: 0, not in reference: 0
  against the stored reference: 552/552 identical, 0 differ, 0 missing, 0 not in reference
  validation: 276 modules, 0 invalid, 0 of them expected
  SMOKE OK (smoke-20261028-092951-388684)
  ```

Not verified: ND / T-stop on screen (the factor is `e *= 2^(nd + loss)` in `calculate_exposure_normalization()`,
not script-bound; skipped at 0, so the bit-identical run covers the default), MSAA, XR/multiview, FSR/scaled
viewports, a `dev_mode=yes` (werror) build.

## What the lead must run

- After merging: regenerate the interface references again (34-38 may add bindings), then smoke.
- `./webgpu_tests/local_ci.sh --dev-mode` (I built without werror).
- Optionally rerun the checks on the merged tree: `tools/render_all.sh <editor> <out> default vig dist ca diff`
  (expects `tools/lens_test.gd` in a scratch project at the path in its header) and `tools/analyze.py`.

## Risks

- Mobile turns off the subpass post-process whenever a lens effect is on (a cost like glow's).
- Camera3D now calls `camera_set_use_vertical_aspect` when physical attributes change the keep-aspect mode; before,
  `_attributes_changed()` set the member only. Only reachable with an anamorphic lens or a previously stale KEEP_WIDTH.
- Diffraction with sigma above ~2 texels samples taps more than a texel apart; fine detail can alias slightly.
- Distortion/CA/diffraction see only the tonemap pass's input: DOF, SSR and compositor effects run on the undistorted
  image (as on a real sensor); SMAA runs after the pass, on the distorted image.
- Two deviations from the task text, both as questions in `for-tasks.md`: focus breathing defaults to 0 (the
  bit-identical rule beats "default a typical stills lens"), and lateral CA does not depend on the aperture (physics).
