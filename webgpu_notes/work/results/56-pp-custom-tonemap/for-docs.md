# Wiki text: Custom tonemapper (Engine Additions > Post-processing)

## Custom tonemap curve

`Environment.tonemap_mode = Custom` (`TONE_MAPPER_CUSTOM`) gives you a film-style tone curve you shape yourself,
instead of the fixed curves of Filmic and ACES or AgX's single contrast control. Its five controls are the ones
Unreal Engine's Film tonemapper uses, with the same meaning and defaults, so a look built in Unreal can be carried
over by copying the numbers.

### What it does

The curve maps scene brightness (linear, HDR) to display brightness. Seen on a logarithmic brightness axis, it is a
straight line through the midtones, joined smoothly to a **toe** in the shadows and a **shoulder** in the highlights.
Mid gray (0.18) always comes out as 0.18, so changing the curve changes contrast and roll-off without making the
image brighter or darker overall.

| Property | Default | Effect |
|---|---|---|
| `tonemap_custom_slope` | 0.88 | Midtone contrast: the steepness of the straight part. Higher = darker shadows and brighter highlights around a fixed mid gray. |
| `tonemap_custom_toe` | 0.55 | Shadow compression: how much of the output range the toe covers (it ends at output `1 - toe`). Higher = deeper, more compressed shadows. |
| `tonemap_custom_shoulder` | 0.26 | Highlight compression: the output level where the shoulder starts. Higher = highlights stay linear longer and reach white sooner; lower = earlier, gentler roll-off. |
| `tonemap_custom_black_clip` | 0.0 | 0 = shadows approach black without reaching it; above 0 = the darkest values are crushed to pure black. |
| `tonemap_custom_white_clip` | 0.04 | 0 = highlights approach white without reaching it; above 0 = bright values clip to pure white (from a scene value of about 9.6 with the defaults). |

With the defaults the curve is Unreal's default Film curve, a close match of the ACES film look: 0.18 → 0.18,
1.0 → 0.72, 4.0 → 0.94, white from about 9.6.

### How to use it

1. Select your `WorldEnvironment`'s Environment, open **Tonemap**, set **Mode** to **Custom**. The five Custom
   properties appear (and White disappears: the curve has its own white, set by the shoulder and White Clip).
2. Set **Exposure** first so mid gray objects look right, then shape the curve: Slope for overall contrast, Toe for
   the shadows, Shoulder and White Clip for the highlights.

### Example

A punchy, high-contrast look with crushed blacks:

```gdscript
var env := $WorldEnvironment.environment
env.tonemap_mode = Environment.TONE_MAPPER_CUSTOM
env.tonemap_custom_slope = 0.98
env.tonemap_custom_toe = 0.6
env.tonemap_custom_shoulder = 0.3
env.tonemap_custom_black_clip = 0.02
env.tonemap_custom_white_clip = 0.04
```

A soft, low-contrast look that holds highlight detail: slope 0.7, toe 0.4, shoulder 0.15, white clip 0.0.

The same is available on the server: `RenderingServer.environment_set_tonemap_custom(env_rid, slope, toe, shoulder,
black_clip, white_clip)` with `RenderingServer.ENV_TONE_MAPPER_CUSTOM`.

### Limits per renderer

- **Forward+** (desktop and web/WebGPU): the full curve. Measured within 1/255 of the reference model; the web matches
  native.
- **Mobile**: the full curve, but the Mobile renderer stores the scene with values only up to 2.0, so nothing brighter
  reaches the shoulder: with the defaults the brightest output is about 0.86 (not white), as with every tonemapper on
  Mobile. Raise **Exposure** or the shoulder, or lower the slope, if highlights look dull.
- **Compatibility**: the full curve, applied in the scene shader like the other tonemappers.
- It is a curve for standard (SDR) output: the result is clipped at 1.0 (as with Filmic and ACES), so it does not use
  extra HDR headroom.
- It works per color channel (like Filmic): very saturated bright colors shift toward yellow/white as channels reach
  the shoulder at different times. Unreal also applies a small desaturation and works in a wider color space; this
  curve does neither (see the question `custom-tonemap-gamut`).
