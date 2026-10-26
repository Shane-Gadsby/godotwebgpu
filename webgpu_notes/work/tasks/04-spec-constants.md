# 04-spec-constants: specialization constants through SPIRV-Cross overrides

State: integrated (merged f62806e2e3; smoke OK, spec_constant_overrides 8/8 offline on the merged tree)
Owner: worker-04
Claimed: 2026-10-25 09:40
Needs: — (gpu for the browser half of the tier)
Branch: work/4
Results: webgpu_notes/work/results/04-spec-constants/

## Context

`spirv_preprocess::spec_constants_overridable()` decides per module whether spec constants stay in the SPIR-V (Tint
emitted `@id(N) override`, set through pipeline constants) or are frozen by `freeze_spec_constant_ops()` and specialized
by re-patch + re-convert (CLAUDE.md "Specialization constants"; `drivers/webgpu/README.md`). Its rules encode what *Tint*
could lower (`OpSpecConstantOp` kinds, composites, array sizes, workgroup sizes). SPIRV-Cross emits scalar overrides
and composites as override-expressions inline (`spirv_wgsl.cpp` ~2470-2530) and may accept more or less. The corpus
shows identical override ids for all 276 variants (216 overrides), but the corpus is built with defaults.

## Goal

1. Map SPIRV-Cross's spec-constant support against `spec_constants_overridable()`'s rules; where SPIRV-Cross can lower
   something Tint could not, decide whether to widen (fewer re-converts) — with evidence — or keep the rules.
2. Make `webgpu_tests/spec_constant_overrides` run against the SPIRV-Cross converter (it finds `bin/tint_convert_cli`),
   both halves; fix what fails.
3. Check the legacy re-convert path (frozen modules) converts with SPIRV-Cross for every engine shader that takes it.

## Verification

`spec_constant_overrides/run_tests.mjs` passes (offline half; browser half under the `gpu` lease, headed); a list of
the engine shaders that take each path, before and after.
