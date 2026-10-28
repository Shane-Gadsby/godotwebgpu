# Task 57: local exposure (Unreal's Bilateral method): SUMMARY

Branch `work/57` on `6028459941` (webgpu-4.8). Worker-57, 2026-10-28.

## What changed

**CameraAttributes** (both kinds, base class), group **Local Exposure**, all `local_exposure_*`:
`enabled` (off), `highlight_contrast` (0.6), `shadow_contrast` (0.8), `detail_strength` (1.0), `middle_gray_bias`
(EV, 0), `blurred_luminance_blend` (0.6), `blurred_luminance_kernel_size` (% of the frame width, 50). Spelled "gray"
(Godot's API spelling; codespell rewrites "gray" in docs, so names and docs would otherwise disagree; Unreal says
"Middle Gray Bias"). RS `camera_attributes_set_local_exposure(rid, enable, highlight, shadow, detail,
middle_gray_bias)` and `camera_attributes_set_local_exposure_blurred_luminance(rid, blend, kernel_size)`. Class
reference for all of it, purely additive (`--doctool` also re-sorts the existing Panini members; that hunk left out).

**Model** (block "Local exposure" in `servers/rendering/storage/camera_attributes_storage.cpp`, sources cited): in
log2 luminance L (Rec. 709 Y of the exposed color, relative to 0.18), the base B is a bilateral grid (Chen, Paris and
Durand 2007) of 16 square cells down the frame x as many across as the aspect needs x 32 range bins of 1 EV (±16 EV),
blurred with a binomial [1 4 6 4 1]/16 per axis (one-cell Gaussian, Paris and Durand 2006), sliced trilinearly, mixed
with a plain Gaussian blur of L (±3σ = kernel % of the frame width) by the blend; the pixel is multiplied by
2^(M + (B − M)·c + (L − B)·d − L), c the highlight contrast above M, the shadow contrast below, d the detail strength,
M = middle gray + bias (Unreal's form). Contrasts and detail at 1 leave the frame unchanged. Sizes relative to the frame.

**Renderer:** `CameraLocalExposure` (`effects/camera_local_exposure.{h,cpp}`, `shaders/effects/camera_local_exposure.glsl`,
2 compute variants) builds the grid right before the tonemap pass from the texture it reads, with the tonemapper's
exposure (auto exposure included): MODE_BUILD, one 16x16 workgroup per cell column, sums in fixed point (1/4096 bin)
through workgroup atomics (WGSL has no float atomics) into an RGBA32F 3D texture; MODE_BLUR into an RGBA16F 3D texture
of 33 slices, the last the blurred luminance. The Forward+ tonemapper samples it at **set 1 binding 3**
(`camera_local_exposure_inc.glsl`) right after exposure and FXAA, before glow, glare, vignetting, white balance, grain.
Not applied in Mobile and Compatibility (`[b]Note:[/b]` in the docs, one-time editor warning, as auto white balance
does), nor for multiview.

**CameraEffects components taken (exactly):** `header.w` bit 0 (`LOCAL_EXPOSURE_BILATERAL`; bits 1-31 free for a
later Fusion mode), and `local_exposure[2]` = the former `reserved[0]` and `reserved[1]`; `reserved` is now
`reserved[13]`, layout and size (512 B) unchanged, in all three copies (RD include, Compatibility include, C++ struct).
`local_exposure[0]` = (highlight contrast, shadow contrast, detail strength, blend); `local_exposure[1]` = (middle gray
bias in log2, grid UV scale x, grid UV scale y, unused). `glare[6]`, `exposure[2]`, `header.x/y/z` untouched; task 56's
set 3 binding 2 not used.

Other files: `wgsl_precompile.py` (the 2 compute variants), Forward+ matrix arm `local_exposure` (`fp_scene.gd`,
`features.mjs`, minDelta 3.8), interface references regenerated.

## Verification (numbers.txt; tools/ has the scene, runners and the numpy reference)

Scene `tools/proj/le_scene.gd`: a closed checkered room with an emissive window 8.6 EV over middle gray behind
mullions and an omni light beside it, plus a 13-patch chart (0.18·2^k, k = −6..6, each with a +1 EV inner square).
HDR captures (HDR 2D viewport, Linear tonemapper, RGBH readback) against `tools/le_ref.py`, an independent float64
numpy implementation of the model (np.add.at grid, exact trilinear weights, no fixed point, no half floats).

1. **Against the model per resolution** (defaults): room and chart at 720p, 1080p, 1440p: |measured − model| median
   0.0005, p99 ≤ 0.0015, max ≤ 0.0027 EV; color kept to 0.002. Factor ranges −1.52..+0.92 EV (room), −0.18..+1.00 EV
   (chart). Measured factor maps at 720p and 1440p (2x2 means) agree to p99 0.004 EV.
2. **Each control** at 1080p, against the model with that setting: identity (1/1/1) changes nothing (max relative
   9.6e-4, one half-float ulp); hc 0.3, sc 0.4, detail 0.5 and 2, blend 0 and 1, kernel 10 and 100, bias ±2, auto
   exposure on, physical units + auto exposure: all p99 ≤ 0.0032, max ≤ 0.0064 EV. Median change of window / darkest
   tenth (EV): defaults −1.41/+0.86; hc 0.3 −2.47/+0.86; sc 0.4 −1.41/+2.59; detail 0.5 −3.82/+1.05; detail 2
   +3.39/+0.50; blend 0 −3.34/+0.89; blend 1 −0.13/+0.84; kernel 10 −2.45/+0.89; kernel 100 −0.93/+0.82; bias +2
   −0.61/+1.26; bias −2 −2.21/+0.46.
   **Halos:** down a column through the window, the wall just under it comes out 0.27 EV darker than 150-200 px lower
   at the default blend (0.17 at blend 0), exactly the model's (+0.012/+0.286 measured vs +0.013/+0.286; column max
   error 0.0024 EV): no halo beyond the model (`halo_profile.png`). Look: `look_montage.png`.
