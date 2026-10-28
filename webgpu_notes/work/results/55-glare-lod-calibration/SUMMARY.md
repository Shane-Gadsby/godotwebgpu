# Task 55: glare blur sizes calibrated per lod

Branch `work/55` on webgpu-4.8 at 5625009532. Worker-55, 2026-10-28. User answer `glare-lod-calibration`: Recalibrate.

## What was wrong (measured)

Three separate faults, all measured from HDR captures (RGBA16F, Linear tonemapper; §3 trap 80) and from an
independent model of the passes' sampling arithmetic (`tools/sim.py`):

1. **The per-lod model.** `lod = log2(sigma * chain_h / 2.68)` assumed lod L blurs 2.68 x 2^L lod-0 texels. Measured
   per lod (9 points at different sub-texel positions, 5 heights, each lod sampled on its own): Forward+ is
   2.0, 4.4, 9.0, 18.2, 36.4, 73 texels; that is sigma^2 = A + B 4^L with A = -1.233, B = 5.181 (half resolution) and
   A = -1.556, B = 5.182 (full resolution), within 1% at every lod and height. The asymptote is 2.28 x 2^L, not 2.68:
   2.68 only looked right at 720p because of fault 2.
2. **The hardware's blend between mips is not linear.** `textureLod` at a fractional lod on the glare chain's
   (anisotropic) sampler, NVIDIA RTX 4080 SUPER under Vulkan, weighs the upper mip by exactly min(2t, 1): lod 2.25 is
   half lod 3, lod 2.5 to 3.0 is all lod 3 (32-step sweep, residual < 6e-5). Other GPUs and browsers can differ;
   task 40's 10-15% web/native halo spread is consistent with it.
3. **Mobile's coarse levels were misplaced.** The raster downsample (`BloomDownKernel4`, shared with glow) maps level
   texel j to coordinate 2j+1 of the level above. On odd-sized levels (1080p: 270, 135, 67) that is not where the
   level's UV says, so the glare at lod L sat up to a few percent of the frame off its source: a halo 31 px below its
   light at lod 4 at 1080p, 61 px at lod 5 at 2160p.

## What changed

- `renderer_scene_render_rd.cpp`: `_render_camera_glare_chain()` reports the chain's measured blur
  (`GLARE_CHAIN_BLUR_HALF/FULL/RASTER`, (A, B)) instead of one sigma; the context says whether the chain is raster.
  Mobile: A = -0.157, B = 0.440 (measured after the registration fix).
- `camera_attributes_storage.{h,cpp}`: `CameraEffectsContext::glare_chain_blur` (Vector2) and `glare_chain_raster`
  replace `glare_chain_sigma`. `_glare_lod_for_sigma()` returns lower lod + blend weight. The weight makes the blend's
  best-fitting Gaussian as wide as asked: for (1 - w) G_a + w G_b the least-squares Gaussian's sigma s satisfies
  (1 - w)/(a^2 + s^2)^2 + w/(b^2 + s^2)^2 = 1/(4 s^4), which is linear in w (from the Gaussian product integrals;
  checked against measured blends: within 2% where the source is small next to the blur, and within 0.4% for a point
  source on the simulated kernels, every lod pair including the full-resolution chain's lod 0-1).
- `camera_glare_inc.glsl`: `camera_glare_lod()` is the same function; new `camera_glare_fetch()` reads the two integer
  lods and blends them itself (fault 2), through `camera_glare_fetch_level()`, which reads the raster chain's level L
  at uv * size(0) / (2^L size(L)) (fault 3). Halation, the diffusion filter's four octaves (now each its own lod at
  1/2/4/8 x the base sigma, instead of base lod + 1, 2, 3) and the lens flare ghosts and halo use it. Veiling glare
  (fixed lod) and starburst (`textureGrad`) are untouched.
- `tonemap.glsl`, `tonemap_mobile.glsl`: `CAMERA_GLARE_SIZE(m_lod)` (textureSize of the chain).
- **UBO** (glare block): `glare[5].x` is now chain_h / sqrt(B); **`glare[5].z` = A / B and `glare[5].w` = raster flag
  (the two components that were free; the glare block has none left)**; `glare[2].w` is now the diffusion's first
  octave sigma in frame heights instead of a lod.
- Class docs (`CameraAttributes.xml`): `halation_high_quality` gives the floors in pixels (about 4 standard, 1.9
  high quality, 2 on Mobile) instead of "about 0.5% of the frame height" (true at 720p only); `halation_radius` says
  a halo below the floor is drawn at the floor's size.
- Cost: halation, diffusion and flare take two texture reads where the blend weight is not 0 (flare: up to 48
  instead of 24 per pixel). Not timed.

