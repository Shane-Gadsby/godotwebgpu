# Task 44: longitudinal chromatic aberration; Forward+ matrix arms for the camera effects; Mobile physical DOF

Branch `work/44` on webgpu-4.8 at 46bd8e96b8. Worker-44, 2026-10-28.

## What changed

**Longitudinal chromatic aberration** (CameraAttributes, both kinds; group "Longitudinal Chromatic Aberration":
`longitudinal_chromatic_aberration_enabled` (false), `_strength` (1.0); RS
`camera_attributes_set_longitudinal_chromatic_aberration(rid, enable, strength)`):
- Model (comment above `camera_attributes_get_longitudinal_chromatic_aberration()` in `camera_attributes_storage.cpp`):
  an achromat brings red and blue to one focus, green focuses closest; secondary spectrum k = f/2200 (Smith, Modern
  Optical Engineering 4th ed. 5.3; Kingslake & Johnson 5.6). Focus set on green. Thin lens: red/blue signed CoC =
  green's minus A v k / f = k f u / (N (u - f)); in the DOF's units (its physical blur is the CoC at infinity
  f^2/(N(u-f)) times the depth term) the offset is `strength * k * u / f * blur_size`. Behind focus red/blue blur
  less (green fringe), in front more (magenta). Practical (no lens): red/blue blur scaled by 1 -/+ 0.027 x strength
  (the physical ratio for 50 mm at 3 m), so the in-focus band stays sharp.
- Renderer: the bokeh push constant's last free word (`pad`) is now `longitudinal_ca` (offset, scale as half floats),
  flag `BOKEH_LONGITUDINAL_CA` (bit 11 of `shape_flags`); `BokehDOF::_setup_longitudinal_ca()`. The gathers
  (circular, box/hexagon separable; compute and raster) and both composites compute a second weight for red/blue from
  `bokeh_blur_red_blue(alpha) = alpha - offset - scale |alpha|`; `bokeh_gather_reach()` widens the gather reach so a
  near red/blue blur is not capped at the green one. Every new line is behind the flag; off is the old code path.
  Mobile's composite blends with one alpha, so it takes the larger of the two amounts (documented).
- Push constant stays 128 bytes. The raster composite fragment now reads the push constant, so it gains the
  push-constant emulation binding on WebGPU: interface reference and scan reference regenerated, the only change is
  `(3, 120, '<storage,read>')` in `bokeh_dof_raster.glsl_composite_frag` (and its scan facts).
