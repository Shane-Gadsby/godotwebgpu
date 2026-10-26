# 27-fsr2-baked-uniform-mismatch: SUMMARY (worker-27)

State: ready-for-integration. The GPU browser runs are left for the lead: the `gpu` lease was held the whole time
(worker-24, then lead / lead-final until 20:45).

## Proven cause: the editor's own FSR 2, during the export's shader bake (not the web export)

- `ShaderBakerExportPlugin` installed WebGPU's capability answers (all features false) on the editor's live
  `RenderingDevice` for the whole export. The editor keeps drawing frames between export steps. FSR 2 asks
  `has_feature(SUPPORTS_IMAGE_ATOMIC_32_BIT)` on every dispatch (`execute_gpu_job_compute_rd()`), got false, and bound
  STORAGE_BUFFER to the editor's image-atomics shaders.
- The shader names prove the device: `:N` is the ShaderRD variant. `Fsr2DepthClipPassShaderRD:1` (also Reconstruct:1
  and Lock:1) is the FFX_HALF variant, which exists only on FP16 devices, never WebGPU.
  `Fsr2ComputeLuminancePyramidPassShaderRD:0` is the image-atomics variant, which WebGPU never declares.
- Reproduced exactly with the lead's unfixed editor `72e741beb`. An interactive export from a windowed editor (Xvfb,
  Vulkan, RTX 4080 SUPER) of a camera-sim scratch copy gives the same 4 messages and the same
  `left out 4455 shader group(s)` line as the user's log. A 1-cube fixture also reproduces it.
  `--export-release` does not reproduce it, because it never draws the 3D viewport, which is why no tier saw it.
- The task's unverified lead (FSR 2 baked without WebGPU's variant) was right about a real split, but that split is a
  second bug, not the cause of the errors. FSR 2 built its mode list from `has_feature()` once, at construction:
  different variant count and text per device, and no refresh callback. So the bake baked the editor's flavor (FP16 +
  image atomics) under cache keys the WebGPU runtime never looks up, and dropped 7 unconvertible FSR 2 stages. Seven
  passes (all but RCAS) therefore miss the cache on the web; this follows from the key construction in
  `ShaderRD::_initialize_cache()`. The runtime log confirming it is pending (see below).
- `0bffec3bb..webgpu-4.8` touched none of fsr2.cpp, shader_rd.cpp or the baker. The bug reproduces at HEAD.
- The user's camera-sim `project.godot` was edited at 16:56 today and FSR 2 (`scaling_3d/mode=2`) was removed. My
  scratch copy puts it back to match the report.

## What changed (work/27)

1. `RenderingDevice::has_feature()` applies the bake overrides only on a thread inside
   `RD::ShaderBakeFeatureOverrideScope` (a per-thread depth counter). The baker opens one in
   `_begin_customize_resources`, `_customize_resource` and `_customize_scene`.
   `shader_bake_feature_override_is_active()` is unchanged. This also covers every other per-frame `has_feature()` in
   the editor (fog, GI, VRS, FP16, depth resolve), which had the same exposure silently.
2. `fsr2.cpp`: capability variants are now shader groups with bake requirements, as fog.cpp does it:
   - depth clip, reconstruct, lock: 4 groups (atomics x FP16);
   - luminance pyramid: 2 groups;
   - accumulate, autogen reactive, TCR autogen: FP16 groups.

   Only the selected group is enabled, and the texts are identical on every device, so the cache keys agree. The baker
   prunes by requirements. The WebGPU groups' text is byte-identical to the old WebGPU variants, so the precompiled
   table and the references are unchanged.
3. `FSR2Effect::Device::use_image_atomics` is recorded once. `create_resource_rd()` and the dispatch bindings follow it
   instead of asking `has_feature()` again.
4. New tier `webgpu_tests/bake_isolation/run.sh` (fixture project + editor plugin): an interactive Web export from a
   windowed editor under Xvfb with FSR 2 + TAA. It FAILs on any "Mismatch uniform type" / "Uniforms were never
   supplied" in the editor log, on an editor that drew no frame during the bake (inconclusive), on an unbaked FSR 2
   pass, or on an FSR 2 stage baked in the editor's flavor.
