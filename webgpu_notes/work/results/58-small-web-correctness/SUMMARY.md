# Task 58: small web correctness items (TASKS §2.42, §2.43, §2.44)

Branch `work/58`, three commits, one per item, on `01eb7d3753`. All three close; `for-tasks.md` has the §4 entries,
§3 trap candidates and two questions for the user (both with a "leave it" default).

## §2.44 `run_spec_ops.sh`'s invalid FConvert probe (`9035ac6935`)

- Confirmed: `wgsl_convert_cli` refuses `op_FConvert_f32` ("Expected component type of Value to be different from
  component type of Result Type: SpecConstantOp ... FConvert"). The distro `spirv-val` (v2025.1) accepts it, which is
  why it looked valid; the vendored SPIRV-Tools is stricter.
- Dropped the probe with the reason in `spec_op_probes.py` (a valid FConvert needs f16 or f64; WGSL has no f64, and no
  engine or Godot user shader declares an f16 spec constant).
- `spec_op_check.mjs`: loads Playwright from the main checkout in a worktree (as `validate_wgsl.mjs` does) and refuses a
  fallback adapter.
- `run_spec_ops.sh` (Chrome, nvidia/lovelace, 48 sampled textures): override path 42 pass / 6 expected failures,
  legacy path 47 pass / 1 expected (`op_SMod`), 0 unexpected.

## §2.43 swizzled texture views (`ae3e4a29e9`)

The full audit is in `for-tasks.md`'s table. What changed:
- `TextureStorage::_validate_texture_format()` (WebGPU only): **RGBA4444 and RGB565 were broken outright** on the web
  (packed 16-bit formats WebGPU lacks; `texture_create()` failed, "Format 'B4G4R4A4_Unorm_Pack16' does not support
  usage as sampling texture", checked against a pre-change template); now converted to RGBA8. The RA-as-RG normal map
  formats always take the decompress path (unreached in practice). Zero/one swizzles that only restate a channel the
  source image lacks are reset to identity (BC1 punch-through alpha forced to 1 after decompression, as the swizzle
  did). The conversions log as the verbose "Expanded ..." note, not "not supported by hardware" (trap 17).
- `render_target_update()`: an opaque viewport's texture no longer asks for alpha ONE on the web (it would have warned
  in every web game at startup). Residual difference documented, question 2.
- `RenderingDeviceDriverWebGPU::_warn_dropped_swizzle()` in `texture_create()`, `texture_create_shared()` and
  `texture_create_shared_from_slice()`: `WARN_PRINT` once per (view format, swizzle) for any swizzle WebGPU's defaults
  do not already satisfy. What still reaches it: a `Texture2DRD` over R8/RG8 (shown as L8/LA8 natively; question 1) and
  user RD views.
- Tests: new Forward+ arm `image_formats` (L8, LA8, RGBA4444, RGB565, R8, RG8 against the same images expanded to RGBA8)
  native diff off 0.002 / on 0.003. Scratch project (`run_swz.mjs`, not committed): two RD views with (R,R,R,1) on R8 and
  one (A,A,A,A) on RGBA8 give exactly 2 warnings; (R,0,0,1) on R8 and nine Image formats give none; startup silent.

## §2.42 FSR2 accumulate and scene binding 24 (`954462ac34`)

- FSR2 accumulate's `r_input_depth` (set 1 binding 5) is declared but never read: the pass uses
  `LoadDilatedMotionVector()` (low-resolution motion vectors), and only `LoadInputMotionVector()` reads depth. SPIRV-Cross
  drops it from the WGSL; the fallback is never sampled. The passes that read depth (reconstruct-previous-depth, depth
  clip) are reclassified to `texture_depth_2d` (text-scan facts `DEPTH_RECLASSIFIED 1/2` and `1/18`). Left as is.
- New Forward+ arm `fsr2_camera_move`: FSR2 while the camera pans across the sky (0.25° yaw, 0.05° pitch a frame,
  64-frame settle; driven by `get_frames_drawn()`, because the native sweep applies an arm before `_process()` and the
  web from inside it, which first put the native camera one step ahead: 0.591, all at edges). Native diff on **0.059**,
  off 0.142. `fsr2` 0.142, `taa` 0.006.
- Scene binding 24 (Mobile 9) is §2.31's "4x4 depth texture": the D16 `DEFAULT_RD_TEXTURE_DEPTH` placeholder, bound
  when no material reads `DEPTH_TEXTURE` (so no R32F depth copy exists). On WebGPU it is now the black float default:
  a `--verbose` run logs no fallback for the scene shaders (before: a few per frame); FSR2 accumulate's unread binding 5
  still logs (verbose only, 16 lines over three arms).
- Found on the way: `wgsl_precompile.py` lacked task 17's `FFX_FSR2_OPTION_GODOT_REACTIVE_MASK_FROM_ALPHA`, so the
  precompiled FSR2 depth-clip and TCR passes read `.x` and never matched the runtime SPIR-V (runtime-converted; correct
  but slower). Added; those two WGSL files now read `.w`.

## Verification (this branch, 4080 SUPER, system Chrome with the Vulkan flags, adapter asserted every run)

- Builds: editor `dev_mode=yes` (werror) and web `template_debug` nothreads, both clean.
- Forward+ matrix, `BAKE=1` export under xvfb with Vulkan, `--baked --parity`: **101/101 pass** (99 + the two new arms);
  translated 157 at boot (one more GDScript material: ceiling raised 150 to 170). An earlier subset run also re-checked
  `depth_texture` 0.002, `proximity_fade` 0.003, `refraction` 0.003, `scaling_3d` 0.002 (binding 24's readers).
- `run_forward_plus.mjs` now prints and asserts the adapter (not fallback, 48 sampled textures) before anything else.
- smoke.sh:

```
identical interface: 309/309, mismatched: 0, missing in candidate: 0, not in reference: 0
against the stored reference: 618/618 identical, 0 differ, 0 missing, 0 not in reference
adapter (chrome): {"vendor":"nvidia","architecture":"lovelace","fallback":false,"features":["shader-f16","subgroups","clip-distances","dual-source-blending"]}
validation: 309 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261028-163155-2238525)
```

Interface and text-scan references unchanged, so not regenerated.

## For the lead

- Run: the Forward+ matrix (`--parity`, unbaked and baked) after the merge; the new arms need a fresh export. CLAUDE.md's
  arm count becomes 101.
- Not run: Firefox, the scene smoketest, `perf_guard` (nothing here adds per-frame work), the unbaked matrix.
- Shared docs: TASKS §2.42-2.44 to §4, §2.31 follow-up 3 done, `drivers/webgpu/README.md`'s "no texture component
  swizzle" paragraph can add RGBA4444/RGB565 and the new warning.
- Risks: the swizzle normalization relies on Image conversions writing 0/1 into missing channels (they do; DXT1's
  punch-through alpha is the one exception, handled). DXT1 kept compressed as BC1 on the web still decodes punch-through
  blocks to alpha 0, as before this task (Godot's BC1 encoders are not known to emit them for RGB images; not proven).
