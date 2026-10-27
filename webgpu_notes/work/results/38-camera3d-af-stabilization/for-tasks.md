# For TASKS.md (§4, camera realism batch)

**Camera3D autofocus and handheld shake (task 38, 2026-10-28).** Scene-side only (`scene/3d/camera_3d.*`, no renderer
change). Autofocus: Manual / Single (`autofocus_trigger()`) / Continuous, on a tracked Node3D or the nearest physics-ray
hit of normalized screen points (collision mask), servo in diopters with a time constant (exact per-frame solution,
frame-rate independent) and optional hunting overshoot; drives `CameraAttributesPhysical.frustum_focus_distance` or
moves CameraAttributesPractical's near/far distances and transitions keeping the band's diopter width; signals
`focus_acquired` (within DOF/4) / `focus_failed`; runs in the editor preview with editor-save hooks that store the
user's value. Shake: seeded band-limited sinusoid sums per axis (rotation, position, breathing), exact RMS and spectrum,
IS = 1 - s * first-order 0.5 Hz high-pass per partial, optional FOV-referenced amplitudes; an offset in
`_get_adjusted_camera_transform` so `transform` stays untouched while `get_camera_transform()`/picking include it.
Verified by `webgpu_notes/work/results/38-camera3d-af-stabilization/tests/run.sh` (numbers in that SUMMARY.md).

Possible §3 trap (cost ~15 min here): `--fixed-fps N` with N != physics ticks per second does not give a constant
process delta (e.g. 144 alternates ~1/120 and ~1/150), and `await process_frame` in a `-s` script resumes before nodes
process. A convergence test must integrate the deltas the node actually used (record from a node with a higher
`process_priority`), not assume k/N.

Also: the session scratchpad is shared between the batch's workers (same session id). Each worker should keep its
files under its own subfolder; worker-38 briefly ran another worker's `build.sh` (same path) by mistake, which only
re-took that worker's own lease, and overwrote the `project.godot` in the shared `scratchpad/proj` (its grain test
project, now restored as a minimal file next to `grain.gd`).

## Questions for the user

- Should handheld shake also run in the editor's camera preview (current default: yes, like autofocus), or only in
  the running game? Default kept: yes.
