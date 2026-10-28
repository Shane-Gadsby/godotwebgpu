# Task 64: Color.from_temperature() and one shared colour-temperature model

Branch `work/64`, commit `9f7b0c01d2` (task 65 follows on the same branch, `d86f09e86c`).

## What changed

- **`core/math/color_temperature.h`** (new): the engine's one blackbody model.
  `ColorTemperature::planckian_uv<T>()` is the Krystek 1985 uv fit (as Filament uses it), templated so the white
  balance keeps double precision and Light3D its float arithmetic; `planckian_linear_srgb(float)` is uv → xy → XYZ
  (Y = 1) → linear sRGB, operation for operation what Light3D always computed (including its one double
  reciprocal); `normalize(linear, mode)` applies MAX_CHANNEL (Light3D's, divide by the max then clamp) or LUMINANCE
  (clamp negatives, divide by Rec. 709 Y, no upper clamp, camera-sim's).
- **`Color.from_temperature(kelvin, normalization = TEMPERATURE_NORMALIZATION_LUMINANCE)`**, static, bound with
  the builtin enum `Color.TemperatureNormalization` (`TEMPERATURE_NORMALIZATION_LUMINANCE`,
  `TEMPERATURE_NORMALIZATION_MAX_CHANNEL`; `VARIANT_ENUM_CAST` in `variant_caster.h`). Returns **linear**, clamps
  kelvin to 1000-15000 K. Docs say to use `.linear_to_srgb()` for color properties and swatches, cite Krystek /
  Filament, and say it is the white `white_balance_temperature` balances to.
- `scene/3d/light_3d.cpp` `_color_from_temperature()` and `camera_attributes_storage.cpp`
  `_white_balance_planckian_uv()` call the helper. Light3D does *not* clamp the temperature (it never did), so it
  calls `planckian_linear_srgb()` directly rather than `from_temperature()`.
- `camera_metering.glsl` keeps its `planck_uv()` copy, now commented as a copy of the C++ one (comment only).
- Unit test `[Color] from_temperature` in `tests/core/math/test_color.cpp` (the file is `.cpp` in 4.8, not `.h`).
- `doc/classes/Color.xml`: method and both constants.

## Verification (numbers)

- **Bit-identical Light3D:** `get_correlated_color()` dumped as `var_to_bytes` hex for 500-20000 K in 13.7 K steps
  plus 6 fixed values (1430 values), physical light units on: pre-change editor vs this one, `cmp` identical
  (`tools/corr.gd`). Also identical after task 65.
- **Bit-identical renders** (pre-change editor vs post-64 editor, HDR captures saved as EXR, compared byte for byte):
  9/9 cases identical in Forward+, Mobile and Compatibility, with and without physical light units (lights at
  2500/4000/6500/9000 K and a colored light; white balance 6500 and 3200 K, so the white-balance gains are covered).
- **camera-sim match:** worst |difference| from camera-sim's `light_color_for_temperature()` (verbatim copy, before
  its `linear_to_srgb()`) is **2.4e-6** over every whole Kelvin from 1000 to 15000; at 1000/2000/3200/5600/6500/
  10000/15000 K: 1.4e-6, 4.8e-7, 2.4e-7, 6.0e-7, 6.0e-7, 8.3e-7, 1.3e-6 (`tools/cs_out.txt`). Requirement: 1e-5.
- **LUMINANCE** Y = 1 within 7e-8; **MAX_CHANNEL** max channel exactly 1, none negative (unit test, every 250 K).
- **6500 K** = (1.0440, 0.9833, 1.0359): close to neutral, not exactly (D65 sits off the locus); documented.
- **Under a white balance of T** (white card, Linear tonemapper, light_color = `from_temperature(T).linear_to_srgb()`,
  T = 2500/3200/4300/5600/6500/8000 K): the card matches the 6500 K light at the default white balance within
  **0.05%** (Forward+), 0.08% (Mobile), 0.01% (Compatibility), the model's promise. It is **not neutral within 1%**:
  r/g = 1.062, b/g = 1.054 at every T, because that 6500 K reference light itself is r/g 1.06 (see the question in
  for-tasks.md). MAX_CHANNEL gives the same chromaticity (r/g 1.0617, b/g 1.0536 at every T).
- Unit tests: `--test --test-case="*Color*"`: 25 test cases, 1796 assertions, all pass.
- Editor built with `dev_mode=yes` (werror), web `template_debug` and `template_release` (nothreads, no dlink) built.
- Doctool run; only Color.xml (and Light3D.xml for 65) changed. `make_rst.py --dry-run`: no errors in these classes.

## Not done / skipped (lead instruction 2026-10-28: only the directly related tests)

- No Forward+ matrix, scene smoketest, perf_guard, full unit-test run or other tiers. **smoke.sh not run**: no shader
  interface changed (the only shader edit is a comment in `camera_metering.glsl`).
- **C#:** the mono module is not in this editor build (`module_mono_enabled` off), so no glue was generated. It would
  not change anyway: C#'s `Color` is hand-written (`modules/mono/glue/GodotSharp/GodotSharp/Core/Color.cs`) and the
  bindings generator does not read builtin methods, so C# has no `Color.FromTemperature()` (question in for-tasks.md).
- Compatibility ran on llvmpipe (xvfb has no GLX on the NVIDIA card), as earlier camera tasks did.

## Risks

- `color.h` gained an enum and a static: every TU recompiles once. The enum is a new builtin-class enum, the first one
  on `Color`; GDScript, the extension API and docs handle it like `Vector3.Axis`.
