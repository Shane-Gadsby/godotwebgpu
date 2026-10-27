# 34-camera-motion: motion blur from the shutter, rolling shutter

State: open
Owner: worker-34
Claimed:
Needs: build, gpu
Branch: work/34
Results: webgpu_notes/work/results/34-camera-motion/

Read `webgpu_notes/work/camera-batch.md` first.

## Goal

1. **Motion blur** driven by the camera: CameraAttributesPhysical's `exposure_shutter_speed` (1/s) and the frame time
   give the blur length: blur = screen-space velocity per frame * (shutter time / frame time), so a 1/50 s shutter at
   60 fps smears 1.2 frames' worth of motion and 1/2000 s almost none; a **shutter angle** mode (180° default for cinema look) for
   CameraAttributesPractical (and as an option on Physical); enable + strength + max blur (fraction of frame).
   Per-pixel velocity from the motion vector buffer Forward+ already renders for TAA/FSR2 (force it on when motion blur
   is active), with camera motion included. Technique: a well-known reconstruction filter (McGuire et al. 2012
   "A Reconstruction Filter for Plausible Motion Blur" with tile max/neighbor max velocity, or Jimenez's next-gen
   approach) — scene-linear, before tonemapping and after DOF. Sample counts per project (ProjectSettings, like DOF
   quality). Resolution correct (velocity in UV units).
2. **Rolling shutter**: readout time (ms, typical CMOS 5-30 ms; global shutter = 0): rows are exposed at different
   times, so each row is displaced by velocity * (row time offset). Screen-space approximation from the same velocity
   buffer (document its limits: no disocclusion fill beyond what the buffer has). Camera rotation (pans) is the main
   visible case: make sure camera motion produces the skew.
3. Renderer support: Forward+ (desktop and WebGPU). Mobile renders no motion vectors: either add them for this (only if
   cheap and safe) or document "Forward+ only" like auto exposure. Compatibility: document unsupported.

## Verification

Default off and bit-identical. A test scene with an object moving at a known screen speed: measured blur length in
pixels vs shutter_time * speed (two resolutions, same fraction of frame); a static scene with a panning camera: skew
angle vs readout time * pan speed. WebGPU vs native. Smoke. A short frame sequence or GIF of the look for the user.
