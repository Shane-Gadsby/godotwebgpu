# Task 55: text for TASKS.md

## §4 entry

### 4.xx Glare blur sizes calibrated per lod; explicit mip blending; Mobile glare registration (October 2026) *(work task 55; user answer `glare-lod-calibration`: Recalibrate)*

Halation, the diffusion filter and the lens flare ghosts now have their modeled size at every resolution, in both
Forward+ chains, Mobile and the web (they were right at 720p only; ~10% small at 1080p and up, task 40).
- The chain's blur per lod is measured (HDR captures, 9 points, 720-2160 lines): sigma^2 = A + B 4^L lod-0 texels^2,
  Forward+ half (-1.233, 5.181), full (-1.556, 5.182), Mobile (-0.157, 0.440); within 1% for Forward+
  (`GLARE_CHAIN_BLUR_*` in `renderer_scene_render_rd.cpp`, `CameraEffectsContext::glare_chain_blur`). The old model
  (2.68 x 2^L) was wrong per lod and only matched at 720p by accident.
- Sizes between two lods are a blend whose least-squares Gaussian has the asked sigma: (1 - w)/(a^2 + s^2)^2 +
  w/(b^2 + s^2)^2 = 1/(4 s^4), linear in w (`_glare_lod_for_sigma()` and `camera_glare_lod()`, kept in step).
- The shader blends the two integer lods itself (`camera_glare_fetch()`): NVIDIA's own blend on the anisotropic glare
  sampler weighs the upper mip by min(2t, 1) (trap below).
- Mobile reads each raster level where its downsample put it (`camera_glare_fetch_level()`, `glare[5].w`): halos at
  coarse lods sat up to 61 px off their light at 2160p.
- UBO glare block: `glare[5].z` = A/B, `glare[5].w` = raster flag (the block is now full); `glare[2].w` = diffusion's
  first octave sigma (frame heights). Diffusion's octaves are each their own lod (1/2/4/8 x base).
- Verified: halation within 3.2% (Forward+ half resolution, all heights and radii), Forward+ full resolution and
  Mobile within 3.4% from radius 1 up except halos between lod 0 and 1 (+5 to +13%, source-size limited; point-source
  simulation within 0.4%); diffusion Forward+ within 3.9%, Mobile +3 to +12% (single source); flare -9 to +8%; before
  -32% to +28%. Defaults bit-identical in 3 renderers (27 captures, including starburst-only and veiling-only).
  WebGPU vs native max diff 4 in the halo. smoke OK, no interface change. Numbers: `results/55-glare-lod-calibration/`.

## §3 trap candidates

- **`textureLod` at a fractional lod is not a linear blend of two mips on every GPU.** On the RTX 4080 SUPER (Vulkan)
  with an anisotropic sampler, lod L + t weighs mip L+1 by exactly min(2t, 1) (task 55: a 32-step sweep, residual
  < 6e-5). A blur that picks its size with a fractional lod is then wrong between lods, by up to 2x, and differently
  per GPU/browser. Read the two integer lods and `mix()` them yourself when the blend weight matters.
- **Glow's raster downsample (`BloomDownKernel4`) does not keep levels at their UVs on odd sizes.** Texel j reads
  coordinate 2j+1 of the level above, so level L's texel coordinate x holds lod 0's 2^L x. Glow's own upsample undoes
  it, but anything sampling the raster mip chain by UV (the glare chain) must read level L at uv * size(0) / (2^L
  size(L)), or coarse levels drift up to a few percent of the frame (task 55).
- **The fitted size of a blend of two blur levels depends on the source's size.** A blend of two Gaussians is not a
  Gaussian, so a least-squares Gaussian fit against a source as wide as the blur reads several percent wide (task 55:
  +9-11% for 3 px halos from a 2 px sphere, where point-source kernels give 0.4%). Test PSF-sized effects with
  sources well below the blur, or on simulated kernels.

## Questions for the user

None needed. (Default kept, for information: the flare ghosts' blur is within 9% of its model, not 3%; the ghost
blurs are look choices in the code, so this was not chased further.)
