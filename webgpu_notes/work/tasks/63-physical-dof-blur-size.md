# 63-physical-dof-blur-size: make CameraAttributesPhysical's depth of field blur the size a real lens would

State: open
Owner: worker-63
Claimed:
Needs: build (editor + web template), native Vulkan run, browser (Playwright)
Branch: work/63
Results: webgpu_notes/work/results/63-physical-dof-blur-size/

User request, 2026-10-28, from the camera-sim session. camera-sim tried switching its camera to
`CameraAttributesPhysical` to use the engine's depth of field, autofocus and sensor-aware grain, and had to switch
back to `CameraAttributesPractical`. The physical depth of field's blur is a fixed fraction of the right size,
whatever the sensor size or render resolution.

## The problem

`CameraAttributesPhysical::_update_frustum()` (`scene/resources/camera_attributes.cpp`):

```cpp
float scale = (frustum_focal_length / (u - frustum_focal_length)) * (frustum_focal_length / exposure_aperture);
...
scale / 5.0); // Arbitrary scaling to get close to how much blur there should be.
```

`dof_blur_amount` is then multiplied by 64 in `BokehDOFRD` ("Base 64 pixel radius") to get the blur radius in
pixels. So the physical blur radius is `f² / (N·(u − f)) / 5 · 64` px, where the f²/(N·(u−f)) term is the blur
circle's diameter in mm on the sensor. That ignores:
- **the sensor size.** The same blur in mm is a bigger part of a smaller frame. On an MFT sensor (half the
  height of full frame), the same lens and aperture should blur twice as many pixels. Today `sensor_size` /
  `sensor_format` change the FOV, the CoC and the grain, but not the blur.
- **the render resolution.** At 1440p a blur circle covers twice as many pixels as at 720p. Today the radius in
  pixels is fixed, so the image gets sharper as the resolution goes up.

It's also about 30% weaker than the thin-lens value at camera-sim's 1244×700 render.

## The correct size

For a lens of focal length f (mm) at f-number N focused at u (mm), a point at distance d blurs into a circle on
the sensor of diameter

    c(d) = f² / (N·(u − f)) · |d − u| / d        (mm)

The bokeh shader's physical mode (negative transitions) already applies the `|d − u| / d` falloff, so it only
needs the maximum radius in **pixels of the image being blurred**:

    radius_px = 0.5 · f² / (N·(u − f)) / frame_mm · frame_px

Here `frame_mm` is the sensor dimension the field of view is fitted to, and `frame_px` the same dimension of
the render. Today that's `sensor_size.y` and the render height; for an anamorphic lens it's the width, matching
`is_fov_horizontal()`. Only the renderer knows `frame_px`, and `CameraAttributes` are resolution-independent.
So the conversion belongs at render time:
- keep the lens's blur in mm (or as a fraction of the frame, `c∞ / frame_mm`) in the camera attributes storage;
- turn it into pixels in `BokehDOFRD` (both the compute and raster paths), the way `camera_attributes_get_grain_packed`
  already takes the output height;
- remember `half_size` / `blur_scale` and 3D resolution scaling (FSR 2, Bilinear): the size is in pixels of the
  buffer being blurred.

camera-sim has computed exactly this in GDScript since 2026-10-26 (`scripts/main.gd` `update_dof()`: Practical
attributes, both distances at u, both transitions −1, `dof_blur_amount = radius_px / 64` with the render width
and sensor width). It matches the thin-lens blur at f/1.4 to f/16. Use it as a reference implementation.

## Goal

1. **Opt-in, so existing projects don't change** (the fork's rule for new options): a
   `CameraAttributesPhysical` property such as `dof_physical_blur_size` (bool, default `false`). When true, the
   blur radius is the formula above. When false, the current `/ 5` behaviour stays bit-identical. Document both
   ("the default matches upstream Godot").
