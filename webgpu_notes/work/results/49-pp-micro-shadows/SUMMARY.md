# Task 49: micro shadows (HDRP style, Environment)

Branch `work/49` on webgpu-4.8 at 7b978e6baa. Worker-49, 2026-10-28.

## What changed

`Environment.micro_shadows_enabled` (false) / `micro_shadows_opacity` (1.0, HDRP's default), group "Micro Shadows";
RS `environment_set_micro_shadows(env, enable, opacity)`. Each directional light is multiplied by
`mix(1, saturate(N.L + 2 AO^2 - 1), opacity)`, left at 1 where N.L < 0: "The Technical Art of Uncharted 4" (Brinck and
Maximov, SIGGRAPH 2016), the same as HDRP's `ComputeMicroShadowing()` and its N.L >= 0 guard in
`ShadeSurface_Directional()`. N is the normal-mapped normal. AO is the material's own (texture, shader `AO`, decals),
captured before SSAO and before `ao_light_affect`, as in HDRP's forward path.

- **Forward+ / Mobile** (`micro_shadow()` at the end of `scene_forward_lights_inc.glsl`, applied after `blur_shadow()` in
  each per-pixel directional loop, behind `if (scene_data.micro_shadow_opacity > 0.0)`). The value lives in
  `SceneData`'s old `uint pad` after `reflection_color`, now `float micro_shadow_opacity` (GLSL and
  `RenderSceneDataRD::UBO`), filled in `update_ubo()`'s Environment branch (0 when off, with no Environment or in the
  unshaded debug mode: the UBO is memset). Layout and size unchanged: **no binding change, no new variants, interface
  and scan references unchanged** (smoke below).
- **Compatibility**: its own `micro_shadow()` in `scene.glsl`; the scene UBO grew by one vec4 (`micro_shadow_opacity` +
  3 pad) in both `SceneData` structs and `RasterizerSceneGLES3::SceneState::UBO`, set every frame (0 when off).
  Applied to the base pass's unshadowed directional lights and the additive pass's shadowed one.
- Not applied with per-vertex lighting (no per-pixel normal); documented in the class reference.
- Storage/server plumbing in its own `Micro shadows` blocks next to SSIL (not next to the glare/diffusion blocks, to stay
  clear of tasks 45/46): `environment_storage`, `renderer_scene_render`, `rendering_method`, `renderer_scene_cull`
  (PASS3), `rendering_server{.h,.cpp,_default.h}`, `scene/resources/environment`.
- Class reference: Environment (two members), RenderingServer (`environment_set_micro_shadows`), by hand; checked
  against `--doctool` output in a scratch dir (same signatures and defaults).
- **Forward+ matrix arm `micro_shadows`** (cat lighting, 97 -> 98): the probe sphere wears the bumpy normal map and a
  matching AO texture (`tex_ao`, new) in both frames; only the Environment toggle differs. minDelta 0.229 (measured
  0.687 / 3).

## Verification (native: xvfb, RTX 4080 SUPER, Vulkan / GL; tools in `tools/`)

**Against the HDRP formula** (`tools/micro_test.gd`, `tools/analyze.py`, results in `formula.txt`): orthographic view of
a quad, AO gradient 0..1 along x, normal map tilted -60..60 deg along y, Lambert, no specular, no ambient, linear
tonemapper, HDR viewport, 512x512. The factor on/off per pixel vs the formula evaluated in numpy from the 8-bit texture
values (the off image itself is checked to be 0.80 x N.L of the modeled normals, mean error 1e-4), light elevations 90,
60, 40, 25 and 10 deg, opacity 1 and 0.5, light shadow off and on, every renderer:

| renderer | mean abs error | p99 | max |
|---|---|---|---|
| Forward+ | 0.00004-0.00008 | 0.0005 | 0.0009 |
| Mobile | 0.00008-0.00019 | 0.0012 | 0.0020 |
| Compatibility | 0.00010-0.00026 | 0.0010 | 0.035 (a few pixels whose lit value is ~0: half-float sRGB quantization) |

