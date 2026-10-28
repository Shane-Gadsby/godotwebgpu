# Task 45: Environment color grading (one baked 3D LUT in the color correction slot)

Branch `work/45` on webgpu-4.8 at f3ed4b3852. Worker-45, 2026-10-28.

## What changed

- **Environment, new group "Color Grading"** (`grading_*`, 28 properties, all off/neutral by default): color filter,
  hue shift, vibrance; lift/gamma/gain; shadows/midtones/highlights + 4 range values; channel mixer (3 x Vector3);
  split toning (2 colors + balance); curves as `Curve` resources (master, R, G, B, hue-vs-hue, hue-vs-sat, sat-vs-sat,
  lum-vs-sat; the scene samples each 256 times over its domain, a curve without points is off). RS:
  `environment_set_color_grading()`, `_shadows_midtones_highlights()`, `_channel_mixer()`, `_split_toning()`,
  `_curves()`. Class reference for all of it (Environment, RenderingServer; doctool run, unrelated CameraAttributes.xml
  reorder reverted).
- **Model** (`servers/rendering/storage/color_grading.{h,cpp}`, `RendererColorGrading`): display-referred, on the
  tonemapped sRGB-encoded image after brightness/contrast/saturation and before the user's
  `adjustment_color_correction` LUT. URP LutBuilderLdr order: filter, mixer, SMH in linear light (Rec. 709 luma,
  URP's smoothstep ranges); then on encoded values LGG (`gain*(x+lift*(1-x))`, `^(1/gamma)`), split toning (URP's
  soft light in gamma space), hue shift + hue-vs-hue, hue/sat/lum-vs-sat (URP curves, neutral 0.5), vibrance
  (CeeJay.dk's Vibrance.fx formula, clamped at full desaturation for vibrance < -0.5), YRGB curves. Sources in the
  file's header comment. Baked into a 33^3 RGBA16F table of **lattice points** on the worker threads (one blue slice
  per task) only when a setter bumps `color_grading_version`.
- **Forward+, Mobile, web**: `RendererRD::ColorGrading` (effects/color_grading.*), created on first use. Per
  environment: the CPU bake, and up to two finished LUTs (sRGB-out, and linear-out for HDR 2D/HDR output viewports),
  made by a small compute shader (`shaders/effects/color_grading.glsl`, variants user_lut_3d / user_lut_1d) that
  composes grade -> user LUT exactly as the tonemap would sample it (same sampler, sRGB view for linear output), only
  when the bake or the user LUT changes. It runs from `render_scene()` before any render pass (Mobile's subpass tonemap
  cannot dispatch); the tonemap path only looks it up. The finished LUT goes in the existing color correction slot:
  **no new binding, tonemap interfaces unchanged**. The tonemap samples it at lattice points through Forward+ push
  constant flag bit 24 (`FLAG_COLOR_CORRECTION_LATTICE`; `CAMERA_LUT_PARAMS` now masks bits 8-23) and Mobile's former
  `pad[0]` (`color_correction_lattice`); user LUTs keep their texel-center mapping. Grading with adjustments off turns
  BCS on with neutral values. The user LUT applies only with adjustments enabled, as before.
- **Compatibility**: `USE_COLOR_GRADING` post specialization, the grading LUT at texunit 5 (GL RGBA16F 3D texture,
  uploaded on version change), applied before the user LUT (two lookups, no compose needed); grading forces the post
  pass.