2. **The 32 px cap.** In `BokehDOFRD`, physical mode caps the gather reach at 32 px:
   `blur_size = (dof_near_size < 0.0 && dof_far_size < 0.0) ? 32 : bokeh_size`. With correct sizes, a portrait
   lens wide open at 1440p needs more (camera-sim: 50 mm f/1.4 focused at 2.5 m, the far background reaches about
   13 px at 1244 wide; 200 mm f/2.8 focused at 5 m at 1440p reaches about 89 px). When the opt-in is on, either raise the cap
   in proportion to the resolution (e.g. 32 px at 1080p, scaled with the height) or make it a project setting.
   Measure the cost on WebGPU (`gpu_time.mjs --uncapped`) and report it. A visible clamp is better than a large
   hidden cost, but say which one ships.
3. **Mobile DOF for physical cameras** (already in the fork) and Compatibility (no DOF) follow whatever applies
   to them. Document what the opt-in does on each renderer.
4. Nothing else about the physical camera changes: FOV, CoC (diagonal / 1500), autofocus driving
   `frustum_focus_distance`, grain and exposure stay as they are.

## Verification

- **Formula check:** a flat textured plane at d = 5 m, focus u = 2.5 m, 50 mm f/1.4, full frame (36×24), at
  1280×720. Measure the edge spread (blur radius in px) against `0.5 · f²/(N(u−f)) · |d−u|/d / 24 · 720`
  (about 5.5 px at d = 5 m), within the bokeh shader's sampling error.
- **Resolution:** same scene at 2560×1440: the radius in px doubles; the image looks the same scaled down.
- **Sensor size:** MFT (17.3×13) with the same focal length and aperture: the radius in px is 24/13 ≈ 1.85× the
  full-frame one.
- **Opt-in off:** bit-identical screenshots to today's for Forward+, Mobile and web.
- **Cap:** a case above 32 px renders correctly blurred, or clamped as designed, with the GPU cost reported.
- **A/B against camera-sim:** its current Practical setup and a Physical camera with the opt-in, at the same
  settings (render width W, sensor 36 × 20.25 mm = the 16:9 strip of a full-frame sensor's width, so FOV and
  pixels match): face edge energy within a few percent at f/8 focus 5 m, f/1.4 focus 5 m and f/1.4 focus 2.5 m.
  The camera-sim harness steps are in that session's `tools/ui_harness.gd`.

## camera-sim afterwards

Hand this back to the camera-sim session when it lands. It will:
- switch `POV Cam` to `CameraAttributesPhysical` with the opt-in on, and `sensor_size = (sensor_width,
  sensor_width · 9/16)`, the 16:9 recording strip of the sensor, so the engine's vertical FOV gives the app's
  horizontal framing;
- set `frustum_focal_length` from the lens slider and `exposure_aperture` from the aperture slider (with
  physical light units off these don't change brightness; verified 2026-10-28);
- delete the blur-amount maths and the manual near/far writes in `update_dof()` (only the Focus panel's
  "sharp from … to …" readout stays) and the FOV formula in `set_lens()`;
- drop the crop-factor multiplier on `grain_intensity`, since the physical sensor size handles that;
- let autofocus drive `frustum_focus_distance`.

Optional, if it's small: a `sensor_fit` (Auto / Horizontal / Vertical) on `CameraAttributesPhysical`, like
Blender's, so a project can fit the FOV to the sensor width without the 9/16 sensor-size trick.

## Added by the lead

- Runs alongside task 62 (histogram readback: CameraFinish, Camera3D) / 63 (physical DOF blur: CameraAttributesPhysical,
  BokehDOFRD); keep changes in their own blocks, the lead merges both.
- camera-sim (`/mnt/109313D2109313D2/godot-game-dev/camera-sim`) is read-only: read its scripts as the reference, never
  modify it; scratch copies only.
- Compile a web `template_release` before `ready-for-integration` (work README, TASKS §3 trap 95).
- Put the "camera-sim afterwards" hand-back, updated with what you actually built (names, defaults), in for-tasks.md
  under "Note for the camera-sim session".
