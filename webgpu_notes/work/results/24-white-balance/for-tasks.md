# 24-white-balance: text for TASKS.md

## §4 entry (new, after §4.40)

### 4.41 Camera white balance on CameraAttributes `DONE 2026-10-26` (work task 24)

User request: a White Balance attribute on `CameraAttributesPhysical` and `CameraAttributesPractical`, as on a real
camera. Added on the shared base `CameraAttributes` (both inherit it, and exposure already lives there):
`white_balance_temperature` (Kelvin, 1000-15000, default 6500) and `white_balance_tint` (-1..1, default 0), plus
`RenderingServer.camera_attributes_set_white_balance(rid, temperature, tint)`.

Model (written up in `camera_attributes_storage.cpp` and the wiki page): camera semantics, the temperature is the
illuminant the camera is set for. Illuminant on the Planckian locus with the Krystek approximation `Light3D` already
uses for `light_temperature`, tint along the isotherm at 0.05 Duv per unit (Unreal's `WhiteTint` scale; positive =
magenta image, as in Lightroom), von Kries adaptation in Bradford cone space onto the same model's 6500 K / tint 0
white. Consequences: the default is exactly the identity, and a light of temperature T under a white balance of T
renders exactly like the default 6500 K light (checked numerically: identical normalized RGB at 3200 K and 10000 K).

Plumbing: `RendererCameraAttributes` computes three cone gains on the CPU (`white_balance_compute_gains()`); the shaders
apply linear sRGB -> LMS, the gains, LMS -> linear sRGB with constant matrices, so only 12 bytes cross. That was
forced by the Forward+ tonemap push constant, which was 112 bytes against Vulkan's 128-byte guarantee: a full 3x3 does
not fit, the gains do (now exactly 128, with a `static_assert`). Applied in linear scene-referred color after exposure,
FXAA (which resamples the source) and pre-tonemap glow, before tonemapping; the softlight glow is balanced on its own
before its tonemap. Skipped when exactly 6500 K / 0 (Forward+: push-constant flag bit 7; Mobile: spec constant 18
`use_white_balance`, `TONEMAP_MOBILE_ADRENO_BUG` moved to bit 19; Compatibility: a `vec4` in the Tonemap UBO checked by
a uniform branch, applied at both exposure sites in `scene.glsl` and in `sky.glsl`).

Interface references regenerated on purpose: the only change is override id 18 in the two `tonemap_mobile` fragment
variants. Forward+ matrix: arm `white_balance` (3200 K, tint 0.2), 83 arms.

Verification: see `webgpu_notes/work/results/24-white-balance/SUMMARY.md` (default bit-identical against the pre-change
editor in all three renderers; 3200 / 10000 K / ±0.5 tint within 1/255 of the model's prediction in all three
renderers and on WebGPU).

Known gap: Compatibility does not white balance a `BG_CANVAS` background (copied with exposure only).

## Questions for the user (hand-offs page)

1. **Tint units** (choice). Default: **-1..1, 0.05 Duv per unit, positive = magenta image** (Unreal's scale, Lightroom's
   direction). Alternatives: Duv directly (-0.05..0.05, physically named but tiny numbers); a Lightroom-style -150..150.
2. **Temperature model** (choice). Default: **Planckian locus at every temperature** (matches `Light3D.light_temperature`,
   so light and camera settings line up exactly). Alternative: Unreal's split (Planckian below 4000 K, CIE daylight
   locus above), which matches photographic daylight a little better but no Godot light and has a small jump at 4000 K.
3. **Range** (choice). Default: **1000-15000 K** (the approximation's valid range, same as `Light3D`). Alternative:
   2000-10000 K like most camera bodies.
4. **Look** (look): frame triples 3200 / 6500 / 10000 K, native Vulkan beside WebGPU, in
   `results/24-white-balance/looks/` - does the blue / orange shift look like a camera's?
