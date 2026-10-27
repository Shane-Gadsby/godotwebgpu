# 38-camera3d-af-stabilization: autofocus and stabilization / handheld shake on Camera3D

State: ready-for-integration
Owner: worker-38
Claimed: 2026-10-28 08:18
Needs: build
Branch: work/38
Results: webgpu_notes/work/results/38-camera3d-af-stabilization/

Read `webgpu_notes/work/camera-batch.md` first.

## Goal

Camera3D, scene side (no renderer changes expected):
1. **Autofocus**: modes Manual (off, default), Single (AF-S: focus once when triggered, method `autofocus_trigger()`),
   Continuous (AF-C: keep focusing). Focus point: a target `Node3D` (tracking), or a normalized screen point (default
   center) measured with a physics ray cast (like phase/laser AF measuring distance; collision mask property), or the
   nearest of several points. Focus speed (diopters/s or a time constant) with optional hunting/overshoot for a
   contrast-detect look. It drives the camera's attributes focus distance: `CameraAttributesPhysical.frustum_focus_distance`;
   for CameraAttributesPractical, move the far/near DOF distances around the focus distance. Works in the editor
   preview when enabled. Signals (focus acquired).
2. **Stabilization / handheld shake**: procedural handheld motion (rotation and position), frequency-shaped noise with
   amplitude/frequency per axis and a "breathing" low-frequency component; an IS strength that removes a fraction of
   the high-frequency part (as optical/IBIS stabilization does); scale with focal length optionally (longer lenses show
   more shake). Applied as an offset that does not change the node's own transform (so gameplay code keeps working),
   in `_process` (or the camera's transform update), deterministic with a seed.

## Verification

GDScript tests run headless on the native editor (`--headless --path <proj> -s test.gd`): AF converges to the target
distance within the configured time constant; single vs continuous behavior; ray-cast point hits; Practical mode moves
DOF distances. Shake: RMS amplitude and spectrum (FFT of the offset over N seconds) match the settings; IS strength
reduces the high band by the set fraction; the node's own transform is untouched. Doc XML for all of it. Smoke
(no shader changes expected, but run it).
