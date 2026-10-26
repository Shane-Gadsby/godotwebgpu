# 27-fsr2-baked-uniform-mismatch: FSR 2 on a web export fails with "Mismatch uniform type ... Expected 'Image', supplied: 'StorageBuffer'"

State: integrated (lead: bake_isolation PASS, probes 7 -> 0 misses, camera-sim Chrome + Firefox 0 translated, fsr2/taa parity unbaked + baked)
Owner: worker-27
Claimed: 2026-10-26 16:45
Needs: build, gpu (web captures, parity)
Branch: work/27
Results: webgpu_notes/work/results/27-fsr2-baked-uniform-mismatch/

## Context

User report, 2026-10-26. A WebGPU export (template `v4.8.dev.custom_build.0bffec3bb`, 2026-10-25 20:56, post-swap,
18 commits behind `webgpu-4.8` at the time; Forward+, single-threaded, GDExtension/dlink) with FSR 2 logs, every frame:

```
Shader baker: baking Forward+'s advanced shader variants, needed for TAA (project setting rendering/anti_aliasing/quality/use_taa).
ERROR: Shader 'Fsr2ComputeLuminancePyramidPassShaderRD:0' Mismatch uniform type for binding (1), set (1). Expected 'Image', supplied: 'StorageBuffer'.
ERROR: servers/rendering/renderer_rd/uniform_set_cache_rd.h:119 - Condition "rid.is_null()" is true. Returning: rid
ERROR: servers/rendering/rendering_device.cpp:7273 - Parameter "uniform_set" is null.
ERROR: Uniforms were never supplied for set (1) at the time of drawing, which are required by the pipeline.
(the same for Fsr2ReconstructPreviousDepthPassShaderRD:1 binding 5 Image, Fsr2DepthClipPassShaderRD:1 binding 0
 Texture, Fsr2LockPassShaderRD:1 binding 2 Image — repeated every frame)
Shader baker: left out 4455 shader group(s) this export cannot use (shader_baker/variant_pruning).
```

The check is `rendering_device.cpp`'s `uniform_set_create()` (the shader's reflected type vs the supplied one), so FSR 2
draws nothing. Most likely the user's camera-sim project (`/mnt/109313D2109313D2/godot-game-dev/camera-sim`, their own
git repo: **never modify it**, scratch copy per LIVE_REPRO_METHODOLOGY.md); ask the lead if it reproduces nowhere else.

Unverified lead from a glance (check it, don't assume it): on WebGPU, `fsr2.cpp` puts the resources FSR 2 updates
atomically into storage buffers instead of images (`create_resource_rd()`, the `SUPPORTS_IMAGE_ATOMIC_32_BIT` variant,
amd-fsr2 patch 0004), and binds them as `UNIFORM_TYPE_STORAGE_BUFFER` (~lines 487/504). A shader that expects an image
there was compiled without that variant — e.g. the export-time baker (`ShaderBakerExportPlugin`, the advanced group
TAA pulls in, `shaders_extra.bin`, variant pruning, TASKS.md §2.27 / §4.23) baking FSR 2 with the editor's own
capabilities/defines instead of WebGPU's, so the baked module and the runtime's binding plan disagree. The Forward+
matrix's FSR 2 arm passes 82/82, so find what that export does differently (TAA on, pruning, dlink template, the
separate shaders pack). Also check whether the four slot types are the only mismatches and whether `0bffec3bb..HEAD`
already changed anything here.

## Goal

1. Reproduce in a web export, find the cause with evidence (which shader module, from which source — baked, extra
   pack, runtime conversion — reflects which type, and why).
2. Fix it at the cause so baked and runtime FSR 2 agree on every backend, and no other shader the baker builds with
   capability-dependent defines has the same split (look for the pattern, list what you checked).
3. A regression check that would have caught it: an export shaped like the user's (TAA + FSR 2, pruning on, the
   templates and options it used) whose console must show no "Mismatch uniform type" and whose FSR 2 frame passes
   `--parity`; a matrix arm or a smoketest/export check, whichever fits the existing tiers.

## Verification

- The user's-shape export clean in Chrome on the real GPU (and Firefox), FSR 2 visibly working, frame pair native beside
  WebGPU in `looks/`; `run_forward_plus.mjs --parity --feature` for the FSR 2 / TAA arms passes, baked and unbaked;
  `webgpu_notes/work/smoke.sh` passes.
