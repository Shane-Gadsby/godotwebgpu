# Wiki text: Engine Additions → Light Shafts (task 46)

Proposed page: **Engine Additions / Light Shafts** (a sibling of the camera pages, or under a "Post-processing" group
with grading and Panini when those land).

---

When the sun is behind trees, a window frame or a cloud edge, the haze in the air lights up in streaks pointing at it:
light shafts, or "god rays". Godot WebGPU adds them as a screen-space effect: bright sky around the sun is blurred
toward the sun's position on screen, and whatever hides the sky (leaves, beams, buildings) cuts dark gaps into the
streaks.

It is **off by default**: an existing project renders exactly as before. It works in **every renderer** (Forward+,
Mobile, Compatibility) and in web exports (WebGPU).

## How to use it

Two switches, both needed:

1. On the **DirectionalLight3D** that is your sun, turn on **Light Shafts Enabled** (`light_shafts_enabled`). If
   several lights opt in, one of them is used.
2. On the **Environment** (WorldEnvironment), open **Light Shafts** and enable it.

| Property | Default | Effect |
|---|---|---|
| `light_shafts_enabled` | `false` | Turns the effect on. Off, nothing is computed. |
| `light_shafts_intensity` | `1.0` | Brightness of the shafts. At `1.0` a pixel looking straight at the sun gets as much light again as the sky there. |
| `light_shafts_threshold` | `1.0` | Only sky brighter than this casts shafts (linear scene light before exposure and tonemapping, like glow's HDR threshold). Raise it to keep the shafts to the sun and its glare; lower it to let the whole sky cast them. |
| `light_shafts_decay` | `2.0` | How fast the shafts fade with distance, per frame height. `1 / decay` is their mean length as a fraction of the frame height: `2.0` gives shafts about half the screen long, `0.5` shafts that cross the whole frame. |
| `light_shafts_tint` | white | Multiplies the shafts' color, which is otherwise the color of the sky that casts them. |

Quality is a project setting, **Rendering > Environment > Light Shafts > Quality**
(`rendering/environment/light_shafts/quality`): Low (16 samples per pixel), Medium (32, the default) or High (64). The
shafts look the same at every quality; lower qualities are slightly noisier and cheaper.

Shafts are measured in fractions of the frame, so they look the same at every resolution. They fade out as the sun
leaves the frame (gone half a frame height past the edge) and are off while it is behind the camera.

## Example

A forest at sunset:

```gdscript
$Sun.light_shafts_enabled = true            # the DirectionalLight3D
var env: Environment = $WorldEnvironment.environment
env.light_shafts_enabled = true
env.light_shafts_threshold = 0.8            # the sky near the sun, not only the disk
env.light_shafts_decay = 1.0                # long shafts
env.light_shafts_tint = Color(1.0, 0.85, 0.6)
```

The sky must be bright near the sun for anything to scatter: a `ProceduralSkyMaterial` or `PhysicalSkyMaterial` with a
sun disk works out of the box. If you see no shafts, lower the threshold.

## How it works

The technique is the radial blur of Kenny Mitchell, "Volumetric Light Scattering as a Post-Process" (GPU Gems 3,
chapter 13), reformulated so the result does not depend on the sample count or the resolution:

1. **Mask** (a quarter of the frame's width and height): sky pixels (nothing drawn there) brighter than the threshold.
2. **Radial blur**: each pixel takes the mean of the mask along the line toward the sun, weighting each point by
   `exp(-decay * distance)` (a Beer-Lambert extinction, distances in frame heights). Each sample is itself averaged
   along the same line, so a small, bright sun is never missed between samples.
3. **Composite**: the result times intensity and tint is added to the scene's light, before depth of field, motion
   blur, auto exposure, glow and tonemapping, so the camera effects see it as real light.

## Limits

- It only scatters light that is **visible on screen**: a sun hidden behind a wall, or sky outside the frame, casts
  nothing. For shafts from off-screen light, or inside fog, use volumetric fog (Forward+).
- Orthogonal cameras get parallel shafts along the sun's direction.
- **Compatibility**: the frame is clipped before the shafts see it (at about 30 in linear light with a 10-bit 3D
  buffer, otherwise at 1.0), so keep the threshold below that. Multiview (XR) viewports have no shafts there.
- **Mobile**: the frame's range tops out at about 2.0, so very bright suns give weaker shafts than in Forward+.
- Extremely bright suns can leave a faint fine pattern far from the sun; raise the quality or the threshold.

| Renderer | Supported | Cost |
|---|---|---|
| Forward+ | yes | about 0.2 ms at 1080p on an RTX 4080 SUPER |
| Mobile | yes | similar |
| Compatibility | yes (single view) | the passes plus Compatibility's post-process pass, which enabling forces |
| Web (WebGPU) | yes | 0.07 ms at 1152x648 measured in Chrome (four passes of 0.015-0.024 ms) |

Enabling the effect in Compatibility changes the frame very slightly (at most 2/255) even when no light opts in, since
it switches that renderer to its post-processing path.

Images: `images/look.jpg` (Forward+ off and on, WebGPU on, Mobile on) and `images/bars_model.jpg`.