- Class reference (CameraAttributes, RenderingServer), inserted by hand: doctool also reorders the whole
  CameraAttributes.xml (the merged file is not in doctool's order), left out to avoid conflicts. Running doctool on
  the merged tree will reorder it.

**Mobile physical DOF** (hand-offs question `mobile-dof-physical`, default "Fix it", built): `bokeh_dof_raster()` now
sets `use_physical_far/near` and `blur_size_far/near` from a negative transition and caps the gather at 32 px when
both are physical, exactly as `bokeh_dof_compute()`. Before, a CameraAttributesPhysical camera had **no blur at all**
on Mobile (measured edge radius 0.00 px). Changes existing Mobile renders that use a Physical camera with DOF.

**Forward+ matrix: 10 new arms (87 -> 97)**, category `camera`: `lens_optics`, `bokeh_shape` (5 blades + cat's eye),
`longitudinal_ca` (physical camera), `tilt_shift` (physical camera), `starburst`, `halation`, `lens_flare`,
`veiling_glare_diffusion`, `camera_lut` (17^3 ImageTexture3D built in the fixture), `in_camera_processing` (noise
reduction + sharpening). Fixture: a small emissive `glare_sphere`, `cam_attrs_physical` (23.05 mm = the default 55 deg
view, f/1.4, focus 4 m), `_reset()` restores `cam.attributes` and hides the sphere; resources snapshotted as before.
Floors = native delta / 3. README (forward_plus and webgpu_tests) updated to 97.

## Verification (native: xvfb, RTX 4080 SUPER, Vulkan, 1280x720, linear tonemapper; tools in `tools/`, numbers in `numbers.txt`)

Edge test (`tools/lca_test.gd`, white|black plane, circle bokeh, medium quality); radius = 2 sigma of the per-channel
line spread (`tools/edge.py`). Physical 50 mm, focus 2 m, strength 10; model offset k u/f x blur_size:

| case | green r (model b) | red/blue r | model red/blue | fringe |
|---|---|---|---|---|
| f/1.4, plane 6 m (behind) | 7.89 (7.81) | 5.79 | 5.68 | green on the dark side (23,45,23) |
| f/1.4, plane 1.2 m (front) | 7.89 (7.81) | 9.99 | 9.94 | magenta (59,45,59) |
| f/2.8, 6 m | 4.03 (3.91) | 2.44 | 2.84 | |
| f/2.8, 1.2 m | 4.03 (3.91) | 5.10 | 4.97 | |

The fringe width (green - red/blue radius) is 2.10 / 2.10 px at f/1.4 vs 2.13 modeled and 1.59 / 1.07 at f/2.8 vs
1.07: it halves when stopping down two stops. The f/2.8 far case's 2.44 is the gather's own response at that radius:
a strength-0 lens set to the same blur (b = 2.84, f/3.85) measures 2.44 with the identical 10-90 width (3.35), i.e.
red/blue is drawn exactly as green would be at the shifted focus. Practical (amount 0.3, strength 10, scale 0.273):
far 13.98 vs 13.96 modeled, near 24.10 vs 24.4 (before widening the gather reach it was capped at 19.13).

**Off is bit-identical**: base editor (f77f823bf, the code base of 46bd8e96b8) vs work/44 on the edge scene:
Practical and Physical x circle/hexagon/box x Forward+/Mobile/Compatibility, plus Practical x 3 shapes x quality
very low/low/high x Forward+/Mobile: max diff 0, 0 pixels in all cases except Mobile + Physical (the intended fix).

**Mobile vs Forward+ CoC, physical camera** (50 mm, focus 2 m, LoCA off): f/1.4 Mobile 8.15 px, Forward+ 7.89, model
7.81; f/2.8 Mobile 4.16, Forward+ 4.03, model 3.91 (Mobile ~3% wider, and its edge center sits 1 px left: its
half-resolution composite, the same as with Practical blur). Base editor Mobile: 0.00 (no blur). Mobile LoCA: 8.15 ->
5.91 (behind), 10.01 (front); f/2.8 2.91 / 5.16; practical 14.09 / 24.12.

**Forward+ matrix** (system Chrome headed, Vulkan; adapter asserted nvidia/lovelace, not fallback,
maxSampledTexturesPerShaderStage 48 by `tools/adapter_probe.mjs` before each run; banner "WebGPU 1.0 - Forward+"; CI
unset): the 10 new arms `--parity` unbaked 10/10 PASS and `--parity --baked` 10/10 PASS (native diff 0.002-0.003 for
every frame); full matrix `--parity` unbaked **97/97 PASS**. Native deltas: lens_optics 18.28, bokeh_shape 2.47,
longitudinal_ca 0.41, tilt_shift 1.77, starburst 2.86, halation 0.94, lens_flare 1.12, veiling_glare_diffusion 6.18,
camera_lut 10.85, in_camera_processing 0.53; WebGPU frames within 0.0025 of native.

**Builds**: editor with `dev_mode=yes` (werror) OK; web template_release nothreads OK (292 precompiled, 0 failures).
`pre-commit run --files` on every changed file OK.

**Smoke** (final tree):
```
[WGSL Precompile] Results: 292 compiled, 0 glsl failures, 0 conversion failures
identical interface: 292/292, mismatched: 0, missing in candidate: 0, not in reference: 0
against the stored reference: 584/584 identical, 0 differ, 0 missing, 0 not in reference
validation: 292 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261028-115845-872978)
```

All browser runs used the required launch (run_forward_plus.mjs's own: system Chrome, headed, the four flags,
swiftshader default removed); none needs redoing.

Not verified: Mobile LoCA/physical DOF on WebGPU (the web runs Forward+), MSAA, XR, the full matrix baked (only the 10
new arms baked), GPU cost (two extra smoothsteps per DOF sample while on).

## What the lead must run

- After merging: regenerate the interface/scan references again if another branch touched them (this one adds only
  `(3,120)` to the raster composite), then smoke.
- The full matrix with the merged tree's templates (`./export.sh`, `BAKE=1 ./export.sh`, `--parity`, `--parity --baked`).
- Optionally the edge checks: copy `tools/lca_test.gd` into a project with `project.godot` setting
  `rendering/camera/depth_of_field/depth_of_field_bokeh_shape=2`; `tools/render.sh <editor> <method> <out.png> n=1.4
  focus=2 dist=6 lca=10` (edit its project path), then `tools/edge.py <pngs>`.

## Risks

- Mobile + Physical renders change (the fix): scenes that relied on Mobile ignoring physical DOF now blur.
- Mobile LoCA blends all three channels by the larger amount, so with a Physical camera the in-focus area is blended
  by the offset (0.2 px at strength 1) toward the half-resolution image. Forward+ blends per channel.
- At strength 1 the effect is subtle (2.7% of the far blur); the docs point to 3-10 for fast-lens fringing.
- With LoCA on, `bokeh_gather_reach()` widens the gather (and the separable filters' step) by up to the scale and
  offset, so green is sampled slightly more sparsely and the gather costs that much more.
