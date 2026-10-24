# 07-forward-plus-parity: Forward+ against native Vulkan with SPIRV-Cross WGSL

State: open
Owner: lead
Claimed:
Needs: gpu
Branch: (lead)
Results: webgpu_notes/work/results/07-forward-plus-parity/

## Context

The acceptance test of the swap. Before the swap the Forward+ matrix was at parity on every feature
(`run_forward_plus.mjs --parity`, TASKS.md §4.29: 71/71 except the spec-limited gaps of §2.31). Export-time baking runs
the converter out of process, so a baked export made by the *existing* editor + templates with the SPIRV-Cross
`tint_convert_cli` already renders SPIRV-Cross WGSL for every baked shader — no engine rebuild needed for a first
reading; the full reading needs task 03's templates (runtime conversion) and task 02's scans.

## Plan

1. Baseline at `pre-SPIR-V-Cross` with `bin/tint_convert_cli.tint-pre-swap` (or the recorded §4.29 result if the
   export is reproducible): export fp_check baked, `--parity`, record per-feature results.
2. Same export with the SPIRV-Cross converter, `--parity`; every difference becomes a fix task (`08+`).
3. After 02 and 03 are integrated: rebuild editor + templates together, re-export, `--parity` again, plus the scene
   smoketest and `perf_guard`.

## Verification

The per-feature parity table, before and after, in results; zero features worse than the baseline.
