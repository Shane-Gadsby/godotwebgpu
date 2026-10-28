# For TASKS.md and README (task 36: exposure automation)

## TASKS.md §4 entry (new subsection after §4.52)

### 4.5x CameraAttributes exposure automation: metering modes and auto white balance (2026-10-28, work task 36)

Auto exposure metering modes (Average, Center-Weighted, Spot, Highlight-Weighted) and auto white balance on
CameraAttributes (both kinds), Forward+ and WebGPU Forward+; Mobile and Compatibility keep Average and the manual white
balance (documented, warned).

- **Metering** (`auto_exposure_metering_mode`, `_spot_size` [% of the frame], `_spot_point` [UV], `_highlight_percentile`):
  Average is the original `luminance_reduce.glsl` reduction, untouched. The others are `CameraMetering`
  (`renderer_rd/effects/camera_metering.*`, `camera_metering.glsl`): weighted sums of (w·max RGB, w) down a round-up
  RGBA32F chain, or a 256-bin log2 histogram (1/8 EV, workgroup atomics) for the percentile; a resolve pass applies the
  same adaptation/clamp as `WRITE_LUMINANCE` and writes `LuminanceBuffers::reduce[last]`, which is swapped in as
  `current`, so the tonemapper, glow and everything else that reads the exposure texture is unchanged. Center-weighted =
  Nikon's 75% in a 12 mm circle of a 24 mm frame height; spot default 2.5% of the frame; highlight places the
  percentile at 1.0 at the default `auto_exposure_scale` 0.4 (`AUTO_EXPOSURE_HIGHLIGHT_KEY`).
- **Auto white balance** (`white_balance_auto`, `_speed`, `_min_temperature`, `_max_temperature`): gray world over
  near-gray pixels (Gaussian weight sigma 0.25 on ln(R/G), ln(B/G) after the current correction; Huo et al. 2006
  iterated by the frame loop; pixels the tonemapper would clip are rejected), the estimate clamped as CCT/Duv on the
  Krystek locus, adapted in CIE 1960 uv (time constant 1/speed), gains to display white (R = G = B) in the white
  balance's Bradford cone space. State: two 2x1 RGBA32F textures (gains; uv + valid), ping-ponged per viewport
  (`MeteringBuffers`). Manual temperature/tint stay on top as a bias and are the starting point.
- **Tonemapper**: Forward+ `tonemap.glsl` set 1 binding 2 (`camera_exposure_inc.glsl`), read when
  `CameraEffectsUBO.header.z & EXPOSURE_AUTO_WHITE_BALANCE`; that bit is set only when the renderer ran the AWB passes
  (`CameraEffectsContext::auto_white_balance`). `camera_attributes_uses_effects_buffer()` deliberately does **not**
  include it: Mobile and Compatibility cannot run AWB, so it must not cost Mobile its subpass or force Compatibility's
  post pass.
- Verified (worker): defaults pixel-identical to the pre-change editor in 18/18 renders (3 renderers x 6 scenes,
  including Average AE and manual white balance); metering within 0.0005 EV of a Python reference (weighted modes) and
  0.03 EV (histogram) at 1080p and 720p; AWB converges a 3200 K-lit gray card from r/g 1.94 to 1.0000 (b/g 1.0002,
  |duv| 1e-5), matching an independent Python estimator; clamp and bias match predictions; WebGPU vs native in SUMMARY.
- Interface references: the AWB texture in the four tonemap fragment variants, 7 new camera_metering modules.

## README engine additions line

- **Camera exposure automation** (CameraAttributes): auto exposure metering modes (center-weighted, spot,
  highlight-weighted) and auto white balance that follows the light, on the GPU without readback, Forward+ and the web.

## §2 follow-ups (not done here)

- **Average metering ignores part of the frame (pre-existing, upstream Godot).** `luminance_reduce.glsl` sizes each
  level `floor(size / 8)`, so the blocks past the last whole one are dropped: at 1920x1080 the rightmost 384 columns
  (20%) and the bottom 56 rows never count; at 1280x720 the rightmost 256 columns and the bottom 192 rows (27%). Measured on the metering test scene:
  the original reduction reads 0.978 where the frame mean is 1.028 (1080p) and 1.278 vs 1.021 (720p). It also tests
  `any(lessThan(pos, size))` where `all` was meant. The new modes use a round-up chain and are exact. Fixing Average
  changes the exposure of existing projects, hence a question below.
- Metering modes and AWB on Mobile: possible with a raster reduction (the luminance raster path exists), but Mobile has
  no auto exposure in its tonemapper at all today.
- A Forward+ matrix arm (`exposure_automation`) with `--parity`: AE and AWB adapt over time, so it needs the matrix to
  hold a frame count long enough to converge.

## Questions for the user

1. **Metering statistic: arithmetic or log-average.** The task text says "log-average luminance, with weights". The
   new modes use the **arithmetic** (weighted) mean of the same value Average meters, because a reflected-light meter
   integrates light linearly (ISO 2720's exposure equation uses the average luminance) and because then switching
   Average to Center-Weighted on an evenly lit scene changes nothing; a log-average would meter a contrasty frame up to
   a few stops darker than Average does.
   - **Arithmetic, like Average (default, built).**
   - Log-average (Reinhard 2002's key): darker exposure for high-contrast frames than Average gives, so switching modes
     also changes the overall brightness.
2. **Fix Average's dropped edges?** (see the follow-up above)
   - **Keep Average as is (default, built)**: existing projects are bit-identical.
   - Fix it (meter Average through the new exact chain): existing auto-exposed projects get brighter or darker by up to
     about a third of a stop, depending on what sits at the right and bottom edges.
3. **What auto white balance makes neutral.** Display white (R = G = B), as a camera does, or the manual model's own
   6500 K Planckian white (slightly magenta of display white, Duv -0.003), which is what a 6500 K light looks like at
   the default manual setting.
   - **Display white (default, built)**: a gray card comes out exactly gray; a white (1, 1, 1) light is left alone.
   - The manual model's 6500 K white: consistent with "a light set to T under white balance T looks like the default
     6500 K light", but a gray card under a plain white light gets a faint magenta cast when AWB is turned on.
