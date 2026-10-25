# 06-test-tiers: summary

State: ready-for-integration (branch work/6).

## What changed
- `TINT_CONVERT_CLI=<path>` selects the converter in every tier that drives it (preprocessing_tests, shader_corpus run_tests + validate_spirv_dump, spec_constant_overrides, wgsl_cache/test_determinism.sh, wgsl_precompile.py). Default bin/tint_convert_cli (SPIRV-Cross); Tint reference bin/tint_convert_cli.tint.
- preprocessing_tests: 32 assertions parsed Tint's WGSL spelling. Now backend-neutral (canon(): vec4f vs vec4<f32>, 0i vs 0; `u` suffixes on bindings/array sizes; module-scope const or var<private> for hoisted arrays; `switch (`; Y-negation pattern).
- SPIRV-Cross regression fixed: patch 0006 (16-bit normalized storage formats; CanvasSdfShaderRD:4,5.comp failed to convert, Tint did them).
- shader_corpus/expected_failures.json: reason per entry; same 11 failing variants under both backends (339-file dump from the editor on lavapipe).
- Fuzzers: build_fuzzers.sh was already stale (object dirs); fixed, ported (SPIRV-Cross default, WGSL_BACKEND=tint option), child-process signal handlers reset so forked-backend aborts do not write false crash artifacts.
- CI: tint-cli cache keys include the SPIRV-Cross sources.

## Tier table (commands run from the tier's directory with TINT_CONVERT_CLI set)
| Tier | Command | Tint | SPIRV-Cross (before fixes) | SPIRV-Cross (after) |
|---|---|---|---|---|
| shader_corpus | compile_fixtures.sh; node run_tests.mjs | 14/14 | 14/14 | 14/14 |
| validate_spirv_dump (339 dumped .spv) | node validate_spirv_dump.mjs /tmp/dump | no regressions, 11 XFAIL | 2 regressions (CanvasSdf 4,5) | no regressions, same 11 XFAIL |
| driver_unit_tests | node run_tests.mjs | 379/0 | 379/0 | 379/0 |
| preprocessing_tests | node run_tests.mjs | 291/0 (1 skip) | 257/34 | 291/0 (1 skip) |
| spec_constant_overrides | node run_tests.mjs | 5/0 (browser half skipped: no playwright here) | 5/0 | 5/0 |
| wgsl_cache determinism | ./test_determinism.sh | deterministic, 1816915 B | deterministic | deterministic, 1562162 B |
| fuzzers | build_fuzzers.sh; bin/fuzz_* 60 s each | builds + runs (.tint) | n/a | build + run; findings in for-tasks.md |
| local_ci.sh --quick | not run | | | needs editor export + GPU scene smoketest; GPU/build rules |

Fuzz findings: SPIRV-Tools asserts on malformed SPIR-V in the shared preprocessing (4 reproducers in fuzz_repro/), backend-independent.
Render/GPU tiers not run (game on the GPU, board rule).

## smoke.sh (last lines)
```
identical interface: 276/276, mismatched: 0, missing in candidate: 0
WGSL bytes: reference 6287157, candidate 6686228 (1.063x)
validation: 276 modules, 1 invalid, 1 of them expected
SMOKE OK
```
(smoke needed a node_modules symlink for playwright in the worktree; not committed.)

## Lead must run / risks
- Patch 0006 needs the thirdparty/README.md line (for-docs.md). Hoisted-array perf (module const vs var<private>) untested on GPU (for-tasks.md).
