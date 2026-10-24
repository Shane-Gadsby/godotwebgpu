# 08-gpu-timing: does SPIRV-Cross WGSL cost GPU time?

State: open
Owner:
Claimed:
Needs: gpu
Branch: work/8
Results: webgpu_notes/work/results/08-gpu-timing/

## Context

Chrome recompiles WGSL with Tint into SPIR-V/MSL/HLSL anyway, but the shape of the WGSL (helper functions, pointer
params, `let` vs `var`, loops, the private-variable stage I/O wrapper SPIRV-Cross uses) can still change what the GPU
runs. Tools: `webgpu_tests/perf_guard/gpu_time.mjs --dir <export> --uncapped --passes`, `api_profile.mjs`,
`run_perf_guard.mjs` (CLAUDE.md "Testing"); the heavy Forward+ export `webgpu_tests/forward_plus/export_heavy.sh`.

## Goal

Same exports baked with each converter (`bin/tint_convert_cli.tint-pre-swap` vs the SPIRV-Cross build), measured
per pass on the real GPU, three runs each, after checking the adapter and `nvidia-smi` (no game, no LM Studio
holding VRAM). Anything slower by more than noise: find the WGSL construct responsible and propose a SPIRV-Cross
patch (or write it).

## Verification

Per-pass GPU ms table, both converters, mean ± spread, in SUMMARY.md.