- `wgsl_precompile.py`: the two color_grading variants (294 modules). Interface + scan references regenerated: **only
  the two new modules** added (the scan file's large diff is fact-table renumbering).

## Verification (RTX 4080 SUPER, Vulkan, xvfb; 1080x1080; tools in `tools/`, all numbers in `numbers.txt`)

Chart: 729 unshaded patches at the LUT lattice (levels i/8) plus 512 off-lattice ((i+0.5)/8), linear tonemapper, so
each patch reaches the grading as a known sRGB value. `tools/grade_ref.py` is an independent numpy (float64) model
written from the documented math; each render is compared with (a) the reference baked into the same 33^3 half LUT
and sampled the same way, (b) the exact analytic grade. 25 cases: every control alone, all together, all + 3D user
LUT, all + 1D user LUT, all + BCS + 3D LUT, user LUT alone, off, neutral.

| | vs LUT model, max (/255) | vs analytic, max |
|---|---|---|
| Forward+ (every case, both charts) | 1.00 (curve_master 0) | 0.65 per control; 1.40 all+3D LUT; 2.44 all+BCS+LUT |
| Mobile | 1-2 (3 on one off-lattice case; its ungraded floor is 2.06) | <= 2.6 |
| Compatibility (on the post pass's actual input, see below) | 1-2 | <= 7.0 (mixer, darks) |
| Web (WebGPU Forward+) | **pixel-identical to native Forward+ in all 31 cases** (max diff 0) | as Forward+ |

- **Default off is bit-identical**: base editor (c5cca53fd = code of f3ed4b3852) vs work/45, off chart, off-lattice
  chart, user 3D LUT alone, user 1D LUT alone x Forward+/Mobile/Compatibility: max diff 0, 0 pixels, all 12.
- Final code (parallel bake, vibrance clamp) re-rendered: identical bytes to the earlier renders for off, neutral, all,
  all_user3d, vibrance_min on all three renderers.
- **Bake time** (verbose, every control on, hue shift animated, 120 frames, Forward+): 1.51 ms median parallel
  (5.65 ms first); 4.60 ms single-threaded before parallelizing (what a threads=no web export pays per change).
- Web: export from the dev_mode editor with the work/45 template_debug nothreads (unbaked), system Chrome headed with
  the required flags; adapter asserted nvidia/lovelace, not fallback, maxSampledTexturesPerShaderStage 48 (twice).
- **Compatibility note** (not this task's bug, identical in the base editor): this chart's readback in Compatibility
  differs from Forward+'s before any grading (sRGB 0.5 reads back as 54/255, off and with the post pass). So the
  Compatibility check takes the "neutral" render (grading on, identity) as each patch's real post-pass input and
  checks the grade of it: 1-2 levels.
- Builds: editor with `dev_mode=yes` (werror) OK (final at 86c066558b + docs), web template_debug nothreads OK
  (294 precompiled, 0 failures). `pre-commit run --files` on every changed file OK.
- By-eye frames: `grid_forward_plus.jpg` (off, teal-orange, bleach, warm), `teal_orange_renderers.jpg` (Forward+,
  Mobile, Compatibility; Compatibility's ungraded scene is already brighter, the grade matches).

**Smoke** (final tree):
```
== build wgsl_convert_cli
== dump + convert every engine variant
[WGSL Precompile] Results: 294 compiled, 0 glsl failures, 0 conversion failures
== interface: stored reference vs this tree
identical interface: 294/294, mismatched: 0, missing in candidate: 0, not in reference: 0
== WGSL text-scan facts: stored reference vs this tree
against the stored reference: 588/588 identical, 0 differ, 0 missing, 0 not in reference
== Chrome validation
adapter (chrome): {"vendor":"nvidia","architecture":"lovelace","fallback":false,"features":["shader-f16","subgroups","clip-distances","dual-source-blending"]}
/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w45/smoke/smoke-20261028-142105-1667353/wgsl: 294 files, 0 invalid
validation: 294 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261028-142105-1667353)
```

## What the lead must still run

- Smoke after the merge, and the regenerated references again after merging next to tasks 46-50 (they may add
  modules too).
- Forward+ matrix `--parity`: no arm uses grading; a `color_grading` arm (all controls + a user LUT) would be a cheap
  addition.

## Risks

- Forward+ push-constant flags bit 24 and Mobile's `pad[0]` are now used; tasks 46-50 touching the tonemap push
  constant must not take the same ones (merge conflict is in the same lines, so it will show).
- `CAMERA_LUT_PARAMS` now masks bits 8-23 (needed since bit 24 can be set).
- Grading clamps to 0-1 like the user LUT: HDR output is clipped while grading is on (documented).
- A user LUT whose texture is updated in place (same RD texture) is not recomposed until a grading value changes
  (the cache is keyed by the RD texture RID). Replacing the texture resource works.