5. `wgsl_precompile.py`: comment only.

## Other shaders checked for the same split

The full list is in for-tasks.md. Only FSR 2 lacked both groups and a refresh callback. FSR 1, the clustered SDF
variant, SDFGI and the raster octmap use refresh callbacks. Fog and Forward Mobile FP16 are groups. The cluster
builder declares both variants. Point size is a spec constant. `modules/` has no `RD::has_feature()` users.

## Verification

| check | before (lead editor 72e741beb) | after (work/27) |
|---|---|---|
| `bake_isolation/run.sh` | FAIL: 4 Mismatch + 4 never-supplied over 9 bake frames; 7 FSR 2 stages dropped | PASS: 17 bake frames, 0 errors, 8/8 FSR 2 passes baked in WebGPU flavor |
| camera-sim scratch (FSR 2 + TAA, dlink nothreads release, pruning Automatic, separate pack), interactive export | 64 Mismatch (16 per pass x 4) over 15 frames; left out 4455; 7 dropped | 0 Mismatch over 15 frames; left out 4481 (+26 = FSR 2's unselectable groups); 0 dropped |
| baked depth-clip group in index.shaders.pck | editor flavor `20a55a1f...` | `adaafa8e...` = group 2 (NO_IMAGE_ATOMICS, FP32), the one WebGPU enables |
| smoke.sh | | SMOKE OK |
| pre-commit (changed files) | | clean |

```
[WGSL Precompile] Results: 276 compiled, 0 glsl failures, 0 conversion failures
identical interface: 276/276, mismatched: 0, missing in candidate: 0, not in reference: 0
against the stored reference: 552/552 identical, 0 differ, 0 missing, 0 not in reference
validation: 276 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261026-170122-2838538)
```

Builds: editor + web template_release nothreads (dlink and non-dlink), from this tree under the build lease, with a
capped scope. The lead's bin/ dlink zips are a mix: godot.js/godot.wasm from 2026-10-25 21:06, side.wasm from today.
My repro used my own dlink build.

## What the lead must still run (gpu lease)

Exports are in `/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/86fd2a38-7e49-49e0-a3fd-d31362379e4b/scratchpad/`:
- `out1`: fixture, lead editor + template (before);
- `out_new`: fixture, work/27 (after);
- `cam_old` / `cam_new`: camera-sim scratch, dlink template.

`runtime_probe.cjs <dir> <outprefix> [chrome|firefox]` (in this folder) serves an export with COOP/COEP, runs it headed
in system Chrome on the real GPU with `--verbose`, and prints the adapter, the FSR 2 cache-miss count and the errors.

1. `out1` vs `out_new`: expect 7 Fsr2 cache misses before and 0 after, and no Mismatch on either.
2. `cam_new` in Chrome and Firefox; put the FSR 2 frame beside native in looks/.
3. After merging, rebuild and re-export, then:
   `cd webgpu_tests/forward_plus && ./export.sh && BAKE=1 ./export.sh && WEBGPU_REAL_GPU=1 node run_forward_plus.mjs --parity --feature fsr2,taa`
   and the same with `--baked`.
4. `webgpu_tests/bake_isolation/run.sh` on the merged editor (~1 min, Xvfb + Vulkan, no browser, no lease).

## Risks

- Shader text that the bake-start refresh callbacks switch to the target flavor stays switched until the bake ends.
  So a version the editor first compiles mid-export is compiled in WebGPU flavor. This was already the case before.
- Native backends now compile only the selected FSR 2 group (less work, same variant indices). Native FSR 2 was not
  checked by eye; the `--parity` run covers it.
- Any code that relied on the override outside the three scoped callbacks now sees the editor's answers. The bake
  outputs above (pruning counts, FSR 2 groups, smoke) match what is expected.
- The tier needs Vulkan under Xvfb, which NVIDIA provides here; CI runners may not have it.
- Pre-existing and cosmetic: the baker's verbose `baking '<path>'` line always prints group 0's path.
