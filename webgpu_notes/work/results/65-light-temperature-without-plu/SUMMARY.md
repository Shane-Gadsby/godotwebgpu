# Task 65: Light3D.light_temperature_enabled (light_temperature without physical light units, constant luminance)

Branch `work/64`, commit `d86f09e86c`, on top of task 64's shared helper (`9f7b0c01d2`).

## What changed

- **`Light3D.light_temperature_enabled`** (bool, default `false`; `set_temperature_enabled` /
  `is_temperature_enabled`), in the Light group just above `light_temperature`, always visible.
  - **On:** `light_temperature` tints the light with or without physical light units, with
    `Color.from_temperature(T, LUMINANCE)`: Rec. 709 Y = 1, unclamped (channels > 1 allowed), so brightness never
    changes with temperature. Stored sRGB-encoded in `correlated_color` like before and multiplied with
    `light_color` in linear space, exactly as the physical-light-units path always did.
  - **Off:** unchanged. Without physical light units the temperature is ignored (correlated colour white); with
    them the old brightest-channel normalization stays.
  - **With physical light units and the opt-in on**, the light switches to constant-luminance normalization (my
    choice; question `c65-plu-interaction` below). Documented in Light3D.xml.
- `_validate_property()` shows `light_temperature` when the opt-in is on (and, as before, with physical light units).
- `get_correlated_color()`, the gizmo (already multiplies by it) and **LightmapGI's bake** (now multiplies the tint in
  when `use_physical_light_units || is_temperature_enabled()`) follow the opt-in. Turning the opt-in off resets the
  correlated colour to white (or the physical-light-units colour).
- Light3D refactor: one `_update_light_color()` (RS colour + gizmo) and `_update_temperature()`; arithmetic of the old
  paths unchanged.
- Docs: Light3D.xml (`light_temperature_enabled`, `light_temperature`, `get_correlated_color`), Color.xml cross-reference.

## Verification (numbers; scene: white card albedo 0.5, one OmniLight3D, Linear tonemapper, no ambient, HDR captures)

Native on the RTX 4080 SUPER (Forward+, Mobile; Vulkan) and Compatibility (llvmpipe, OpenGL; 8-bit sRGB capture,
decoded); web on headed system Chrome, adapter asserted nvidia/lovelace, not fallback, 48 sampled textures.

| Check | Forward+ | Mobile | Compatibility | Web (WebGPU Forward+) |
|---|---|---|---|---|
| Constant luminance, opt-in on, WB 6500, 2000-10000 K (10 temps), spread of Y | 0.015% | 0.042% | 0.037% | 0.25% (8-bit canvas) |
| Neutral pairs (opt-in, WB = T, 2500/3200/4300/5600/6500/8000 K): worst deviation from the 6500 K light at default WB | 0.050% | 0.076% | 0.011% | 0.103% |
| Same, as absolute neutrality r/g, b/g | 1.062, 1.053 | 1.062, 1.053 | 1.062, 1.054 | 1.060, 1.055 |
| Opt-in off, no physical light units, vs pre-change editor (9 cases, EXR bytes) | 9/9 identical | 9/9 | 9/9 | - |
| Physical light units on, lights without opt-in, vs pre-change editor | 9/9 identical | 9/9 | 9/9 | - |
| Opt-in on, physical light units on, neutral pairs | 0.047% | 0.094% | not measurable (8-bit capture rounds the dim PLU frame to 0) | - |

- **The 1% neutral criterion is met relative to the white-balance model, not absolutely.** The white balance maps T
  onto the *6500 K Planckian* white (§4.42: "a light of T under WB T renders exactly like the default 6500 K light"),
  and that light is slightly magenta in sRGB (r/g 1.062, b/g 1.054, because D65 is off the locus). Every pair renders
  that same colour within 0.1%. camera-sim's GDScript version measures the same (it uses the same fit). Question
  `c65-neutral-reference` below.
- **Web vs native Forward+:** within 0.3% for every channel above 0.1 linear; up to 3% on channels below 0.07
  (8-bit canvas quantisation: e.g. blue 0.005 at 2000 K).
- **get_correlated_color()** with the opt-in: Y = 1.000000 and equal to `from_temperature()` for Omni/Spot/Directional,
  with and without physical light units; white again after turning it off (without physical light units);
  save/load round-trip keeps it (`tools/props.gd`: PASS in both projects).
- **Bit-identical correlated colour** (opt-in off) over 1430 temperatures, with and without physical light units.
- **Inspector:** property usage checked headless: `light_temperature` has `PROPERTY_USAGE_EDITOR` off/on =
  false/true without physical light units, true/true with them; `light_temperature_enabled` always shown.
- Builds: editor `dev_mode=yes` (werror) clean; web `template_debug` and `template_release` (nothreads, no dlink,
  gdscript_braces, raycast) compiled; the web test exported from `template_debug`.

## Skipped (lead instruction 2026-10-28: only the directly related tests)

Forward+ matrix, scene smoketest, perf_guard, other tiers; Firefox; full unit-test run (Color tests only).
smoke.sh not run (no shader interface change). LightmapGI bake with the opt-in not rendered (code path reviewed only).
No by-eye look in the inspector GUI (visibility checked through the property list).

## What the lead must still run

`--test --test-case="*Color*"` on the merged tree; optionally `tools/gpu64.sh` (needs the pre-change editor; paths
in the scripts point at my scratch directory).

## Risks

- A light with the opt-in under physical light units changes brightness vs the old normalization (by design, opt-in).
- `correlated_color` can now hold sRGB-encoded values above 1 (documented in `get_correlated_color()`). Code reading
  it with `srgb_to_linear()` (gizmo, LightmapGI) handles that; the gizmo re-normalises V to 1 anyway.
