# For TASKS.md (work task 63)

## §4 entry

### 4.xx Physical depth of field sized by the lens (October 2026) *(work task 63, from the camera-sim session)*

`CameraAttributesPhysical.dof_physical_blur_size` (bool, default off; off = upstream, pixel-identical): the DOF blur is
the thin-lens circle `f² / (N (u − f)) · |d − u| / d`, sized as a share of the frame. The scene sends it as a fraction of
the frame dimension the FOV is fitted to (sensor height, or desqueezed width for an anamorphic lens) through the new
`RS::camera_attributes_set_dof_physical_blur_size(rid, enable, frame_fraction, frame_horizontal)`.
`BokehDOF::_physical_blur_size()` turns it into pixels of the buffer being blurred (`base_texture_size`: the internal
size, or the upscaled one under FSR 2 / MetalFX temporal), in the compute (Forward+) and raster (Mobile) paths.
- **Gather reach:** a project setting, `rendering/camera/depth_of_field/depth_of_field_physical_max_blur_size` (percent
  of the buffer height, default 3% = upstream's 32 px at ~1080p), read with `GLOBAL_GET_CACHED`. It applies to opt-in
  cameras only; the others keep the fixed 32 px. A larger blur is clamped (**a visible clamp ships, not a hidden cost**):
  WebGPU at 1440p, Hexagon Low 0.61 ms at 3% / 1.33 ms at 10%, Hexagon High 3.2 / 3.4 ms, Circle Medium 7.8 ms at
  upstream's 32 px / 13.9 ms at 3% (43 px) / too slow to measure at 6% (SUMMARY.md).
- **Box/Hexagon gather fixed under the opt-in (`BOKEH_PHYSICAL_SIZE`, `shape_flags` bit 12; push constant unchanged,
  still 128 bytes):** upstream's `weighted_filter_dir()` adds the center color for every sample outside the blur and
  divides by all 2·steps+1 samples, so it is the right width only when the blur fills the reach. In physical mode the
  reach is fixed at 32 px, so a 5 px blur kept 4 of 25 samples and was nearly sharp. Now it divides by the sample
  positions inside the largest blur reaching the pixel (`bokeh_physical_size_resolve()`), which is upstream's estimator
  with a per-pixel reach. Half-size (Very Low/Low) gathers also convert the full-res blur sizes in alpha to half-res
  pixels (upstream compares them directly: blur twice as wide). Circle was already exact.
- **Verified** (native Vulkan, 4080 SUPER; edge plane, linear tonemapper, level 0.5): 50 mm f/1.4, u 2.5 m, d 5 m, full
  frame 1280×720: model 5.47 px; circle 2σ radius 5.69, 10–90% width 7.62 (model 7.51); hexagon 7.66 (7.57); box
  8.53 (8.75); every shape × quality within ~5% on Forward+ and Mobile, except Circle Very Low (first ring at 8 px:
  upstream, documented). 2560×1440: circle 10.97 / hexagon width 14.49 (model 10.93 / 15.15). MFT 17.3×13: circle
  10.12 (model 10.09). Near field (u 8, d 3): 5.74 (model 5.62). Bilinear 3D scale 0.5 stays frame-relative. Cap:
  200 mm f/2.8 u 5 d 50 at 1440p (model 80.4 px): 42.8 at 3% (cap 43.2), 79.5 at 8%. Opt-in off: 10/10 renders
  pixel-identical to the pre-change editor (Forward+, Mobile; physical and practical, all shapes), web 3/3
  pixel-identical to a baseline template; WebGPU opt-in matches native (circle 7.65, hexagon 7.93, box 8.54).
  camera-sim A/B (its scene, 1244×700, Practical `update_dof()` vs Physical + opt-in on a 36×20.25 sensor): with the
  circle shape, face edge energy within 2% at f/8 u5, f/1.4 u5, f/1.4 u2.5. With camera-sim's own High hexagon, 1.02
  and 1.00 at the first two, and 0.16 at f/1.4 u2.5: camera-sim's practical path runs through the diluted upstream
  gather (its defocused face keeps 6x the detail); the opt-in hexagon matches the circle.
- Compatibility: no DOF (documented). Interface references unchanged (no binding or push-constant change); smoke.sh not
  run (no interface change; lead's instruction).
- Not done: `sensor_fit` (Auto/Horizontal/Vertical). Not small: the storage's frame geometry for vignetting,
  distortion, CA, cat's eye and glare (`_camera_frame_mm()`, `_fill_effects_lens()`) assumes the fit from the squeeze in
  three places, and Camera3D's keep_aspect too; it wants its own task.

## §3 trap candidates

- **A DOF edge that measures "nearly sharp" may be the gather, not the size.** Upstream's Box/Hexagon physical-mode DOF
  divides by all samples up to a fixed 32 px reach, so blurs well under 32 px are diluted toward sharp (a 5.5 px blur
  read as a 1.5 px 10–90% edge with long tails). The Circle gather does not do this. Compare shapes before trusting a
  size measured on the hexagon; camera-sim's tuning by eye was done through the diluted hexagon.
- **`gpu_time.mjs --size` does not set the canvas of an export with the default window size**: the canvas stayed
  1152×648 at `--size 1920x1080` and `2560x1440`, so both runs measured the same thing. Set
  `display/window/size/viewport_width/height` in the exported project, and check `canvas.width` before reading
  resolution-dependent costs.

## Questions for the user

- `dof-physical-max-blur` (choice): how far may a physical camera's blur reach before it is clamped?
  - **3% of the image height (default)**: upstream's 32 px at 1080p; same cost as upstream there, cheaper below,
    Circle costs more above (1440p: 43 px, 13.9 ms against 7.8).
  - 6%: long lenses wide open fit (200 mm f/2.8 at 1440p needs 5.6%); Circle costs several times as much.
  - Fixed 32 px whatever the resolution: never costs more than upstream, but the look changes with resolution.
- `camera-sim-hexagon` (choice): camera-sim's look was tuned through the diluted hexagon gather; with the opt-in, wide
  apertures blur noticeably more (the correct amount). **Accept the stronger blur (default)** / keep a "legacy look"
  option.

## Note for the camera-sim session

The engine side has landed on work/63 (pending integration). What to change in camera-sim, read-only reference
`scripts/main.gd`:

- `POV Cam.attributes = CameraAttributesPhysical` with `dof_physical_blur_size = true` (Inspector: Depth of Field >
  Physical Blur Size), `sensor_size = Vector2(sensor_width, sensor_width * 9.0 / 16.0)` (sets the format to Custom;
  the vertical FOV of the 16:9 strip equals the app's horizontal framing; verified 39.6° horizontal = 22.9° vertical at
  50 mm), `frustum_near = 0.001`, `frustum_far = 15` (the camera's own values: the physical attributes override them).
- `frustum_focal_length` from the lens slider, `exposure_aperture` from the aperture slider (no exposure change with
  physical light units off).
- Delete `update_dof()`'s blur math and near/far writes (keep `update_dof_label()`), the FOV formula in `set_lens()`
  (the camera's FOV now comes from the attributes; the camera diagram can read `camera.fov`), and the crop-factor
  multiplier on `grain_intensity`.
- Autofocus: keep `autofocus_mode` Continuous; it drives `frustum_focus_distance`.
- Copy the other CameraAttributes settings over (white balance 5600 K, grain type Digital). The A/B harness that did
  this is `results/63-physical-dof-blur-size/tools/ab63.gd`.
- Expect more blur at wide apertures than today: today's look goes through upstream's hexagon gather, which dilutes
  small blurs (f/1.4 focused at 2.5 m: the defocused face had 6x the edge energy the thin lens gives). The sizes
  themselves match camera-sim's math to 2% (checked with the circle shape).
- Blur reach: 3% of the render height by default (21 px at 700p). Raise
  `rendering/camera/depth_of_field/depth_of_field_physical_max_blur_size` for telephoto wide open; keep the Hexagon
  shape (a larger reach costs it little: 0.6 to 1.3 ms at 1440p going from 3% to 10%).
