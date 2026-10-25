# 14-spec-composite-fold: freeze_spec_constant_ops() evaluates composite ops instead of folding to 0

State: ready-for-integration
Owner: worker-14
Claimed: 2026-10-25 12:05
Needs: — (gpu lease for the browser half of the probes)
Branch: work/14
Results: webgpu_notes/work/results/14-spec-composite-fold/

## Context

User decision (2026-10-25): fix inside the swap. Found by task 04: `freeze_spec_constant_ops()` (spirv_preprocess) folds
`OpSpecConstantOp` CompositeExtract, CompositeInsert and VectorShuffle of a spec-constant composite to 0, with Tint and
SPIRV-Cross alike. No engine shader takes the frozen path; a user shader that shuffles a spec-constant vector computes 0.
Task 04's probes reproduce it: `webgpu_tests/spirv_cross_eval/run_spec_ops.sh` (3 of its 4 expected failures).

## Goal

Evaluate those three ops (and any other composite op the freezer meets) on the constant composites, so frozen modules
compute what Vulkan does; move the three probes from expected-failure to pass (the script fails if an expected failure
starts passing, so update its list). SMod's negative-operand case stays as it is (GLSL leaves it undefined).

## Verification

`run_spec_ops.sh` with the three probes passing on both backends; `spec_constant_overrides` 8/8; `smoke.sh` OK.
