# 13-harden-passes: malformed SPIR-V is rejected, never asserted on

State: open
Owner:
Claimed:
Needs: —
Branch: work/13
Results: webgpu_notes/work/results/13-harden-passes/

## Context

User decision (2026-10-25): harden now. Task 06's fuzzers found SPIRV-Tools assertions on malformed SPIR-V inside our
shared preprocessing (`ProcessCallTreeFromRoots` "function does not exist" in InlineOpaque and AggressiveDCE,
`DefUseManager::AnalyzeInstUse` "Definition is not registered", `CFG::RegisterBlock` "must have a terminator") and one
libFuzzer OOM; reproducers in `webgpu_notes/work/results/06-test-tiers/fuzz_repro/` (on webgpu-4.8).

## Goal

`spirv_preprocess::run_pipeline()` validates its input (SPIRV-Tools' validator, the target env the passes assume) and
returns a clean failure for invalid modules before any optimizer pass runs; each optimizer-based pass checks its
preconditions; the OOM case is bounded. Callers (runtime, `tint_convert_cli`, baker) already treat a failed conversion
as "keep the SPIR-V / fall back" — confirm each does. Measure the validation cost on the 276-variant corpus (it runs on
every conversion) and keep it small; if it is not, validate only where input can be untrusted.

## Verification

Every fuzz reproducer is rejected with a message, no abort; the fuzzers run 10 minutes each without a crash artifact;
`smoke.sh` OK; corpus conversion time before/after in SUMMARY.