## Verification (numbers.txt has every row)

1. **Halation size vs model**, 2D fit (task 40's metric), point-like source (a ~2 px sphere at every height):
   - Forward+ half resolution, centered and 9 points: **within 3.2% at every height and radius** (most within 2%);
     before -22% to +9% (1080p and 2160p ran about 10% small, as task 40 found).
   - Forward+ full resolution: within 2.7% at radius 2 and up everywhere, and from radius 1 at 900p and up.
     Exceptions, all halos between lod 0 (1.9 px) and lod 1 (4.4 px): radius 1 at 720p +9.0%, radius 0.5 at
     900/1080/1440p +5/+11/+9%. The simulated point-source kernels give the asked size within 0.4% there, so this is
     the 2 px test source being as wide as the blur (a blend of two lods is not a Gaussian, so its fitted size depends
     on the source's). Before: -24% to +9%.
   - Mobile, 9 points: within 3.4% from radius 1 up, except radius 1 at 720p (+7.9%, between lod 0 and 1 as above);
     radius 0.5 +5 to +13% (same). Before: -32% to +16%. A single centered point reads up to ~8% larger (the raster
     levels' shape, not their variance, moves with sub-texel position, simulated). The registration fix: radius 16
     halo center offsets 11/1/30/22/60 px (720-2160p) -> 0.2/0.6/0.2/0.2/0.7 px.
   - The marginal-fit metric reads blends a few percent wider (up to 9%), as a different norm on a non-Gaussian blend
     must; integer lods agree within 1-2% on both.
2. **Diffusion filter** (strength 1, normal and black, fitted size factor on the four octaves): Forward+ both chains
   -0.2 to +1.7% (normal), +1.3 to +3.9% (black); before -22% to +16%. Mobile +3 to +12% (single centered source, see
   above); before -10% to +23%. **Lens flare** ghost blur (ghosts 1 and 2, single source): Forward+ -9 to +1% (mean
   about -3%, not systematic, unexplained), Mobile -3 to +8%; before -26% to +28%.
3. **Defaults bit-identical** against the pre-change editor (`tools/ident.py`): 27 captures, 3 renderers, 1280x720:
   default scene with no attributes / physical / practical, an HDR float capture, a camera and Environment with glare
   values set but every glare effect disabled, halation_high_quality without halation, **and starburst-only and
   veiling-only scenes** (glare, unchanged by design): **all max diff 0**.
4. **WebGPU** (system Chrome headed with the required Vulkan flags; adapter nvidia/lovelace, not fallback,
   maxSampledTexturesPerShaderStage 48; engine log "WebGPU 1.0 - Forward+"), halation on a 0.5 card edge, 1280x720,
   8-bit, vs native Forward+: halation-off frame identical except the card's edge column (max 6, rasterization);
   with halation radius 4, 16 and HQ radius 1, max diff 4 in the halo, fitted halo sigma web/native 14.41/13.95,
   56.47/55.81 and 3.83/3.53 px (3%, 1%, 8%; the last a 10 px halo resolved in 1-level 8-bit steps). The export's
   template has the placeholder Mobile constant (-0.19, 0.44); Forward+ is unaffected.
5. **smoke.sh** (after the shader changes; only a C++ constant changed after it):
   ```
   [WGSL Precompile] Results: 304 compiled, 0 glsl failures, 0 conversion failures
   identical interface: 304/304, mismatched: 0, missing in candidate: 0, not in reference: 0
   against the stored reference: 608/608 identical, 0 differ, 0 missing, 0 not in reference
   validation: 304 modules, 0 invalid, 0 of them expected
   SMOKE OK (smoke-20261028-150348-1889766)
   ```
   No interface or text-scan reference change. **dev_mode=yes editor build passes** (werror). pre-commit on the
   changed files passes.

## For the lead

- Run smoke.sh after merging. The verification renders take ~15 min on the GPU (`tools/gpu_all.sh`,
  `tools/verify.py jobs|halation|diffusion|flare`, `tools/ident.py`, `tools/webcmp.py`; paths point at this worker's
  scratch directory).
- Risks: existing glare looks change (the user's decision): halos, diffusion and flare up to ~10% larger at 1080p and
  up; Mobile's large halos move onto their sources. The A/B constants were measured on one GPU (NVIDIA, Vulkan); the
  passes are deterministic arithmetic (the simulation reproduces them within 3%), so other GPUs should agree, but only
  NVIDIA was measured. The glare block of the CameraEffects UBO is now full.
- Not done: timing the extra reads; the flare residual (above); a pre-change web export (the web/native gap before
  this change is known only from task 40's 8-bit numbers).
