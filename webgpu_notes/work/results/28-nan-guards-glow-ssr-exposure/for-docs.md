# For the shared docs (task 28)

## webgpu_site/CORRECTNESS_AND_COMPATIBILITY.md — replace §2.9b

### 2.9b Non-Finite Colors in Post Effects

The bokeh depth of field, glow, auto exposure and the previous-frame copy SSR reflects treat a non-finite scene color
as black (`finite_or_zero()` in `servers/rendering/renderer_rd/shaders/effects/finite_inc.glsl`, tested on the
exponent bits), so a stray NaN from a user shader shows as one black pixel instead of a black bokeh shape, a glow blob,
a smeared reflection or a blank frame (`webgpu_notes/TASKS.md` §4.41, §4.44, §3 trap 70). A deliberate divergence from
upstream Godot; finite frames are bit-identical.

## webgpu_tests/README.md

- Forward+ matrix row: 84 arms → 87 (new `glow_nan`, `ssr_nan`, `auto_exposure_nan`, `maxDelta` arms).
- `dof_nan/run.sh` description: one NaN pixel through every bokeh DOF mode, glow, SSR (Forward+) and auto exposure,
  natively on Forward+ and Mobile; `ONLY=dof,glow,ssr,exposure` narrows it.

## CLAUDE.md

- The Forward+ matrix line says "82 arms"; it is 87.

## Wiki, Engine-Additions-Notable-Fixes.md — replace the "Depth of field" row

| Depth of field, glow, SSR, auto exposure | One pixel holding NaN or infinity, for example from a custom shader, turned every pixel within the depth of field's blur radius black in the bokeh's shape, lit a glow blob around itself, smeared black across screen-space reflections, or blanked the whole frame through auto exposure. These effects now treat a non-finite color as black, so the fault stays one pixel. Frames without such a pixel are unchanged. |

## Upstream-sync note (TASKS / site, wherever §4.41's divergence note lives)

The guard touches `copy.glsl`, `blur_raster.glsl`, `luminance_reduce*.glsl`, `bokeh_dof*.glsl` (new `finite_inc.glsl`);
an upstream sync may conflict there.
