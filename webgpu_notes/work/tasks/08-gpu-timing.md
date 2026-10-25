# 08-gpu-timing: does SPIRV-Cross WGSL cost GPU time?

State: ready-for-integration
Owner: worker-08
Claimed: 2026-10-25 12:05
Needs: gpu
Branch: work/8
Results: webgpu_notes/work/results/08-gpu-timing/

## Context

Chrome recompiles WGSL with Tint into SPIR-V/MSL/HLSL anyway, but the shape of the WGSL (helper functions, pointer
params, `let` vs `var`, loops, the private-variable stage I/O wrapper SPIRV-Cross uses) can still change what the GPU
runs. Tools: `webgpu_tests/perf_guard/gpu_time.mjs --dir <export> --uncapped --passes`, `api_profile.mjs`,
`run_perf_guard.mjs` (CLAUDE.md "Testing"); the heavy Forward+ export `webgpu_tests/forward_plus/export_heavy.sh`.

**Scope widened (user, 2026-10-25):** "Firefox and Chrome on Android must work, ideally as performant or better than
Tint". Measure desktop Chrome here; Firefox (desktop) and Android (Chrome, Firefox) are tasks 11 and 12, same method.

## Goal

Same exports baked with each converter (`bin/tint_convert_cli.tint-pre-swap` vs the SPIRV-Cross build), measured
per pass on the real GPU, three runs each, after checking the adapter and `nvidia-smi` (no game, no LM Studio
holding VRAM). Anything slower by more than noise: find the WGSL construct responsible and propose a SPIRV-Cross
patch (or write it).

**Check first (from task 06):** SPIRV-Cross emits dynamically indexed constant arrays as module-scope
`const _N : array<...>`, where Tint emitted the `var<private>` that `hoist_constant_local_arrays` (TASKS.md §2.30, SSIL)
was written to produce — the pass exists because Chrome/Dawn re-materialized a function-local constant array on every
access. Whether a module `const` indexed at run time has the same cost is unknown: measure SSIL's pass time with both
converters, and if it regressed, make SPIRV-Cross emit `var<private>` for those arrays (a local patch).

## Verification

Per-pass GPU ms table, both converters, mean ± spread, in SUMMARY.md.
