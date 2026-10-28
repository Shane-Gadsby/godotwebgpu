# For TASKS.md (task 48, GTAO)

## §4 entry (new)

**4.xx GTAO ambient occlusion (work task 48, 2026-10-28).** Project setting `rendering/environment/ssao/method`
(ASSAO default | GTAO), RS `environment_set_ssao_method()`. `shaders/effects/gtao.glsl` ports Intel's XeGTAO (MIT;
Jimenez et al. 2016): prefilter (view-z + 4 mips, one dispatch), main pass, XeGTAO denoise, depth-aware upsample for
half size. `SSEffects::generate_gtao()` writes the same `RB_SCOPE_SSAO/RB_FINAL` texture as ASSAO, so the scene shader
and SSAO debug draw are untouched; the RT AO of §2.34 can be a third method writing the same texture. Environment SSAO
settings reused (intensity/power relative to their defaults give XeGTAO's reference result). Quality = slices x steps:
2x2, 3x2, 3x3, 9x3, 9-18x4 (XeGTAO's 1x2 and 2x2 measured too dark/noisy without TAA). Against a numpy ray traced
reference: GTAO Medium MAE 0.028, corr 0.90, crease bias +0.05 vs ASSAO's best 0.037 / 0.83 / +0.15; WebGPU matches
native to 0.001. Cost at 1080p: Medium half 0.31 ms (ASSAO default 0.29 ms). Default bit-identical. Forward+ matrix
arm `gtao` (98 arms). Forward+ and web only; Mobile has no SSAO, Compatibility keeps its own. Results:
`webgpu_notes/work/results/48-pp-gtao/`.

## §2.48 / survey

SURVEY entry 6 (pp-gtao) is built; the "decide after the RT AO decision" note is resolved by the shared output texture.

## §3 trap (candidate)

**WGSL clamp() keeps NaN.** A non-finite value that a Vulkan driver's clamp()/min()/max() happens to scrub stays NaN
in WGSL on Chrome, and an R8/RG8 store of NaN reads back as 0: GTAO's sky came out black on the web only (infinite
background depth -> NaN denoiser weights). Keep linear depths finite (clamp to the storage format's range) and test
background pixels explicitly with `!(x < limit)`, which is also true for NaN. Cost ~30 min.

## CLAUDE.md

- Forward+ feature matrix arm count: +1 (`gtao`).

## README

- Feature list: "GTAO ambient occlusion (project setting, Forward+ and web)" if the README lists engine additions.

## Questions for the user

- **pp-gtao-taa-noise** (choice, default first): With TAA on, GTAO changes its noise pattern every frame (XeGTAO's
  design: TAA averages it into a cleaner result), which leaves a slight shimmer (final-image temporal std 0.16/255 vs
  ASSAO 0.02). Options: (1) keep XeGTAO's per-frame noise under TAA [default]; (2) keep the noise fixed always (no
  shimmer, AO as noisy as without TAA).
