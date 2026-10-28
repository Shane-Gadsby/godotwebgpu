# Work task 59: converter smoke check as a webgpu_tests tier (TASKS §2.38)

## What changed (branch work/59)

- `webgpu_notes/work/smoke.sh` → `webgpu_tests/converter_smoke/run.sh`, `smoke_expected_invalid.txt` →
  `webgpu_tests/converter_smoke/expected_invalid.txt` (git mv; the list is still empty). `converter_smoke/`, not
  `spirv_cross_eval/`: `spirv_cross_eval/` is the swap's evaluation tooling (project exports, spec-op probes), while this
  is a standing regression tier that `local_ci.sh` runs; it still uses `spirv_cross_eval`'s `dump_corpus.py`,
  `compare_interface.py`, `interface_reference.json` and `validate_wgsl.mjs` from where they are.
- `webgpu_notes/work/smoke.sh` is a two-line forwarder (`exec .../webgpu_tests/converter_smoke/run.sh "$@"`). Old
  results and task files untouched.
- `run.sh`: took no leases before either; the header now says callers take them. New: `KEEP=0` deletes a passing run's
  directory (~20 MB each); the Chrome step's exit status is read, so an adapter refusal fails with its own message
  instead of a Python traceback on a missing `validation.json`.
- `spirv_cross_eval/validate_wgsl.mjs` now **asserts the adapter** before compiling anything (it only logged it):
  Chrome nvidia/lovelace, not fallback, 48 sampled textures; Firefox an adapter that is not the fallback. Exit 3 with
  `ADAPTER REFUSED (...)`, which says how to run elsewhere: `WGSL_VALIDATE_ADAPTER=<vendor>/<architecture>` or
  `WGSL_VALIDATE_ADAPTER=any` (any real adapter; the fallback adapter is never accepted). Launch flags unchanged.
  This also covers its other callers (`wgsl_text_scan/run_tests.sh VALIDATE=1`, `spirv_cross_eval/run.sh`, project eval).
- `local_ci.sh`: "Converter smoke (every engine variant)" in stage 1, so `--quick` runs it
  (`KEEP=0`, scratch `$CONVERTER_SMOKE_DIR`, default `${TMPDIR:-/tmp}/godot-converter-smoke`); usage/mode lines updated.
- `wgsl_text_scan/run_tests.sh` reads the expected-invalid list from the new path; its and `compare_interface.py`'s
  comments, `webgpu_tests/README.md` (table row + section "1b. Converter smoke check") and CLAUDE.md's Testing block and
  stored-references paragraph point at the new tier. (CLAUDE.md was edited on the branch because the task file asks for
  it; two small hunks.)

## Verification (4080 SUPER, Chrome stable, launch rule; gpu + build leases held)

New path and forwarder, same tree:
```
[WGSL Precompile] Results: 311 compiled, 0 glsl failures, 0 conversion failures
identical interface: 311/311, mismatched: 0, missing in candidate: 0, not in reference: 0
against the stored reference: 622/622 identical, 0 differ, 0 missing, 0 not in reference
adapter (chrome): {"vendor":"nvidia","architecture":"lovelace","fallback":false,"maxSampledTexturesPerShaderStage":48,...}
validation: 311 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261028-172247-2324930)     # webgpu_tests/converter_smoke/run.sh
SMOKE OK (smoke-20261028-172256-2329229)     # webgpu_notes/work/smoke.sh (forwarder)
```
~10 s a run with the converter built (the worktree's first clean build of `wgsl_convert_cli` took 39 s).
Adapter check: `WGSL_VALIDATE_ADAPTER=intel/xe` → validator exit 3 and `run.sh` ends `FAIL: Chrome validation did not
run: adapter check` / `SMOKE FAILED` (interface and scan steps still reported); `WGSL_VALIDATE_ADAPTER=any` → exit 0.
`pre-commit run --files <changed>`: all passed. `bash -n` on the three shell scripts, `node --check` on the validator.

## Not run / for the lead

- `local_ci.sh --quick` end to end was not run (its scene smoketest needs exports and the GPU for minutes); the new
  `run_test` line is the same command as above with `KEEP=0`. Worth one `--quick` run after merging.
- The work board's README still says `webgpu_notes/work/smoke.sh` (correct through the forwarder); TASKS.md §2.38 → §4
  text in for-tasks.md.
- Risk: any script that called `validate_wgsl.mjs` on a non-NVIDIA machine now exits 3 unless it sets
  `WGSL_VALIDATE_ADAPTER`; that is the point (trap 79), but it is a behavior change.
