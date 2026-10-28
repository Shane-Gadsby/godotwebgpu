# For TASKS.md (task 64)

## §4 entry

### 4.xx Color.from_temperature() and one colour-temperature model (October 2026) *(work task 64)*

User request (2026-10-28, camera-sim). The Krystek 1985 Planckian-locus fit was written out three times (Light3D,
white balance storage, `camera_metering.glsl`) and unreachable from scripts, so camera-sim copied its coefficients.
`core/math/color_temperature.h` now holds it once (`ColorTemperature::planckian_uv<T>()`, `planckian_linear_srgb()`,
`normalize()`); Light3D and `RendererCameraAttributes` call it, the GLSL keeps a commented copy. New
`Color.from_temperature(kelvin, normalization = TEMPERATURE_NORMALIZATION_LUMINANCE)` (linear, 1000-15000 K;
`TEMPERATURE_NORMALIZATION_MAX_CHANNEL` is Light3D's physical-light-units normalization). Verified: Light3D's
correlated colour bit-identical over 500-20000 K, renders byte-identical in all three renderers with and without
physical light units, within 2.4e-6 of camera-sim's function, unit test in `test_color.cpp`. A light of
`from_temperature(T)` under a white balance of T matches a 6500 K light at the default within 0.05-0.08%.

## §3 trap candidates

- **A templated port of float code is bit-identical only if every literal rounds the same.** `T(0.860117757)` (decimal
  → double → float) can differ from `0.860117757f` (decimal → float) by double rounding. Checked all 11 Krystek
  coefficients with a 5-line C program before relying on it; check again when porting other float constants.
- **GDScript cannot reach a static builtin method through `Color.call(...)` or `Expression`** (`Color.from_temperature`
  through Expression fails to parse: "Expected '('"). A test script that must also parse in an older editor loads a
  second script on demand that names the method. Cost one GPU-lease round trip.
- **Compatibility's viewport `get_image()` is 8-bit sRGB even with `rendering/viewport/hdr_2d`** (format 11, values
  sRGB-encoded), while Forward+ and Mobile return RGBAH linear (format 14). Decode before comparing renderers.

## Questions for the user

1. **`c64-csharp`**: C# has no `Color.FromTemperature()`: C#'s `Color` is hand-written and builtin methods are not
   generated. (a) **Leave it GDScript-only for now (default)**; (b) port it to `Color.cs`, a second copy of the
   formula (with a C# test against the C++ values).
2. **`c64-enum-names`**: the constants are `Color.TEMPERATURE_NORMALIZATION_LUMINANCE` / `_MAX_CHANNEL` (Godot's
   prefix convention, like `Vector3.AXIS_X`). (a) **Keep (default)**; (b) shorter names.

## Note for the camera-sim session

Task 65 is done too, so camera-sim does not need `Color.from_temperature()` for its lights: set
`light_temperature_enabled = true` on each light and write the temperature to `light_temperature` (see task 65's
note). For anything that is not a light (a swatch, an emissive bulb, a readout),
`Color.from_temperature(kelvin)` returns exactly what `CinemaSettings.light_color_for_temperature()` returned before
its `.linear_to_srgb()` (within 2.4e-6), so its body can become
`return Color.from_temperature(kelvin).linear_to_srgb()` and the coefficients can go.
