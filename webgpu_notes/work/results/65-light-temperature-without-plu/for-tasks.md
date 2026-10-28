# For TASKS.md (task 65)

## §4 entry

### 4.xx Light3D.light_temperature_enabled: colour temperature without physical light units (October 2026) *(work task 65)*

User request (2026-10-28, camera-sim). `light_temperature` did nothing without physical light units, was hidden,
and its brightest-channel normalization made temperatures away from 6500 K darker. New per-light opt-in
`light_temperature_enabled` (default off): when on, the temperature tints the light with or without physical light
units at constant Rec. 709 luminance (`Color.from_temperature(T)`, task 64's shared model), multiplied into
`light_color` in linear space; under physical light units it also switches that light to the constant-luminance
tint. Off is unchanged: renders byte-identical to the previous editor in Forward+, Mobile and Compatibility with and
without physical light units. Inspector, `get_correlated_color()`, gizmo and LightmapGI follow it. Verified: luminance
spread across 2000-10000 K 0.015% (Forward+), 0.042% (Mobile), 0.037% (Compatibility), 0.25% (web, 8-bit); a light at
T under white balance T matches the 6500 K light at the default within 0.1% everywhere (that light itself is r/g 1.06,
b/g 1.05: the model's reference, §4.42).

## §3 trap candidates

- **"Neutral under white balance T" means "like the 6500 K Planckian light", which is not neutral** (r/g 1.062,
  b/g 1.054 in linear sRGB): the white balance's reference white is the locus at 6500 K, not D65. A 1% neutrality test
  of a white card fails by ~6% while the model works exactly; compare against the 6500 K light at the default instead.
  (Task 64/65 verification.)
- See task 64's for-tasks.md for the Compatibility capture format and the GDScript static-builtin-call traps.

## Questions for the user (defaults are built)

1. **`c65-plu-interaction`**: with physical light units on, what should `light_temperature_enabled` do?
   (a) **Switch that light to constant-luminance normalization (default, built)**: lumens/lux stop depending on the
   colour; (b) nothing (physical light units always use the old brightest-channel tint, the toggle only matters
   without them, and is hidden with them).
2. **`c65-neutral-reference`**: a light at T under a white balance of T renders like a 6500 K light, which is
   slightly magenta (r/g 1.06, b/g 1.05). (a) **Keep (default)**: physically the Planckian locus at 6500 K is not D65,
   and white balance's default stays exactly the identity; (b) normalize light tints to the 6500 K colour, so a 6500 K
   light (and any T under WB T) is exactly white: changes every temperature-tinted light and breaks the exact match
   with camera-sim's current function; (c) make the white balance target D65: the default would no longer be the
   identity, changing every existing render with a non-default white balance.
3. **`c65-default-on-for-new-lights`**: should new lights created in the editor get the opt-in on? (a) **No, default
   off everywhere (default)**; (b) on for new lights only.

## Note for the camera-sim session

Landed on `work/64` (tasks 64 and 65). In camera-sim:
- Set `light_temperature_enabled = true` on `%KeyLight`, `%FillLight`, `%RimLight` and `%DirectionalLight3D` (in the
  scenes or in code), leave their `light_color` white, and write the temperature sliders to `light_temperature`.
- Delete `CinemaSettings.light_color_for_temperature()`. If anything else still wants the colour (a swatch, an
  emissive), `Color.from_temperature(kelvin).linear_to_srgb()` is the same value (within 2.4e-6 before encoding).
- Brightness: the tint has Rec. 709 luminance 1 at every temperature, the same normalization camera-sim used, so the
  exposure model and light meter stay valid. Measured: card luminance constant within 0.015-0.04% from 2000 to
  10000 K.
- The project does not use physical light units, so nothing else changes. With the white balance at the key light's
  temperature, the card renders like a 6500 K light at the default white balance (r/g 1.06, b/g 1.05 in linear sRGB),
  exactly as camera-sim's own function did.
- `get_correlated_color()` returns the tint sRGB-encoded (values above 1 possible).
