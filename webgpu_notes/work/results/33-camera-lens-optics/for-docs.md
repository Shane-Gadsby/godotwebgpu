# For the docs (task 33: lens optics)

Wiki page under **Engine Additions**: "Camera lens optics". Text below is user-facing; class reference XML is already
on the branch.

---

## Camera lens optics

A real lens is not a perfect pinhole: it darkens the corners, bends straight lines, fringes edges with color and, at
small apertures, softens the whole image. `CameraAttributes` can now do all of this, and `CameraAttributesPhysical`
derives it from the camera you describe (sensor, focal length, aperture, focus distance) instead of from free
sliders. Every effect is off by default, and a project that does not turn one on renders exactly as before.

All lens effects act on the light before it reaches the sensor, so they come before white balance, grain, the
tonemapper and the camera LUT. Spatial sizes are relative to the frame, so the look is the same at any resolution.

### Sensor (CameraAttributesPhysical)

| Property | What it does |
|---|---|
| `sensor_format` | Full Frame (36x24 mm, default), APS-C (23.6x15.6), Super 35 (24.89x18.66), Micro Four Thirds (17.3x13), Medium Format (44x33), 1-inch (13.2x8.8), Custom. |
| `sensor_size` | Width and height in mm; editable with Custom. |

The sensor sets the field of view of `frustum_focal_length` (vertical FOV = 2·atan(height / 2f)), the circle of
confusion of depth of field (diagonal / 1500) and the size of film grain. A 50 mm lens on Micro Four Thirds frames
like a 100 mm lens on full frame, exactly as on real cameras.

### Lens (CameraAttributesPhysical)

| Property | What it does |
|---|---|
| `lens_max_aperture` | The lens's widest f-stop (default f/1.8). Optical vignetting is strongest here and gone two stops down. |
| `lens_transmission_loss` | Stops of light the glass loses: the gap between the T-stop and the f-stop (T = N·2^(loss/2)). Exposure only. |
| `lens_anamorphic_squeeze` | 1.0 (spherical), or 1.33, 1.5, 1.8, 2.0 for anamorphic lenses: the horizontal field of view widens by the squeeze, and the camera keeps the horizontal FOV (`is_fov_horizontal()`). |
| `lens_focus_breathing` | How the field of view changes with focus distance: 1.0 = a unit-focusing stills lens (narrower up close), 0 = a cine lens that does not breathe (default), negative = an internal-focus zoom that widens up close. |
| `exposure_nd_filter` | A neutral density filter in stops (each stop halves the light). |

ND and transmission loss need **Physical Light Units** (Project Settings > Rendering > Lights and Shadows).

### Vignetting (both)

`vignetting_enabled`, `vignetting_strength`, and on `CameraAttributesPractical` `vignetting_falloff`.

- **Physical**: natural vignetting (the cos⁴ law of the field angle; wide lenses darken more) plus optical vignetting
  (the barrel clipping the aperture off-axis: about half a stop at the corners wide open, none two stops down).
  `vignetting_strength` scales it in stops (1 = physical).
- **Practical**: `vignetting_strength` stops of darkening at the corners, growing with distance from the center to the
  power `vignetting_falloff`.

### Distortion (both)

`distortion_enabled`, `distortion_automatic` (Physical), `distortion_k1`, `distortion_k2`, `distortion_auto_crop`.

The Brown-Conrady radial model: a point at distance r from the center (1 at the corners) moves to
r·(1 + k1·r² + k2·r⁴). Negative k1 = barrel (wide angle), positive = pincushion (telephoto). **Automatic** takes a
typical lens of the focal length and sensor: about 5% barrel at 14 mm on full frame, 2% at 24 mm, nothing around 50-60
mm, slight pincushion on short telephotos. Anamorphic lenses distort more horizontally. **Auto Crop** zooms in just
enough that no black border shows, as in-camera lens correction does.

### Chromatic aberration (both)

`chromatic_aberration_enabled`, `chromatic_aberration_strength`. Lateral color: each color is imaged at a slightly
different size, so edges get red and blue/cyan fringes that grow toward the frame's edges. Strength 1 = 0.1% red-blue
spread (a moderate real lens, about a pixel at the corner of a 1080p image); negative swaps the colors. Like on a real
lens, stopping down does not reduce it.

### Diffraction (CameraAttributesPhysical)

`diffraction_enabled`, `diffraction_strength`. At small apertures the aperture's diffraction spreads every point into an
Airy disk 2.44·λ·N across (λ = 550 nm, N = working f-number, larger at close focus). It is relative to the sensor, so
like on a real camera it is invisible at f/8 and shows past about f/16-f/22 on small sensors or at 4K; at 1080p on full
frame you need f/32 or more. `diffraction_strength` exaggerates it.

### Example

```gdscript
var attributes := CameraAttributesPhysical.new()
attributes.sensor_format = CameraAttributesPhysical.SENSOR_FORMAT_SUPER_35
attributes.frustum_focal_length = 40.0
attributes.lens_anamorphic_squeeze = 2.0       # Classic 2x anamorphic: wide horizontal field.
attributes.exposure_aperture = 2.8
attributes.lens_max_aperture = 2.8              # Wide open: strongest optical vignetting.
attributes.vignetting_enabled = true
attributes.distortion_enabled = true            # Automatic: barrel, stronger horizontally.
attributes.chromatic_aberration_enabled = true
attributes.chromatic_aberration_strength = 2.0
$Camera3D.attributes = attributes
```

Set the viewport to a wide aspect (2.39:1) for the full anamorphic frame.

### Renderer support

| | Forward+ | Mobile | Compatibility | Web (WebGPU) |
|---|---|---|---|---|
| Sensor, breathing, anamorphic FOV, ND, T-stop | yes | yes | yes | yes |
| Vignetting, distortion, chromatic aberration, diffraction | yes | yes (turns off the subpass post-process, like glow) | yes (runs the post pass) | yes |

Limits:
- Effects applied before the tonemap pass (depth of field, SSR, SMAA's input, compositor effects) see the
  undistorted image; FXAA follows the lens. Glow follows the distortion; the glow map (lens dirt) stays in screen space.
  Soft Light glow is added after tonemapping and is not vignetted.
- Diffraction costs 25 texture reads per pixel (75 with chromatic aberration) while it is visible, and nothing when it
  is far below a pixel.
- In Compatibility the lens resamples the renderer's sRGB-encoded scene buffer, whose bilinear filtering blends encoded
  values, so the diffraction blur measures about 5% narrower there than in Forward+ and Mobile.
