# 24-white-balance: SUMMARY

State: ready-for-integration (branch `work/24`, worker-24, 2026-10-26)

## What changed

- **API**: `CameraAttributes.white_balance_temperature` (K, 1000-15000, default 6500) and `white_balance_tint` (-1..1,
  default 0), group "White Balance", on the shared base, so `CameraAttributesPhysical` and `CameraAttributesPractical`
  both have them. Exposure already lives on that base, and white balance is meaningful for both kinds of camera.
  `RenderingServer.camera_attributes_set_white_balance(rid, temperature, tint)`. Docs in `CameraAttributes.xml`,
  `RenderingServer.xml` (`--doctool` leaves them unchanged).
- **Model** (`servers/rendering/storage/camera_attributes_storage.cpp`): camera semantics. The illuminant is on the
  Planckian locus, computed with the same Krystek approximation `Light3D.light_temperature` uses. Tint moves it along
  the isotherm by 0.05 Duv per unit (Unreal's `WhiteTint` scale); positive means a magenta image, as in Lightroom.
  A von Kries adaptation in Bradford cone space maps it onto the model's own 6500 K / tint 0 white. Unreal does the
  same thing with CAT02, Planckian below 4000 K and daylight above it. So 6500 K is exactly the identity, and a light
  of temperature T under a white balance of T matches the default light (checked numerically in `wb_check/model.py`).
- **Plumbing**: the CPU computes 3 cone gains. The shaders apply constant RGB->LMS and LMS->RGB matrices, so only 12
  bytes cross; the full 3x3 does not fit in the Forward+ tonemap push constant (112 bytes, now exactly 128, with a
  `static_assert`). It is applied in linear color after exposure, FXAA and pre-tonemap glow, and before tonemapping
  (softlight glow is balanced separately). It is skipped at exactly 6500/0:
  - Forward+: flag bit 7.
  - Mobile: spec constant 18; `TONEMAP_MOBILE_ADRENO_BUG` moves to bit 19.
  - Compatibility: a Tonemap UBO `vec4`, read by a uniform branch in `scene.glsl` (both exposure sites) and `sky.glsl`.
- **References regenerated on purpose**: `interface_reference.json` and `scan_reference.txt`. The only change is the
  new override id 18 in the 2 `tonemap_mobile` fragment variants.
- **Forward+ matrix**: new arm `white_balance` (3200 K, tint 0.2, minDelta 16.0 = a third of the measured 48.1), so
  the matrix now has 83 arms.

## Verification (actual numbers)

- **Builds**
  - Native editor: `scons platform=linuxbsd target=editor webgpu=yes warnings=extra werror=yes` built clean.
    `dev_build=yes` was not built separately.
  - Web template: release, nothreads, non-dlink, raycast.
  - `pre-commit` is clean on all changed files.
- **Scratch scene** (`wb_check/`): a gray card (albedo 0.5) under a white directional light, plus an unshaded patch
  of linear (1, 0.5, 0.25), with linear tonemapping. The values below are the sRGB 0-255 output for card / patch:

  | setting | card | patch |
  |---|---|---|
  | 6500 K | 188 188 188 | 255 188 137 |
  | 3200 K | 123 196 255 | 207 198 226 |
  | 10000 K | 202 186 161 | 255 186 114 |
  | tint +0.5 | 213 177 214 | 255 177 159 |
  | tint -0.5 | 154 199 167 | 228 200 119 |

  - These match the model's prediction (`predict.py`) to within 1/255 on every channel.
  - Native results on the RTX 4080 SUPER (Vulkan Forward+, Vulkan Mobile, GL Compatibility) and on lavapipe/llvmpipe
    agree with each other to within 1/255.
  - **6500 K default is bit-identical to before the change**: 0 of 57,600 pixels differ, on the RTX and on lavapipe,
    in Forward+, Mobile and Compatibility. The baseline was the lead checkout's editor, built without the change.
  - **WebGPU Forward+** (headed system Chrome, adapter nvidia lovelace, not fallback, renderer forward_plus): all 5
    frames are pixel-identical to native Vulkan Forward+ (mean abs diff 0.0000, 0 differing pixels).
- **Forward+ matrix**
  - `run_forward_plus.mjs --parity` full run: **83/83 PASS**, unbaked. `white_balance` delta 48.141, native diff
    off 0.002 / on 0.004.
  - Neighbors run baked and unbaked (tonemap, tonemap_exposure, adjustments, glow, fxaa, debanding, auto_exposure,
    white_balance): 8/8 PASS each. The baked run used 442 baked shaders.
- **smoke.sh**:
  ```
  [WGSL Precompile] Results: 276 compiled, 0 glsl failures, 0 conversion failures
  identical interface: 276/276, mismatched: 0, missing in candidate: 0, not in reference: 0
  against the stored reference: 552/552 identical, 0 differ, 0 missing, 0 not in reference
  validation: 276 modules, 0 invalid, 0 of them expected
  SMOKE OK (smoke-20261026-172202-3806372)
  ```
- **Looks**: `looks/` holds native Vulkan beside WebGPU for the card at 3200 / 6500 / 10000 K and ±0.5 tint, plus the
  fp_check scene off/on.

## What the lead must still run

- Rebuild the editor and templates together from the merged tree. `wgsl_precompile` re-bakes `tonemap` on its own.
- `smoke.sh`, and `run_forward_plus.mjs --parity --feature white_balance` (and `--baked`), on the merged tree.
- `local_ci.sh --dev-mode` if you want the full CI flag set (`tests=yes` etc.). I only built with warnings=extra
  werror=yes.
- Fold `for-tasks.md` (§4 entry, questions for the hand-offs page) and `for-docs.md` (wiki page draft, sidebar, test
  README line) into the shared docs.

## Risks

- **Push constant at exactly 128 bytes** for the Forward+ tonemap. That is the Vulkan minimum, so it is fine, but
  there is now no headroom for future fields.
- **Mobile `ADRENO_BUG` bit moved 18 -> 19**. This is the same trick as before, just one id higher. It was checked on
  NVIDIA only.
- **Compatibility `BG_CANVAS` background is not white balanced**: it is copied with exposure only. This is
  documented as a limitation.
- **Strong settings (below ~2500 K) push saturated colors out of gamut**; they are clamped at 0 before tonemapping.
- **Defaults pending the user**: the tint units/scale, Planckian-only model and range are my defaults. They are
  listed as questions in `for-tasks.md`.
- **`wb_check` scripts contain my scratch paths**. Adjust `S=`/paths before reuse.
