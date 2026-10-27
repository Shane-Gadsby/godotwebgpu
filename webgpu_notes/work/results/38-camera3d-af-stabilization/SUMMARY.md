# 38: Camera3D autofocus and handheld shake / stabilization (scene side)

Branch `work/38`. Files: `scene/3d/camera_3d.{h,cpp}` (new `/* Autofocus */` and `/* Handheld shake */` blocks, two
hooks in `_notification`, one line in `_get_adjusted_camera_transform`, one in `_update_process_mode`, one guard in
`set_attributes`), `doc/classes/Camera3D.xml`. No renderer, RenderingServer, shader or interface-reference changes.

## What it does

**Autofocus** (`autofocus_*` group): `autofocus_mode` Manual (default) / Single (AF-S, `autofocus_trigger()`) /
Continuous (AF-C). Subject: `autofocus_target` (a Node3D, distance along the optical axis) or `autofocus_points`
(normalized screen points, default center; one physics ray each through the inverse projection, so every projection
mode works; nearest hit wins; `autofocus_collision_mask`). Servo in diopters: `autofocus_time_constant` (exact
first-order solution per frame, frame-rate independent), `autofocus_hunting` (overshoot fraction: exact underdamped
second-order solution, damping from OS = exp(-pi z / sqrt(1-z^2)), envelope time constant kept equal to tau).
Drives `CameraAttributesPhysical.frustum_focus_distance`; for `CameraAttributesPractical` it moves near/far distances
*and* transitions together, keeping the in-focus band's width constant in diopters (DOF ~ +-1/H around 1/u). Signals
`focus_acquired(distance)` (error within a quarter of the DOF half width: 1/H from the resource's aperture/focal length
with the engine's own CoC, or the Practical band) and `focus_failed`. Also `get_autofocus_distance()`,
`is_autofocus_acquired()`. Runs in the editor too (camera preview); `NOTIFICATION_EDITOR_PRE_SAVE/POST_SAVE` put the
user's value back for the save and re-apply after it; a user edit while AF runs becomes the new baseline.

**Handheld shake** (`shake_*` group, off by default): per-axis rotation (deg RMS) and position (m RMS) amplitudes and
center frequencies, breathing pitch sway (deg RMS, Hz), `shake_seed`, `shake_stabilization` (0..1),
`shake_scale_with_focal_length`. Each axis is 12 seeded sinusoids, stratified log-uniform over f/2..2f (breathing
+-0.3 octave), phases advanced by the frame delta: exact RMS, deterministic per seed, frame-rate independent, no jump
when a frequency changes. IS multiplies each partial by 1 - s * HP(f), HP a first-order 0.5 Hz high-pass (the gyro
high-pass stabilizers use against fighting pans). Applied in `_get_adjusted_camera_transform` (camera-local, after
h/v offset): `get_camera_transform()`, picking and the RS camera include it; `transform`/`global_transform` never do.
`get_shake_offset()` returns it. With `scale_with_focal_length` off, angular amplitudes are referenced to a 50 mm
full-frame FOV (27 deg) and scaled by fov/27.

With both features off, Camera3D does no extra work (internal process not enabled; one bool test per transform).

## Verification (numbers)

`tests/run.sh <editor> [scratch]` reproduces all of it (`BASE=<pre-change editor>` adds pixel identity); last run:
`TASK 38 VERIFY: OK`, log in `out/run_sh.log`, per-test output in `out/`.

- AF first-order convergence (Physical, 10 m -> 2 m tracked node, tau 0.2 s), using the deltas the camera actually
  integrated: error / e0 at t = tau **0.3679** (e^-1) at 30, 60 and 144 fps; max deviation from e0 exp(-t/tau) over all
  frames **7.4e-8**; 95 % reached at 0.600 s (60 fps), 0.625 (30), 0.604 (144) vs 3 tau = 0.6 s.
- Hunting overshoot fraction set 0.1 / 0.3 / 0.5 -> measured **0.0999 / 0.2983 / 0.4962**; all settle at 2.0000 m.
- `focus_acquired` once, at error 0.082 D < tolerance 0.093 D (f = 35 mm, N = 16: H = 2.69 m); AF-C follows the
  target to 5 m and re-emits; AF-S does nothing before the trigger, focuses (2.0001 m), stays at 2.0000 m when the
  target moves to 6 m, re-trigger -> 5.9995 m.
- Ray points: center ray hits wall face at **3.5000 m**; points [center, (0.8, 0.5)] -> nearest post **1.5000 m**;
  post on layer 2 with mask 1 -> back to 3.5000 m; nothing in view -> focus held, `focus_failed` exactly once.
- Practical (near 2 / far 10, transitions 1 / 5) focused at 1 m: near 0.8333, far 1.2500, near transition 0.2451,
  far transition 0.0543 = the diopter-model reference to 1e-4; at 4 m: near 2.2222, far 20.000; at 50 m far blur
  pushed to 1e5 m (beyond "hyperfocal"); pre-save restores all four values exactly.
- Editor save hooks: pre-save 10.0000 (user value), post-save back to AF value; user edit 7.0 becomes the baseline.
- Shake, 120 s at 60 fps, numpy FFT: RMS within 0.6 % of the setting on all six axes; log power centroid within 0.7 %
  of the set frequency, >= 99.8 % of the energy within f/2..2f. IS 0.5 / 0.9 band amplitude ratio equals the model
  1 - s HP(f) to <= 0.0002 on every axis; for bands above 1.5 Hz: 0.5093 / 0.1173 (yaw, 3 Hz) and 0.5052 / 0.1096 (z,
  4 Hz) vs 1 - s = 0.5 / 0.1. Breathing alone: RMS 0.138 deg for 0.15 (narrow band: its partials only decorrelate
  over ~100 s, so a 120 s window reads ~8 % low), spectral peak 0.275 Hz for 0.25; other axes exactly 0.
  scale_with_focal_length off at half the reference FOV: rotation RMS ratio 0.5000, position 1.000000.
- Node transform bit-identical to an identical shake-off camera over 7200 frames; `get_camera_transform()` ==
  `global_transform * get_shake_offset()` every frame; same seed identical, other seed differs; disabling restores the
  plain transform.
- Windowed editor under Xvfb (`tests/edproj`, @tool autoload): in the editor, AF-C focused the edited camera at 1.9997 m,
  the shake offset changed every frame, node transform untouched; `EditorInterface.save_scene()` wrote the user's
  focus distance (10 m, the default, so omitted), and AF's value was re-applied after the save.
- Shake off renders pixel-identical (max diff 0) to the pre-change editor (lead tree's `bin/godot.linuxbsd.editor.x86_64`)
  in Forward+, Mobile and Compatibility; shake on changes the frame (mean diff 5.8/255) and is identical across runs.
- `pre-commit` on the changed files: clean. Build: 0 warnings in camera_3d.cpp (not a `dev_mode=yes` build).
- smoke.sh: `SMOKE OK (smoke-20261028-082657-140721)` -- 276 compiled, 0 conversion failures, interface 276/276,
  text-scan 552/552, Chrome 276 modules 0 invalid (nvidia/lovelace).

## What the lead must run

- `webgpu_notes/work/results/38-camera3d-af-stabilization/tests/run.sh bin/godot.linuxbsd.editor.x86_64` on the merged
  tree (BASE=<pre-merge editor> for the pixel check). No GPU lease needed (tiny Xvfb renders).
- A `dev_mode=yes` build (werror) was not run here.
- Web: no renderer or platform code changed, so I did not build or export a web template; the code is plain scene C++
  and runs identically there. A quick look on the next web export with shake on is enough.
- Merge: Camera3D files are untouched by tasks 33-37 as far as I know; `doc/classes/Camera3D.xml` gained only new entries.

## Risks / limits

- Autofocus writes to the attributes resource: cameras sharing one resource all get the focus (documented; use a
  per-camera resource). Switching AF to Manual keeps AF's last value, and in the editor that value is then saved
  (as on a camera; documented).
- Ray points use the direct space state from `_process`: not with `physics/3d/run_on_separate_thread` (documented;
  target-node AF still works). Rays are cast in the frame's state, so AF lags physics by at most a frame.
- Practical with only far blur needs a convention for the missing side (focus = half the far distance); documented.
- The shake runs in the editor preview as well; that keeps the camera's internal process on in the editor while enabled.
- Under physics interpolation the shake is pushed to the RenderingServer from `_process` with the interpolated
  transform; not separately tested with interpolation on.
- Default amplitudes are a look choice (source ranges cited in the code: CIPA DC-011 hand-shake band, 8-12 Hz tremor,
  0.2-0.33 Hz breathing).
