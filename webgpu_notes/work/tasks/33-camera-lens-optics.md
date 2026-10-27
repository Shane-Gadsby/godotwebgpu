# 33-camera-lens-optics: sensor format, vignetting, distortion, chromatic aberration, anamorphic, ND/T-stops, diffraction, breathing; the CameraEffects UBO

State: ready-for-integration
Owner: worker-33
Claimed: 2026-10-28 08:19
Needs: build, gpu
Branch: work/33
Results: webgpu_notes/work/results/33-camera-lens-optics/

Read `webgpu_notes/work/camera-batch.md` first (conventions for tasks 33-38).

## Goal

1. **CameraEffects uniform buffer for the tonemap pass** (do this first; tasks 35 and 36 build on it). A std140 UBO
   bound in Forward+ `tonemap.glsl` (pick a free binding in an existing set; set 3 is shared with push-constant
   emulation on WebGPU, so prefer set 1 next to the exposure texture), Mobile `tonemap_mobile.glsl` (set 0, keep all on
   set 0 per GH-114785) and the Compatibility post pass (its own UBO or the Tonemap UBO). Layout with **reserved
   regions**: a `uvec4 header` (x = lens feature bits, y = glare bits for task 35, z = exposure-automation bits for task
   36, w reserved), `vec4 lens[8]`, `vec4 glare[6]` (task 35), `vec4 exposure[2]` (task 36), padded to a round size.
   Filled on the CPU by `RendererCameraAttributes` (one entry point that calls a per-region static function; leave the
   glare/exposure ones as stubs writing zeros), uploaded once per tonemap pass; owned so several viewports per frame
   stay correct (per render buffers, or ordered `buffer_update`s — check the RD graph orders them). Mobile's subpass
   tonemap cannot take a `buffer_update` inside the draw list: features needing the UBO make Mobile skip the subpass
   post-process (as glow and auto exposure already do; see `render_forward_mobile.cpp`). Zero header = nothing runs,
   output bit-identical.
2. **Sensor format** on CameraAttributesPhysical: presets (Full frame 36x24, APS-C 23.6x15.6, Super 35 24.89x18.66,
   Micro Four Thirds 17.3x13, Medium format 44x33, 1-inch 13.2x8.8, Custom) + width/height in mm, replacing the
   hardcoded 36x24 in `_update_frustum()` (FOV, CoC = diagonal/1500, DOF). Grain (task §4.47) uses the frame height
   too: feed the sensor height to the grain model instead of its 24 mm constant.
3. **Vignetting**: natural (cos^4 of the field angle from focal length and sensor; physical) plus optical/mechanical
   vignetting that strengthens wide open and fades by ~2 stops down (model it from aperture; cite the source), and a
   strength override. Practical attributes: a strength + falloff without the physics.
4. **Lens distortion**: radial (Brown-Conrady k1, k2) barrel/pincushion, with an automatic value from focal length
   (wide = barrel) that a user can override, and an "auto-crop" so no border shows. Applied by resampling the source in
   the tonemap pass (all fetches: color, glow, FXAA's neighborhood if feasible; document what isn't).
5. **Chromatic aberration** (lateral): per-channel radial magnification difference, stronger wide open and at the edges;
   strength slider. Same resample.
6. **Anamorphic lenses**: squeeze factor (1.0, 1.33, 1.5, 1.8, 2.0): horizontal FOV widens by the squeeze, oval bokeh is
   task 35's (expose the squeeze in the UBO lens region for it), distortion becomes anamorphic (different k per axis).
7. **ND filter** (stops) and **T-stops** (lens transmission loss in stops): exposure only, CPU side, physical units.
8. **Diffraction softening**: Airy-disk blur from the f-number and the pixel pitch on the sensor (resolution correct):
   visible past ~f/11 on full frame; small kernel in the tonemap pass. Cite the Airy formula.
9. **Focus breathing**: FOV change with focus distance (a per-lens breathing amount, default a typical stills lens),
   via the FOV CameraAttributesPhysical already drives.

Order inside the tonemap pass, physically: optics (distortion, CA, diffraction, vignetting) act on the scene light
before the sensor, so before white balance, grain and tonemapping; grain must not be distorted.

## Verification

Bit-identical default (all three renderers). Numerical checks against references you compute independently: vignette
falloff curve vs cos^4 at known field angles; a straight-line grid through distortion vs Brown-Conrady; CA fringe
widths in pixels at two resolutions (same fraction of the frame); FOV for each sensor preset vs the formula; diffraction
kernel width vs 2.44 * lambda * N relative to pixel pitch. WebGPU in Chrome vs native Forward+. Smoke (with the
regenerated references). SUMMARY with numbers; for-docs.md with wiki text.
