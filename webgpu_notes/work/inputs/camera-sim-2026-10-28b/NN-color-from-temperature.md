# NN-color-from-temperature: expose the engine's colour-temperature formula to scripts

State: open
Owner:
Claimed:
Needs: build (editor), doctool
Branch: work/NN
Results: webgpu_notes/work/results/NN-color-from-temperature/

User request, 2026-10-28, from the camera-sim session. Companion to NN-light-temperature-without-plu. Either
task alone lets camera-sim drop its own copy of the formula, so if only one gets done, NN-light-temperature-without-plu
is the more useful. This one is small, and also helps anything that isn't a light: emissive materials, UI
swatches, a "colour of this light" readout.

## The problem

The engine has one blackbody approximation (the Krystek uv fit of the Planckian locus), written out three times:
- `_color_from_temperature()` in `scene/3d/light_3d.cpp` (normalised to its brightest channel, clamped, sRGB-encoded);
- `_white_balance_planckian_uv()` in `servers/rendering/storage/camera_attributes_storage.cpp`;
- `camera_metering.glsl` (auto white balance).

None of them is reachable from a script, so a project that wants "the colour of a 3200 K light, matching the
engine's white balance" copies the coefficients, as camera-sim's `CinemaSettings.light_color_for_temperature()`
does. A copy can silently drift if the engine's fit ever changes.

## Goal

- `Color.from_temperature(kelvin: float, normalization: ColorTemperatureNormalization = LUMINANCE) -> Color`
  (static, like `Color.from_hsv`). It returns the **linear** colour of a blackbody at `kelvin` (clamped to
  1000–15000 K, the fit's range), with:
  - `LUMINANCE`: Rec. 709 luminance 1, so channels may exceed 1. This is what camera-sim uses: changing a
    temperature never changes brightness.
  - `MAX_CHANNEL`: brightest channel 1, matching what `Light3D` uses today with physical light units.
  Linear rather than sRGB-encoded, because it's a light or emission value; document `.linear_to_srgb()` for
  display. If the engine's API conventions prefer encoded colours for `Color` statics, say so in the docs
  and offer both via a parameter.
- **One shared implementation** (e.g. a small `Math` / `Color` helper in `core/`) that `light_3d.cpp` and the
  white-balance storage call, so the fit lives in one place. The GLSL copy in `camera_metering.glsl` stays (the
  shader needs it), with a comment pointing at the C++ one. Light output must stay bit-identical.
- Class docs: the formula's source (Krystek 1985, as used by Filament), the range, both normalisations, and
  that it's the same white `CameraAttributes.white_balance_temperature` balances to.

## Verification

- `Color.from_temperature(6500.0)` is close to neutral, and `from_temperature(T)` under a white balance of T
  renders a white card neutral within 1% for T = 2500–8000 K (one test scene, Linear tonemapper).
- `LUMINANCE` results have luminance 1 within float error; `MAX_CHANNEL` results have a largest channel of 1.
- Matches camera-sim's `CinemaSettings.light_color_for_temperature()` (before its `.linear_to_srgb()`) within
  1e-5 at 1000, 2000, 3200, 5600, 6500, 10000 and 15000 K.
- `Light3D` with physical light units: bit-identical output before and after the refactor.
- Doctool run; GDScript and C# bindings build.

## camera-sim afterwards

If NN-light-temperature-without-plu isn't done, camera-sim replaces the body of
`CinemaSettings.light_color_for_temperature()` with `Color.from_temperature(kelvin).linear_to_srgb()` and drops
the coefficients. If it is done, camera-sim uses `light_temperature` and doesn't need this.
