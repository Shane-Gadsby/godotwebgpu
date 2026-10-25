# Task 09 for TASKS.md (lead folds in)

## §2.35 (close-out text)

**Tint removed 2026-10-25 (work task 09).** `thirdparty/tint` (830 files), `tint_wrapper.*`, `tint_ir_transforms.*`,
the `webgpu_wgsl_backend` SCons option and the `WGSL_BACKEND` build.sh switch are gone; SPIRV-Cross's WGSL backend is
the only SPIR-V → WGSL converter (runtime, precompile, export bake, tests). The host tool is `bin/wgsl_convert_cli`,
built by `drivers/webgpu/wgsl_cli/build.sh` (C++17 now, ~210 objects instead of ~570; Tint objects were 2/3 of a cold
CLI build). `WGSL_DEBUG_DUMP_PREPROCESSED` replaces `TINT_DEBUG_DUMP_PREPROCESSED` (old name honoured for one release),
`WGSL_CONVERT_CLI` replaces `TINT_CONVERT_CLI` in the tiers, `--skip-wgsl-cli` replaces `--skip-tint-cli` in
build-linux.sh (old flag still accepted). The web templates did not change size: since the swap started the default
backend was already SPIRV-Cross, so no Tint was linked into them; the saving is build time (384 Tint TUs per variant
that a `webgpu_wgsl_backend=tint` build compiled) and source size (-204k lines).

Interface reference: with no Tint to compare against, `webgpu_tests/spirv_cross_eval/interface_reference.json`
(bindings + kinds + override ids of the 276 engine variants, as Tint emitted them at 7f2bf18eb2) and
`webgpu_tests/wgsl_text_scan/scan_reference.txt` (the driver's WGSL text-scan facts for the same variants, both
modes) are what smoke.sh and the text-scan tier compare with. SPIRV-Cross matched both 276/276 and 552/552 on work/9.
After a deliberate interface change, regenerate with `compare_interface.py --write` / `WRITE_REFERENCE=1
run_tests.sh` and say why in the commit.

## §3 trap (new)

**An editor built before the rename cannot bake.** The editor looks for `wgsl_convert_cli` beside itself
(`wgsl_bake_subprocess.cpp`); an editor built before work/9 looks for `tint_convert_cli`, and an editor from after it
paired with a stale `tint_convert_cli` finds nothing either — the export warns once ("not found next to the editor
executable") and ships every shader unbaked, which works but loads slowly (baked: 0). Rebuild editor, tool and
templates together after merging (they must be anyway), and update anything that copies the tool by name (CI artifacts,
`project_export.sh` wrappers in older results folders).

## §4 archive note

work/9: verified with the editor + all 8 web variants built with `dev_mode=yes` (werror) in the worktree, smoke.sh OK
(276 converted, interface 276/276 against the stored reference, scan facts 552/552, 276 valid in Chrome),
shader_corpus 14/14, preprocessing 291/0 (1 static skip), driver unit tests 379/0, spec_constant_overrides 5/0
offline, determinism OK, fuzzers build and smoke (`build_fuzzers.sh --test`, whose libFuzzer smoke args were wrong
before and are fixed), local_ci (see SUMMARY).
