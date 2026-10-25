# 06-test-tiers: the offline test tiers run on SPIRV-Cross

State: ready-for-integration
Owner: worker-06
Claimed: 2026-10-25 09:45
Needs: —
Branch: work/6
Results: webgpu_notes/work/results/06-test-tiers/

## Context

Tiers that drive `tint_convert_cli` or assume Tint's output: `webgpu_tests/shader_corpus` (`compile_fixtures.sh`,
`run_tests.mjs`, `expected_failures.json` — Tint's known failures, `validate_spirv_dump.mjs`),
`webgpu_tests/preprocessing_tests` (incl. Test 22 via `--image-decls`), `webgpu_tests/driver_unit_tests`
(`test_image_decl_mapping.mjs`, `test_override_filtering.mjs`, anything parsing WGSL text),
`webgpu_tests/wgsl_cache/test_determinism.sh`, `drivers/webgpu/tint_cli/fuzz/` (fuzzers), and the CI workflow
`.github/workflows/webgpu_tests.yml`. Tier list: CLAUDE.md "Testing", `webgpu_tests/README.md`.

## Goal

Every one of them runs and passes against the SPIRV-Cross converter (and keeps passing against Tint until task 09).
Replace Tint-specific expectations with backend-neutral ones (re-derive `expected_failures.json` for SPIRV-Cross with a
reason per entry; an entry that is a real regression vs Tint becomes a proposed task, not a baseline). Port the fuzzers
to call the SPIRV-Cross wrapper (keep a Tint build option until 09). Note anything CI-only for the lead (`for-tasks.md`).

## Verification

A table in SUMMARY.md: tier, before (Tint) result, after (SPIRV-Cross) result, with the commands. `webgpu_tests/
local_ci.sh --quick` result included.
