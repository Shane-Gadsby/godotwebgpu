# Wiki text for task 44 (lead adds it; the wiki pages were only read)

## Engine-Additions-Camera-Bokeh-and-Glare.md

### Table: add a row after "Tilt-shift"

| Longitudinal chromatic aberration | `CameraAttributes`, Longitudinal Chromatic Aberration group | yes | yes⁴ | no (no depth of field) | yes |

### Replace footnote ¹ (Mobile now honors the physical focus model, work task 44)

¹ Works with both `CameraAttributesPractical` and `CameraAttributesPhysical`. (Before 2026-10-28, Mobile's depth of field ignored `CameraAttributesPhysical`, so a physical camera had no blur at all on Mobile; it now matches Forward+.)

### New footnote

⁴ Mobile blends the sharp and the blurred image with one amount for all three colors, so with `CameraAttributesPhysical` the in-focus area is blended very slightly toward the half-resolution blur.

### New section, after "Tilt-shift"

## Longitudinal chromatic aberration (bokeh fringing)

A lens focuses each color at a slightly different distance. In a normal (achromatic) camera lens red and blue focus together a little behind green, so out-of-focus edges get colored fringes: **green behind the plane of focus, magenta ("purple") in front of it**, the bokeh fringing fast lenses are known for. It is drawn by the depth of field, so it only appears where the image is out of focus, and it shrinks as you stop down.

| Property | Default | Effect |
|---|---|---|
| `longitudinal_chromatic_aberration_enabled` | `false` | Turns it on. |
| `longitudinal_chromatic_aberration_strength` | `1.0` | `1.0` is an ordinary achromat (red and blue focus 1/2200 of the focal length behind green). Fast lenses wide open show several times that: `3` to `10` gives the strong fringing of fast primes. Apochromats are below `1`. |

With `CameraAttributesPhysical` the fringe follows the lens: its width is the circle-of-confusion difference of a focus shift of 1/2200 of the focal length, so it halves from f/1.4 to f/2.8 and grows with the focus distance relative to the focal length. With `CameraAttributesPractical` (no lens) red and blue blur 2.7% more than green in front of the focus and 2.7% less behind it, times the strength, so the in-focus band stays sharp.

Example: a portrait lens wide open with visible fringing.

```gdscript
var attributes := CameraAttributesPhysical.new()
attributes.frustum_focal_length = 85.0
attributes.exposure_aperture = 1.4
attributes.frustum_focus_distance = 2.0
attributes.longitudinal_chromatic_aberration_enabled = true
attributes.longitudinal_chromatic_aberration_strength = 6.0
$Camera3D.attributes = attributes
```

The fringe is exactly the model's: red and blue blur as much as green would at the shifted focus (measured on a defocused edge, 50 mm f/1.4 and f/2.8). Lateral chromatic aberration, which does not depend on focus or aperture, is the Lens page's `chromatic_aberration_*`.

**Cost:** two extra smoothsteps per depth-of-field sample while it is on; nothing when off.

## Engine-Additions-Camera-Lens.md, after the chromatic aberration paragraph

This is lateral chromatic aberration (colors imaged at different sizes). Its focus-dependent sibling, longitudinal chromatic aberration (colored fringes on out-of-focus edges), is on the [Bokeh and Glare](Engine-Additions-Camera-Bokeh-and-Glare) page.

## Engine-Additions-Notable-Fixes.md

- **Mobile depth of field with `CameraAttributesPhysical`** (2026-10-28): the Mobile renderer never passed the physical camera's focus model to its depth of field, so a physical camera had no depth-of-field blur on Mobile at all. It now blurs as in Forward+ (circle of confusion about 3% wider than Forward+'s on a defocused edge). Mobile scenes using `CameraAttributesPhysical` with a wide aperture now show blur where they showed none.