Mean factor over lit pixels, all three renderers vs model: 0.4413 (90 deg), 0.4040 (60), 0.3869 (40), 0.3619 (25),
0.3291 (10), 0.6934 at opacity 0.5 (Mobile at 40 deg: 0.3857 / 0.6929 vs model 0.3858 / 0.6929). With a real cast
shadow (occluder box, shadow on, 60 deg: lit pixels 259072 -> ~234500) the factor still matches (Forward+ max error
0.0009, Mobile 0.0018, Compatibility mean 0.0001), which also exercises Compatibility's additive (shadowed light) path.

**Off is bit-identical** (`identical.txt`): base editor (7b978e6baa, built in a scratch worktree) vs work/49, three
renderers x {AO + normal map, shadowed + occluder, no AO map, flat normal}: micro off, micro on at opacity 0, and micro
on at opacity 1 on a material without an AO map: max diff 0, 0 differing values in all 27 comparisons.

**WebGPU vs native** (system Chrome headed with the four flags, adapter asserted nvidia/lovelace, not fallback,
maxSampledTexturesPerShaderStage 48 before each run by `tools/adapter_probe.mjs`; CI unset; template_release nothreads
from this tree):
- lighting arms + normal_map + micro_shadows, `--parity` unbaked: 16/16 PASS; micro_shadows delta 0.687, native diff
  off 0.003 on 0.003; every lighting arm within 0.004 of native (omni_shadow_cube on: 0.015).
- `--parity --baked` (micro_shadows, normal_map, directional_light, directional_shadow, light_specular): 5/5 PASS,
  micro_shadows native diff 0.003 / 0.003.
- full matrix `--parity` unbaked: **98/98 PASS** (micro_shadows delta 0.687, native diff
  off 0.016 on 0.014 in the full run; worst arm fsr2 0.142, then sdfgi 0.027, all within tolerance).

**Cost**: a dot product, a multiply-add, a clamp and a mix per pixel per directional light, behind a uniform branch;
no pass, texture or variant. GPU time measured on the 2560x1440 test quad (`tools/cost.sh`) was noise-dominated
(0.2-2 ms swings between runs of the same configuration: GPU clock states, other GPU users), so no number is claimed;
nothing above that noise was seen. Not measured on the web or on the phone.

**Builds**: editor with `dev_mode=yes` (werror) OK; web template_release nothreads OK (`[WGSL Precompile] Results:
292 compiled, 0 glsl failures, 0 conversion failures`). `pre-commit run --files` on every changed file OK.

**Smoke**:
```
[WGSL Precompile] Results: 292 compiled, 0 glsl failures, 0 conversion failures
identical interface: 292/292, mismatched: 0, missing in candidate: 0, not in reference: 0
against the stored reference: 584/584 identical, 0 differ, 0 missing, 0 not in reference
validation: 292 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261028-122731-1043800)
```

All browser runs used the required launch (run_forward_plus.mjs's own and validate_wgsl.mjs's own: system Chrome,
headed, the four flags, swiftshader default removed).

## What the lead must run

- After merging: smoke (no reference regeneration from this branch); `./export.sh`, `BAKE=1 ./export.sh`, the matrix
  with `--parity` and `--parity --baked`. CLAUDE.md / README matrix counts: +1 arm (`micro_shadows`).
- Formula check, optional: put `tools/micro_test.gd` in a folder `proj/` (with a `project.godot`, MSAA off) next to
  `tools/render.sh` and `tools/analyze.py`, then `tools/formula.sh <editor> <outdir>`.

## Risks

- `scene_data_inc.glsl` / `render_scene_data_rd.h`: the spare `pad` word is now used; another branch wanting it would
  conflict (none of 45-48/50 should touch the scene UBO, per the survey).
- The Compatibility scene UBO grew by 16 bytes (far below its limit).
- A shadow-to-opacity material turns micro shadows into opacity, as it does any shadow (in the wiki text).
- Without an AO map nothing changes, by design (as in HDRP); normal-map-only self-shadowing is not what this does.
