# For TASKS.md (task 58)

All three items close. Proposed archive entries (§4, after the last one), then the §2 lines to replace the open entries
with, §3 trap candidates and questions.

## §4 archive entries

### 4.xx `run_spec_ops.sh`'s invalid FConvert probe dropped (October 2026) *(was §2.44; work task 58)*

`spec_op_probes.py`'s `op_FConvert_f32` converted f32 to f32. The vendored SPIRV-Tools validator (the one
`run_pipeline()` runs) rejects that ("Expected component type of Value to be different from component type of Result
Type: SpecConstantOp ... FConvert"), so both paths refused the module. The system `spirv-val` (v2025.1) accepts it,
which is why the probes looked valid by hand: **check probes with the converter, not only with the distro's
`spirv-val`**. A valid FConvert needs f16 or f64; WGSL has no f64, and an f16 spec constant needs `shader-f16` and is
declared by no engine or Godot user shader, so the probe is gone with that reason in the generator (the freezer's
same-width FConvert case in `eval_spec_op()` is now unreachable from valid input; left alone). `spec_op_check.mjs` also
loads Playwright from the main checkout when run in a worktree and refuses to run on a fallback adapter.
`run_spec_ops.sh` (Chrome, 4080 SUPER, 48 modules): override path 42 pass, 6 expected failures; legacy path 47 pass, 1 expected (`op_SMod`); 0 unexpected.

### 4.xx Swizzled texture views audited; the driver warns when it drops one (October 2026) *(was §2.43; work task 58)*

Every non-identity swizzle that can reach `RenderingDeviceDriverWebGPU`, and what happens to it now:

| source | swizzle | on WebGPU |
|---|---|---|
| `TextureStorage::_validate_texture_format()` L8, LA8 | (R,R,R,1), (R,R,R,G) | expanded to RGBA8 in the data (already, §3 trap 17) |
| RGBA4444, RGB565 | R/B exchange on B4G4R4A4 / B5G6R5 | **were broken outright**: WebGPU has no packed 16-bit formats, so `RD::texture_create()` failed. Now converted to RGBA8 (verbose note, not the "not supported by hardware" warning) |
| ETC2_RA_AS_RG, DXT5_RA_AS_RG | G <- A on compressed data | always decompressed on the web; the decompressors write (R, A, 0, 1). Not reached in practice (Basis picks RGTC / ETC2_RG11 first; WebGPU's BC feature has BC5 whenever it has BC3) |
| R8/RG8/RF/RGF/RH/RGH, RGTC, EAC R11/RG11 and their fallbacks | 0 for missing G/B, 1 for missing A | WebGPU's own defaults; no-op |
| RGB8/RGBH/RGBF, DXT1, BPTC RGBF(U), ETC/ETC2 RGB8 and fallbacks | A = 1 | 3-channel formats read alpha 1; converted data has alpha 1 (decompressed DXT1 gets its alpha forced to 255: BC1 punch-through blocks decode to 0, which native's swizzle hides) |
| `RenderSceneBuffersRD::get_internal_texture_reactive()` (A,A,A,A) | | no caller left since task 17 (amd-fsr2 patch 0005 reads `.a`) |
| betsy (`image_compress_betsy.cpp`) L8/LA8/RGBA4444 source views | | editor only (`betsy_export_templates` off); the editor is Vulkan |
| `_texture_format_from_rd()`: a `Texture2DRD` over an R8 / RG8 RD texture | shown as L8 (R,R,R,1) / LA8 (R,R,R,G) | **still dropped** (user GPU data, nothing to convert); now warns |
| user `RenderingDevice.texture_create(_shared)` with an `RDTextureView` swizzle | any | **still dropped**; now warns |
| `render_target_update()`: an opaque viewport's texture (`swizzle_a = ONE`) | (R,G,B,1) | **not requested on the web any more**, or every web game warned at startup. The colour buffer's alpha is 1 wherever the engine writes it; a project that lowers it itself (clear colour alpha below 1, a canvas or 3D blend mode that subtracts or multiplies alpha) and samples the viewport texture with blending sees it on the web. The main viewport is unaffected (the swap-chain pass never writes alpha). See question 2 |

`_validate_texture_format()` resets the zero/one swizzles that only restate a channel the source image lacks (on the web
only), so the driver's check is left with real drops. `RenderingDeviceDriverWebGPU::_warn_dropped_swizzle()` runs in
`texture_create()`, `texture_create_shared()` and `texture_create_shared_from_slice()`: a swizzle is harmless when each
component is identity, or 0 for a green/blue the view format lacks, or 1 for a missing alpha; anything else is a
`WARN_PRINT` (console.warn on the web) once per (view format, swizzle):
`WebGPU: a R8_Unorm texture view asks for the component swizzle (RRR1), which WebGPU texture views cannot apply; the
shader reads the channels unswizzled. Convert the data or swizzle in the shader instead.` Severity: a warning, because
the texture really reads differently from every other driver. Test: Forward+ matrix arm `image_formats` (L8, LA8,
RGBA4444, RGB565, R8, RG8 images on a material against the same images expanded to RGBA8, parity with native): native diff off
0.002, on 0.003. Before the fix RGBA4444 and RGB565 image textures failed outright on the web (`Format
'B4G4R4A4_Unorm_Pack16' does not support usage as sampling texture`, checked with a pre-change template). The warning,
from a scratch project: an R8 view with (R,R,R,1) twice and an RGBA8 view with (A,A,A,A) give exactly two warnings, an
R8 view with (R,0,0,1) and nine Image formats (L8, LA8, RGBA4444, RGB565, R8, RG8, RGB8, RGBF, RH) give none, and the
startup is silent.

### 4.xx FSR2 accumulate and scene binding 24: the depth fallback is never read (October 2026) *(was §2.42 and §2.31 follow-up 3; work task 58)*

**FSR2 accumulate, set 1 binding 5.** `r_input_depth` is declared there because Godot defines
`FFX_FSR2_OPTION_GODOT_DERIVE_INVALID_MOTION_VECTORS`, but the accumulate pass reads motion vectors through
`LoadDilatedMotionVector()` (`FFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS`), and only `LoadInputMotionVector()` calls
`LoadInputDepth()`. So the pass never reads it: SPIRV-Cross drops the variable from the WGSL, and the blank fallback the
driver binds in its slot is never sampled. The passes that do read the depth (reconstruct-previous-depth, which derives
the sky's motion vectors, and depth clip) get `texture_depth_2d` from the reclassifier and the real depth. Checked with
the camera panning across the sky: new Forward+ arm `fsr2_camera_move` (parity only; FSR2 with the camera yawing 0.25° and
pitching 0.05° a frame for the whole 64-frame settle): native diff on 0.059 (off, FSR2 with a still camera, 0.142);
`fsr2` 0.142, `taa` 0.006, baked, parity. The whole matrix, baked with parity: **101/101**.

**Scene set 1 binding 24** (`depth_buffer`, Forward+; binding 9 on Mobile) is §2.31's "4x4 depth texture": it is
`DEFAULT_RD_TEXTURE_DEPTH`, a 4x4 D16 placeholder, bound when the frame has no depth copy (`RB_TEX_BACK_DEPTH`), i.e. when
no material reads `DEPTH_TEXTURE` (any that does makes the copy, an R32_SFLOAT colour texture, which is why the
reclassifier deliberately leaves `depth_buffer` a float texture in group 1). Nothing reads it then. On WebGPU it is now
`DEFAULT_RD_TEXTURE_BLACK` (2D array black for multiview), a float texture, so the driver no longer substitutes and logs
on every uniform set; the verbose log is quiet unless something real is replaced. (Natively the placeholder is
`make_half_float(1.0)` written into a D16_UNORM texture, i.e. 0.234, an upstream oddity nobody reads.) A `--verbose` run of
the `fsr2`, `fsr2_camera_move` and `depth_texture` arms now logs the fallback only for FSR2 accumulate's unread binding 5
(16 lines), none for the scene shaders. That remaining line could go too (the driver would have to check the binding
against the converted WGSL before logging); not done, it is verbose-only.

**Also fixed on the way:** `wgsl_precompile.py`'s FSR2 defines lacked `FFX_FSR2_OPTION_GODOT_REACTIVE_MASK_FROM_ALPHA`
(added to `fsr2.cpp` by task 17), so the precompiled depth-clip and TCR-autogen passes read the reactive mask's `.x`
and their SPIR-V never matched the runtime's: unbaked exports converted those two FSR2 passes at run time (correct, only
slower). They now read `.w` and come from the table. Interface and text-scan references unchanged (smoke 309/309,
618/618).

## §2 text (if the lead prefers to keep them in §2 until merged)

- §2.42 → DONE, see the §4 entry above.
- §2.43 → DONE; the remaining drop (a `Texture2DRD` over R8/RG8, and user RD views with a swizzle) warns, see the question.
- §2.44 → DONE.
- §2.31 follow-up 3 ("Name the 4x4 depth texture in a float slot") → done, see the §4 entry for §2.42.

## §3 trap candidates

- **The distro's `spirv-val` is not the validator `run_pipeline()` runs.** v2025.1 (2026-05 build) accepts a same-width
  `OpSpecConstantOp FConvert`; the vendored SPIRV-Tools rejects it. A hand-built probe that "passes spirv-val" can still
  be refused by the converter as invalid SPIR-V; check it with `wgsl_convert_cli <file.spv>`.
- **An unused binding still logs the depth fallback.** The driver builds bind-group layouts from the SPIR-V's
  declarations, so a binding SPIRV-Cross dropped from the WGSL (FSR2 accumulate's `r_input_depth`) still gets the blank
  fallback and its `print_verbose`. Before chasing one of those, grep the converted WGSL for the variable.
- **`wgsl_precompile.py`'s general defines are a copy of each effect's C++ defines, and a copy drifts silently.** FSR2's
  missed task 17's define for three days; nothing fails, the precompiled entries just stop matching and the runtime
  converts instead. When changing an effect's `general_defines`, change `wgsl_precompile.py` in the same commit.

## Questions for the user

1. **`Texture2DRD` over an R8 or RG8 texture shows red (or red/green) on the web, gray natively.** Godot treats an R8
   RD texture wrapped in a `Texture2DRD` as L8 (R,R,R,1) and RG8 as LA8 through a view swizzle WebGPU does not have; the
   data is the user's GPU texture, so it cannot be converted. Options: (a) leave it, with the new one-time warning
   naming the format and swizzle **(default)**; (b) on the web, describe such textures as R8 / RG8 (what they then
   really show), so `get_image()` and the picture agree; (c) a copy into an RGBA8 texture on each update (costly; only
   worth it if a project needs it). Seen in no project so far.
2. **An opaque SubViewport's texture keeps its colour buffer's alpha on the web** (native forces 1 through a swizzle).
   Only visible where a project lowers that alpha itself and draws the viewport texture with blending. Options: (a)
   leave it, documented **(default)**; (b) have the last pass that writes an opaque render target (tonemap, or the 2D
   canvas's final pass) write alpha 1 on the web: a shader change in the tonemap pass, which tasks 55-57 own today.