3. **Defaults bit-identical** against the pre-change editor (this worktree's first build, copied aside): 12/12 renders
   max diff 0 (Forward+, Mobile, Compatibility × filmic+glow, filmic+auto exposure, linear, physical+auto exposure);
   with local exposure turned on, Mobile and Compatibility render exactly as with it off.
4. **WebGPU** (system Chrome headed, Vulkan, adapter asserted nvidia/lovelace, 48 textures; export baked under xvfb
   with Vulkan): against native Forward+ at 1280x720 in 8 cases (defaults, strong, detail 2, blend 0, kernel 10, auto
   exposure, linear, chart), on pixels where the off frames already agree (80%; the backends rasterize some edges
   differently, max 219 with local exposure off): mean 0.03-0.06/255, max 3-5 (18 and 11 at isolated pixels for blend
   0 and auto exposure). HDR could not be read back on the web (`get_image()` of an HDR 2D viewport returns null
   there; for-tasks.md), so the web model check is transitive: web = native in 8 bits, native = model in HDR.
   **GPU cost at 1080p on WebGPU** (`perf_guard/gpu_time.mjs --passes`, RTX 4080 SUPER at a light scene's clocks):
   build 0.039 ms + blur 0.115 ms, tonemap 0.082 → 0.095 ms: **~0.17 ms/frame**.
5. **smoke.sh: SMOKE OK** with the references regenerated on this branch:
   ```
   [WGSL Precompile] Results: 311 compiled, 0 glsl failures, 0 conversion failures
   identical interface: 311/311, mismatched: 0, missing in candidate: 0, not in reference: 0
   against the stored reference: 622/622 identical, 0 differ, 0 missing, 0 not in reference
   validation: 311 modules, 0 invalid, 0 of them expected
   SMOKE OK (smoke-20261028-153109-2042483)
   ```
   Reference changes, exactly: the grid's sampler + 3D texture (set 1 binding 3 → 1/6 sampler, 1/7 texture_3d in WGSL)
   in the four tonemap fragment variants (`_frag`, `_bicubic_frag`, `_1d_lut_frag`, `_bicubic_1d_lut_frag`);
   `camera_local_exposure.glsl_build_comp` and `_blur_comp` added.
   **Forward+ matrix arm `local_exposure`, `--parity --baked`: PASS**, delta 11.392, native diff 0.002 off / 0.004 on
   (white_balance beside it as a control: PASS).

Builds: editor with `dev_mode=yes` (werror, no warnings), web template_debug; `pre-commit` clean.

## For the lead to run

- smoke.sh on the merged tree (task 56 also changes the tonemap fragment interface: regenerate the references).
- The identity check on the merged tree (3 renderers) and the matrix arm with `--parity`.
- Merge notes: tonemap.glsl hunks are one include block after `camera_exposure_inc.glsl` and one `if` after the FXAA
  block; tone_mapper.cpp adds `u_local_exposure` to the set 1 cache call; CameraEffectsUBO's `reserved[15]` became
  `local_exposure[2]` + `reserved[13]` (a task taking more of `reserved` takes from `[13]`).

## Risks and limits

- Default blend 0.6 (Unreal's) limits how far a very bright window comes down (−1.4 EV at defaults, −3.3 at blend 0),
  so in the test scene the window stays clipped under Filmic: defaults question in for-tasks.md.
- Pixels beyond ±16 EV of middle gray are clamped into the end bins; their base is underestimated.
- Cost could drop: the blur is one 125-tap pass; the build serializes on atomics in flat cells (for-tasks.md §2).
- Not tested: XR (not applied), MSAA, FSR2/upscaled source (built from the texture the tonemapper reads), Firefox and
  phones (nothing browser-specific: compute and 3D storage textures, as color grading uses).
- Exposure Fusion fits later in the same binding (its exposure map in the grid's last slice, a `header.w` mode bit),
  ~2-3x the cost; not built.
