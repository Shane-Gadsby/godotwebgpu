# For TASKS.md (§4 archive entry) and README (engine additions)

## §4.x Camera motion blur and rolling shutter (task 34, 2026-10-28)

CameraAttributes `motion_blur_*` (enabled, shutter_angle 180°, strength, max_length 0.05 of the frame height) and
`rolling_shutter_*` (enabled, readout_time 20 ms) on the shared base; `CameraAttributesPhysical.motion_blur_use_shutter_speed`
(default on) takes the exposure time from `exposure_shutter_speed` instead of the angle, with or without physical light
units. RS: `camera_attributes_set_motion_blur()`, `camera_attributes_set_rolling_shutter()`,
`camera_attributes_set_motion_blur_quality()` + `RS.CameraAttributesMotionBlurQuality`; ProjectSettings
`rendering/camera/motion_blur/motion_blur_quality` (8/16/32 taps).

Model (`camera_attributes_storage.cpp`, "Motion blur and rolling shutter"): the velocity buffer holds one frame's motion,
so blur length = motion per frame x exposure time / frame time (frame time = the step the frame was simulated over,
60 fps assumed when the clock is stopped); rolling shutter shows row y at readout x (y/H - 0.5) from the frame's time.

Pass (`effects/camera_motion_blur.{h,cpp}`, `shaders/effects/camera_motion_blur.glsl`, its own 128-byte push constant;
the tonemap push constant and the CameraEffects UBO are untouched): prep (velocity + depth -> motion in output pixels
and linear depth; the sky gets the camera's motion derived from depth, as MotionVectorsStore does) -> tile max (one
workgroup per tile, tile = longest blur radius) -> 3x3 neighbor max -> gather (rolling shutter by fixed-point
iteration, then McGuire et al. 2012's tile/neighborhood reconstruction with Jimenez 2014's tap weights and fixed-N
normalization), copied back into the color buffer. Runs in `_render_buffers_post_process_and_tonemap()` after DOF,
before auto exposure and glow. Created on first use (no startup cost); skipped entirely when off.
`render_forward_clustered.cpp` turns motion vectors on (and resolves them under MSAA) whenever it is on. The shader
baker's advanced-group scan knows the new property and RS names (web variant pruning, §2.27).

Findings worth keeping:
- McGuire's cone/cylinder weights are not energy-exact: a moving edge over a still background came out with a step
  at the edge (0.39 -> 0.87 instead of a ramp through 0.5). Jimenez's weighting (behind x own spread + in front x tap
  spread, unclaimed taps -> center) reproduces the box shutter exactly for that case.
- McGuire's tap spacing (i + 1) / (S + 1) shortens every blur to S / (S + 1) of its length (0.941 at 16 taps, measured);
  stratified taps (i + 0.5 + jitter) / S fix it.

Verified: blur length vs speed x exposure (box-shutter reference fit) within 0.3% at 720p and 1080p, same fraction of the
frame at both; rolling-shutter skew vs readout x screen speed within 1.1% (geometry) / 0.5% (sky); default off
bit-identical on Forward+, Mobile, Compatibility; WebGPU vs native in SUMMARY.md.

README engine additions line: "Camera motion blur from the shutter (shutter angle, or the physical shutter speed) and
rolling shutter, Forward+ (desktop and web)."

## Questions for the user

- Should the editor's 3D viewport show motion blur while navigating when the WorldEnvironment's CameraAttributes has
  it on? Default (built): yes, as it does with DOF and grain; an editor-camera opt-out would be a small follow-up.
