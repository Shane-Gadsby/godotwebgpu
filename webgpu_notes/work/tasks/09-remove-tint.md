# 09-remove-tint: Tint leaves the tree

State: ready-for-integration
Owner: worker-09
Claimed: 2026-10-25 18:45
Needs: build
Branch: work/9
Results: webgpu_notes/work/results/09-remove-tint/

## Goal

Delete `thirdparty/tint`, `tint_wrapper.*`, `tint_ir_transforms.*`, the Tint branches of `tint_cli/build.sh`, SCsub,
fuzzers, `smoke.sh`'s reference step, the temporary backend options, Tint entries in `thirdparty/README.md`, CI
workflow steps, and every comment that describes Tint as the converter (`git grep -i tint`; Chrome's own Tint stays
in comments where that is what is meant).

**Decided by the user (2026-10-25):** delete `thirdparty/tint` once the bar in the README holds (not kept as a fallback);
**rename `tint_convert_cli` to `wgsl_convert_cli`** everywhere — `tint_cli/` directory and `build.sh`,
`wgsl_bake_subprocess.cpp`'s lookup (also `.exe`), `wgsl_precompile.py`, `TINT_CONVERT_CLI` → `WGSL_CONVERT_CLI` in the test
tiers, build helpers, CI workflows and `package-editor`, docs. An editor built before the rename cannot find the new name:
the editor and templates are rebuilt together afterwards (they must be after any commit anyway). Rename
`TINT_DEBUG_DUMP_PREPROCESSED` too, keeping the old variable as an alias for one release.

## Verification

`git grep -il tint` reviewed line by line; all 8 web variants + editor build; `local_ci.sh --dev-mode` green; the lead
repeats task 07's final parity run on the result.
