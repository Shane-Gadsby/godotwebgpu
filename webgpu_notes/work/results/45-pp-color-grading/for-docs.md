# Wiki text for task 45 (lead adds it; the wiki was only read)

Suggested new page **Engine-Additions-Color-Grading.md**, linked from Engine-Additions.md next to White Balance and
the Camera pages ("Color grading controls on Environment: lift/gamma/gain, shadows/midtones/highlights, channel mixer,
split toning, color filter, hue shift, vibrance and curves").

---

Color grading controls on `Environment`, the ones Unity (URP/HDRP), Unreal and DaVinci Resolve users expect: lift, gamma and gain; shadows, midtones and highlights; a channel mixer; split toning; a color filter, hue shift and vibrance; and curves (master, red, green, blue, hue vs hue, hue vs saturation, saturation vs saturation, luminance vs saturation). They are in the **Color Grading** group of the Environment, below Adjustments, and work in Forward+, Mobile and Compatibility, and on the web.

Before this, the only way to grade in Godot was to grade in another tool and export a LUT. Now the grade lives in the scene, so it can be animated (a hit flash, a mood that changes by area) and tweaked in the editor while you look at the game.

## How it works

Grading is **display-referred**: it works on the image after the tonemapper, sRGB-encoded, where the Adjustments' brightness, contrast and saturation already work. Whenever a grading property changes, every control is baked into one 33×33×33 3D lookup table, which the renderer applies with the color correction LUT it already had. So grading costs the same however many controls you use: one texture lookup per pixel. Animating a control rebakes the table every frame: about 1.5 ms of CPU time on a desktop with every control on, spread over the worker threads (about 4.6 ms on one core, as in a web export without threads).

The order, the one URP uses:

1. Color filter, channel mixer, shadows/midtones/highlights (in linear light),
2. lift, gamma and gain,
3. split toning,
4. hue shift and the hue/saturation curves, then vibrance,
5. the master curve, then the red, green and blue curves.

Then `adjustment_color_correction`, if set, applies to the graded image. Brightness, contrast and saturation (Adjustments) come before the grade.

## Properties

| Property | Default | Effect |
|---|---|---|
| `grading_enabled` | `false` | Turns grading on. |
| `grading_color_filter` | white | Multiplies the image by this color in linear light, like a colored filter on the lens. |
| `grading_hue_shift` | `0` | Rotates every hue by this many degrees. |
| `grading_vibrance` | `0` | Positive: saturates muted colors the most and leaves saturated ones mostly alone, gentler on skin and already-colorful objects than `adjustment_saturation`. Negative: desaturates the most saturated colors the most (`-1` removes almost all color). |
| `grading_lift` | black | Raises each channel's black level toward this color; white stays white. |
| `grading_gamma` | white | Bends each channel's midtones (output = input^(1/gamma)); above 1 brightens. Black and white stay put. |
| `grading_gain` | white | Scales each channel; black stays black. |
| `grading_shadows`, `grading_midtones`, `grading_highlights` | white | Tints that multiply the shadows, midtones and highlights (by luminance). |
| `grading_shadows_start`, `grading_shadows_end` | `0`, `0.3` | Shadows apply fully below the start and fade out by the end. |
| `grading_highlights_start`, `grading_highlights_end` | `0.55`, `1` | Highlights fade in from the start and apply fully above the end. |
| `grading_channel_mixer_red`, `_green`, `_blue` | identity | Each output channel as a mix of the input red, green and blue (x, y, z); negative weights allowed. |
| `grading_split_toning_shadows`, `_highlights` | gray | Soft-light tints for the darks and the brights; gray is neutral. |
| `grading_split_toning_balance` | `0` | Moves the split toward the highlights (positive) or the shadows (negative). |
| `grading_curve_master`, `_red`, `_green`, `_blue` | none | Tone curves (`Curve` resources, X input 0-1, Y output). |
| `grading_curve_hue_vs_hue` | none | Shifts each hue by (Y − 0.5) turns; 0.5 is neutral. |
| `grading_curve_hue_vs_sat`, `_sat_vs_sat`, `_lum_vs_sat` | none | Multiply saturation by 2·Y as a function of hue, saturation or luminance; 0.5 is neutral. |

For the hue curves, make the two ends equal: hue wraps around, so X = 0 and X = 1 are the same red. A curve left empty, or with no points, does nothing.

## Example

A teal-and-orange look with a slight film curve:

```gdscript
var env: Environment = $WorldEnvironment.environment
env.grading_enabled = true
env.grading_split_toning_shadows = Color(0.35, 0.5, 0.6)
env.grading_split_toning_highlights = Color(0.65, 0.5, 0.35)
env.grading_vibrance = 0.2
env.grading_lift = Color(0.02, 0.02, 0.03)

var curve := Curve.new()
curve.add_point(Vector2(0.0, 0.03))
curve.add_point(Vector2(0.25, 0.2))
curve.add_point(Vector2(0.75, 0.82))
curve.add_point(Vector2(1.0, 0.97))
env.grading_curve_master = curve
```

A red hit flash, animated with a Tween:

```gdscript
func hit_flash() -> void:
	env.grading_enabled = true
	var tween := create_tween()
	tween.tween_property(env, "grading_color_filter", Color(1.0, 0.6, 0.6), 0.05)
	tween.tween_property(env, "grading_color_filter", Color(1, 1, 1), 0.3)
```

## Renderers and limits

| | Forward+ | Mobile | Compatibility | Web (WebGPU) |
|---|---|---|---|---|
| Color grading | yes | yes | yes | yes |

- Grading clamps the image to the 0-1 range, like the color correction LUT does: with **HDR output**, values above 1.0 are clipped while grading is on.
- The grade is a 33³ table sampled with linear interpolation, so very sharp curves (a step) are softened over about 1/32 of the range. Measured against the exact math on a color chart, each control is within 1 level (of 255) in Forward+ and on the web (2.5 with the adjustments and a LUT of your own on top), and within 2-3 levels in Mobile and Compatibility.
- Mobile renders into a 10-bit buffer, so its grade carries that buffer's banding in the darks, as the rest of its post-processing does.
- In Compatibility, turning grading on turns on the post-processing pass.

**Cost:** one 3D texture lookup per pixel (the one the color correction LUT uses), 288 KB of video memory per Environment with grading on, and a rebake when a property changes. Nothing while off.
