# Task 39: zebra colors, editor tick boxes for motion blur / rolling shutter and handheld shake, shake presets

Branch `work/39` (on webgpu-4.8 at d75b13c390). State: ready-for-integration.

## What changed

1. **Zebra colors** (Camera3D Monitoring > Zebras): `monitoring_zebras_color` (default black) and
   `monitoring_zebras_second_color` (default white), display colors, shown only while their zebra is on. New RS
   `camera_set_monitoring_zebra_colors(camera, color, second_color)` (sibling of `camera_set_monitoring_zebras`, whose
   signature is unchanged) -> `RendererSceneRender::CameraMonitoring::zebras_color / zebras_second_color`.
   RD `camera_finish.glsl`: two `vec4` at the end of the push constant, 96 -> 128 bytes (the Vulkan guaranteed
   minimum; `static_assert` added; task 37's comment said 112, it was 96). GLES3 `camera_finish.glsl`: two `vec3`
   uniforms. `camera_finish_inc.glsl` (both copies) did not need a change.
2. **Motion blur / rolling shutter in the editor**: CameraAttributes `motion_blur_visible_in_editor` and
   `rolling_shutter_visible_in_editor`, default off, one in each group (a group's enable check box hides its other
   properties, so a single shared box would be unreachable when only the other effect is on). The resource sends the
   effect as off under `Engine::is_editor_hint()` unless ticked (`_camera_motion_shows()`), like Visible in Output.
3. **Shake in the editor**: Camera3D `shake_visible_in_editor`, default off. `_shake_active()` gates the internal
   process, the offset in `_get_adjusted_camera_transform()` and `get_shake_offset()`. Autofocus unchanged.
4. **Shake presets** (added by the lead): Camera3D `shake_preset`, enum `ShakePreset` = Custom (default), Handheld,
   Handheld Stabilized, Handheld Walking, Running, Shoulder Rig, Gimbal, Tripod in Wind, Vehicle Mount. Picking one
   writes all shake settings except the seed; any shake setter that leaves the values different from the preset's flips
   it to Custom (and notifies the inspector); a value equal to the preset's keeps it, so a saved scene loads with its
   preset. Values and sources (CIPA DC-X011, Prieto 1996 postural sway, Pachi and Ji 2005 walking, Hirasaki 1999 head
   motion, Cavanagh and Kram 1989 running, Gillespie vehicle dynamics; look choices marked) in the
   `/* Handheld shake presets */` comment in `scene/3d/camera_3d.cpp`; table in for-docs.md.
- Class reference: Camera3D, CameraAttributes, RenderingServer. Doctool re-ordered existing entries (the merged XML was
  not in doctool order), so I kept the existing order and inserted only my entries, to keep merges with 35/36 small.

## Verification (`tests/run.sh`, log `verify_log.txt`; native, Xvfb, 4080 SUPER, under the gpu lease)

- **Defaults pixel-identical** to the pre-change editor (same tree at 005bda555e, built first in this worktree) in
  Forward+, Mobile, Compatibility: ramp and step scenes 0 px differ; both zebras on with default colors 0 px differ
  (fixed 60 fps, so the crawl phase matches).
- **Zebra colors, numeric** (all three renderers): stripes = pixels where default-color zebras differ from no overlay;
  with zebra 1 set to red every stripe pixel is (255,0,0) exactly, with zebra 2 set to (0,0.5,1) every stripe pixel is
  (0,128,255) within 1/255; stripe masks identical (0 px differ); every other pixel equals the no-overlay frame.
  Stripe px zebra 1 / 2: Forward+ 32900 / 50446, Mobile 33151 / 50698, Compatibility 24614 / 35054.
- **Tick boxes**, through a SubViewport (as the previews render), game run vs editor-hint run (`--editor -s`), Forward+:
  motion blur unticked in the editor 0 px differ from blur off; ticked in the editor and either way in the game 1240 px
  differ, 1123 gray edge px (identical frames in all three). Rolling shutter: editor unticked 0 px differ, skew 0;
  otherwise 25799 px differ, bars skewed 24.5 px top to bottom. Shake: editor unticked 1 distinct offset / camera
  transform over 24 frames; ticked and game 22-24. Windowed editor under Xvfb (`tests/edproj`): ticked camera 192
  distinct offsets, unticked 1 (identity); the same scene run as a game: both 208.
- **Presets** (`test_presets.gd` + `presets_analyze.py`): every preset writes its documented values; the seed does not
  flip it; each of the 8 settings edited flips to Custom with the value kept, the same value keeps the preset; choosing
  Custom changes nothing; save/load keeps Gimbal and Tripod in Wind, an edited preset loads as Custom. 120 s at 60 fps
  per preset: per-axis RMS within 5 % of the table (times the stabilizer's gain for Handheld Stabilized), breathing
  within 12 %, band centroids within 10 % (worst 2.4 %); e.g. Walking pitch 0.798/0.8 deg at 1.99/2 Hz, y 0.01202/0.012
  m at 2.02/2 Hz; Stabilized pitch 0.0256 vs 0.0258 (about a fifth of Handheld's 0.12).
- **Task 38's tests/run.sh**: `TASK 38 VERIFY: OK` (pixel identity 0 in all three renderers) with
  `shake_visible_in_editor = true` added to its `edproj/main.tscn`. Unmodified, exactly one line fails, by design:
  "editor: shake offset changed over frames (1 distinct values)". The lead should add that line to task 38's
  `tests/edproj/main.tscn` (I did not write into another task's folder). `tests/verify_with_leases.sh` runs all three.
- `pre-commit` on the changed files: clean. Build: no warnings in the changed files (not a `dev_mode=yes` build).
- Smoke (`smoke_tail.txt`), interface and text-scan references unchanged (push-constant members are not in them):

```
identical interface: 285/285, mismatched: 0, missing in candidate: 0, not in reference: 0
against the stored reference: 570/570 identical, 0 differ, 0 missing, 0 not in reference
validation: 285 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261028-100138-475963)
```

## What the lead must run

- `BASE=<pre-merge editor> tests/run.sh <editor> [scratch]` on the merged tree (Xvfb + GPU, ~10 min, mostly the 120 s
  preset recording); task 38's `tests/run.sh` after adding `shake_visible_in_editor = true` to its edproj.
- Web: not exported here. Zebra colors go through the camera finish push constant (WebGPU emulates it in a uniform
  buffer) and smoke compiled the changed shader in Chrome; the other changes are scene-side and renderer-independent.
  A look at zebra colors on the next web export is enough.
- `local_ci.sh --dev-mode` was not run.

## Risks

- The camera finish push constant is now 128 bytes, the most every Vulkan device must support: a further field needs
  packing or a uniform buffer.
- Merge: shared files touched only in my own blocks (`camera_3d.*` monitoring and shake blocks; `camera_attributes.*`
  motion blur / rolling shutter blocks; RS headers one line beside the zebras method).
- A preset's values are saved with the scene, so changing a preset's numbers later turns existing scenes using it into
  Custom on load (their saved values win), the safe direction.
- A ticked `shake_visible_in_editor` keeps the camera's internal process on in the editor, as task 38's shake did.
