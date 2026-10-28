# Task 40: text for TASKS.md

## §4 entry

### 4.56 Camera answers, wave 2: whole-frame Average, log-average metering, halation high quality, starburst 20 (October 2026) *(work task 40, camera batch)*

From the user's answers on the hand-offs page (`average-dropped-edges`, `metering-statistic`, `halation-min-size`,
`starburst-default-strength`):
- **Average auto exposure meters the whole frame** (upstream bug fixed; deliberately changes existing auto-exposed
  projects). `luminance_reduce.glsl` now writes 8x8 block *sums* into levels that round up (`LuminanceBuffers::configure()`,
  compute path only) and the last level divides by the frame's pixel count; `any()` -> `all()` for the in-range test.
  The raster path (Mobile) already covered the frame and is untouched (and Mobile's tonemapper never applies auto
  exposure anyway). Measured on task 36's meter scene: the metered value now equals the Python whole-frame mean to
  +0.0005 EV at 1080p and 720p; the old one matched the modeled upstream reduction to +0.0005 EV; the exposure moves
  -0.072 EV at 1080p and +0.323 EV at 720p (where the bottom 27% was dropped). Class docs say so.
- **`CameraAttributes.auto_exposure_metering_log_average`** (default false; RS
  `camera_attributes_set_auto_exposure_metering_log_average()`): Average, Center-weighted and Spot take
  exp(sum(w log(v + 1e-4)) / sum(w)) (Reinhard et al. 2002's key). `FLAG_LOG_AVERAGE` in `luminance_reduce.glsl` and
  `camera_metering.glsl`. Highlight-weighted is a percentile, unaffected (verified identical). Within +0.0005 EV of a
  Python reference in all four cases at both resolutions; WebGPU pixel-identical to native.
- **`CameraAttributes.halation_high_quality`** (default false; RS `camera_attributes_set_halation_high_quality()`):
  Forward+ builds the glare chain at full resolution (`_render_camera_glare_chain(..., p_full_resolution, ...)`, sigma
  calibration 2.68 as for the half-resolution chain, measured). Mobile unchanged (its chain already resolves radius 1).
- **`starburst_strength` default 20** (scene and storage; starburst stays off by default).

Measured halo size (HDR captures, least-squares fit of f (G_sigma * S - S) to the halo, which removes the source disc;
task 35's 6.82 px at radius 1 was an 8-bit measurement whose clipped disc widened it):

| 720p, Forward+ | model | standard | high quality |
|---|---|---|---|
| radius 0.5 | 1.68 px | 3.95 | 1.91 |
| radius 1 | 3.36 px | 3.95 | 3.38 |
| radius 4 | 13.44 px | 13.37 | 13.56 |
| radius 16 | 53.76 px | 54.40 | 54.44 |

At 1080p both chains run ~10% small from radius 1 up (standard 4.01 / 8.85 / 18.08 / 73.20 px, high quality 4.38 /
9.02 / 18.17 / 73.24 px for model 5.04 / 10.08 / 20.16 / 80.64), and the high-quality floor is 1.91 px (radius 0.5:
model 2.52). That is the chain's sigma-per-lod model (s 2^L), calibrated at 720p by task 35: lod 0 of the
half-resolution chain is really ~2.0 texels, not 2.68, and the growth per lod is not exactly 2. A per-lod table would
fix both chains at every resolution, but changes the existing (non-default) halation look; not done (see question).

## §3 trap (candidate)

**An 8-bit capture of a glare source measures the clip, not the glare.** Task 35 measured halation at radius 1 as 6.82
px against a 3.36 px model from 8-bit sRGB renders: the source disc saturates, so its apparent width enters the halo
fit. HDR 2D captures (RGBA16F, Linear tonemapper) fitted with the model convolved with the halation-off frame read
3.95 px for the same chain. Measure glare from HDR captures (task 40's `analyze_halo.py`).

## Questions for the user

- `glare-lod-calibration` (choice, default **leave as is**): the glare chain's blur-per-lod model is ~10% small at 1080p
  for halation radius >= 1 (correct at 720p, where it was calibrated). Options: leave as is (default; existing looks
  unchanged) / calibrate per lod from measured sigmas (halation, diffusion and flare blur sizes become exact at every
  resolution; existing glare looks change by up to ~10% in size).
