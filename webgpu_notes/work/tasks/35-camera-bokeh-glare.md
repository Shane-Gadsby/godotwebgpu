# 35-camera-bokeh-glare: aperture blades, cat's eye, tilt-shift, starburst, halation; lens flare, veiling glare, diffusion filters

State: open (task 33 integrated at 779120b64e: branch from webgpu-4.8; the CameraEffects UBO and its extension recipe are in results/33-camera-lens-optics/SUMMARY.md and TASKS §4.52)
Owner: worker-35
Claimed:
Needs: build, gpu, task 33 (the CameraEffects UBO glare region)
Branch: work/35
Results: webgpu_notes/work/results/35-camera-bokeh-glare/

Read `webgpu_notes/work/camera-batch.md` first.

## Goal

CameraAttributes:
1. **Aperture blades**: blade count (0 = circular, 5-11), blade curvature (rounded vs straight), rotation, replacing
   the global `rendering/camera/depth_of_field/depth_of_field_bokeh_shape` per camera when set (keep the project
   setting as the default). Physical DOF bokeh polygon from blades; anamorphic squeeze (task 33's UBO) makes it oval.
2. **Cat's eye bokeh**: optical vignetting clips bokeh toward the frame edges, more wide open.
3. **Tilt-shift**: focal plane tilt (degrees, axis angle) for the physical DOF: the CoC uses distance to a tilted plane
   (Scheimpflug), so miniature-faking works.
4. **Starburst / diffraction spikes**: bright highlights get spikes, 2 per blade for odd counts, 1 per blade for even;
   stronger stopped down. Through the glow chain or a dedicated kernel.
5. **Halation**: film's red-orange glow around highlights (light scattering back from the film base); radius relative to
   frame, strength; pairs with the film grain type (default off; suggest on for the film look in docs).

Environment (scene look, next to glow):
6. **Lens flare**: ghosts along the line from bright sources through the frame center (screen-space ghosting from the
   bright pass, e.g. the John Chapman / Kawase approach), with chromatic tint, halo; strength, ghost count.
7. **Veiling glare**: contrast loss from scattered light, scaled by the image's bright energy.
8. **Diffusion filter** (Pro-Mist / Black Pro-Mist style): wide soft glow and lifted blacks; strength, and a "black"
   variant that preserves blacks.

Use the existing glow downsample chain where possible (bright pass + blurs), forcing it on when any of these need it;
parameters in the CameraEffects UBO glare region (task 33) and in the glow push constant where they belong.

## Verification

Default bit-identical. Bokeh: render a point light out of focus and compare the bokeh polygon (vertex count, rotation)
with the settings; cat's eye eccentricity vs position; tilt-shift: focus band orientation vs tilt. Starburst spike
count. Flare ghost positions on the line through the center. WebGPU vs native, smoke, frames for the user to judge.
