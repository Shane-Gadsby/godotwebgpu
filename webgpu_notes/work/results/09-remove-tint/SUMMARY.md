# 09-remove-tint: SUMMARY (worker-09, 2026-10-25)

State: ready-for-integration, branch work/9 (two commits on webgpu-4.8 @ 7f2bf18eb2).

## What changed

- **Deleted**: `thirdparty/tint` (830 files), `drivers/webgpu/tint_wrapper.*`, `tint_ir_transforms.*`; the
  `webgpu_wgsl_backend` SCons option (platform/web/detect.py; `-fwasm-exceptions` is now always on the webgpu link),
  the Tint half of `drivers/webgpu/SCsub`, the `WGSL_BACKEND` switch in the CLI and fuzzer build scripts, every
  `WGSL_BACKEND_TINT` `#ifdef`, `tint_wrapper_initialize()` calls. SPIRV-Cross is the only converter.
- **Renamed**: `drivers/webgpu/tint_cli/` -> `drivers/webgpu/wgsl_cli/` (git mv), `bin/tint_convert_cli` ->
  `bin/wgsl_convert_cli` (+ `.exe`) everywhere: `wgsl_bake_subprocess.cpp` lookup, `wgsl_precompile.py`, SCsub
  fingerprint, `platform_macos_builders.py` bundle copy, build.sh / build-linux.sh / build-macos.sh /
  build-windows.ps1, all CI workflows (cache keys now hash spirv-cross-wgsl + all pass sources; matrix key
  `wgsl-cli`; artifact `wgsl-convert-cli`), `.github/actions/package-editor`, `.pre-commit-config.yaml` exclude,
  test tiers (`WGSL_CONVERT_CLI`, `findWgslCli`). `WGSL_DEBUG_DUMP_PREPROCESSED` replaces
  `TINT_DEBUG_DUMP_PREPROCESSED`, which `wgsl_cli/main.cpp` still honours (alias, one release). build-linux.sh:
  `--skip-wgsl-cli`, `--skip-tint-cli` kept as an alias. The CLI is C++17 now, 211 objects instead of 573.
- **smoke.sh** (my call): the interface comparison stays, against stored references made from Tint's output at
  7f2bf18eb2 with a Tint CLI built from the base tree in my scratchpad: `webgpu_tests/spirv_cross_eval/
  interface_reference.json` (bindings + kinds + override ids, 276 variants, 85 KB, one line per module) via
  `compare_interface.py <manifest> <dir>`, and `webgpu_tests/wgsl_text_scan/scan_reference.txt` (the driver's WGSL
  text-scan facts, 552 scans, 94 KB) via the rewritten `wgsl_text_scan/run_tests.sh` + `scan_reference.py`, which
  smoke.sh now also runs. Against a manifest, a reference module missing from the candidate fails (so the check cannot
  silently shrink); new modules are listed. Regenerate after a deliberate change: `compare_interface.py --write`,
  `WRITE_REFERENCE=1 run_tests.sh <run>`. Negative-tested (a moved binding and a deleted module fail both).
  Why keep it: it is the only check that the driver's bind-group layouts / pipeline constants still line up without
  rendering, and it is what caught interface drift during the swap.
- **Tests/tooling**: `shader_corpus/expected_failures.json` reasons are SPIRV-Cross-only; tier messages say SKIP (not
  "PASS (skipped)") when the CLI is missing; `project_eval.py` takes one CLI and an optional reference build;
  `run_spec_ops.sh` / `run.sh` drop the Tint reference notes; fuzzers build without Tint and `build_fuzzers.sh --test`
  passes its smoke (it was passing `--iterations 0` to libFuzzer, which read "0" as a corpus dir and failed on every
  build; fixed). Log matchers in run_scenes.mjs / smoke_test.mjs look for `SPIR-V→WGSL` instead of "Tint conversion".
  The driver's stats line is now `... cache_hits=N misses=N`: `verify_precompiled_hits.mjs` parses `misses=`, which
  never matched the old `tint_misses=` key.
- **Comments**: `git grep -i tint` reviewed line by line outside webgpu_notes/. Comments that described Tint as our
  converter are rewritten; pass rationales keep their Tint history in past tense ("Tint, the converter before
  SPIRV-Cross, aborted on ..."), with one note at the top of `spirv_preprocess.h` saying so. Left as is: the browser's
  Tint (validate_wgsl.mjs, webgpu_audit.js, gpu_process_trace.mjs trace names), SPIRV-Cross patch comments comparing
  with Tint, `thirdparty/amd-fsr2` (patch 0004 and the shaders it patches, history; editing one means regenerating the
  other), `spirv-tools/generated/generators.inc` (the SPIR-V generator registry), other tasks' results folders.
- **Not touched (lead-only)**: CLAUDE.md, README.md, drivers/webgpu/README.md, thirdparty/README.md (delete `## tint`),
  webgpu_site/* — line-by-line text in `for-docs.md`. TASKS text in `for-tasks.md`.
- **spirv-headers**: left as is. The broad `unified1` extraction is needed by SPIRV-Tools (DebugInfo, OpenCL,
  NonSemantic), not only by Tint; whether `spirv.hpp11` can go back to the vulkan-sdk pin was not tried.

## Verification (work/9 worktree)

- Builds, each `dev_mode=yes` (werror), `-j12`, MemoryMax=14G, all exit 0: editor 191 s; web template_release /
  template_debug x dlink no/yes x threads no/yes, 188-287 s each (ccache). Each web build re-baked: `276 compiled,
  0 glsl failures, 0 conversion failures`.
- smoke.sh last lines:
  ```
  [WGSL Precompile] Results: 276 compiled, 0 glsl failures, 0 conversion failures
  identical interface: 276/276, mismatched: 0, missing in candidate: 0, not in reference: 0
  against the stored reference: 552/552 identical, 0 differ, 0 missing, 0 not in reference
  .../wgsl: 276 files, 0 invalid
  validation: 276 modules, 0 invalid, 0 of them expected
  SMOKE OK (smoke-20261025-193751-963228)
  ```
- Before writing the references: base-tree Tint CLI vs this tree's SPIRV-Cross on the same dump: interface 276/276,
  text-scan 276/276 in both modes (0 differ) — the references equal what the old smoke compared against.
- shader_corpus 14/14; preprocessing 291 passed / 0 failed / 1 skipped (static skip, pre-existing); driver unit tests
  379/0; spec_constant_overrides 5/0 offline; wgsl_cache determinism OK (1,609,342 bytes identical); fuzzers built and
  smoke PASS x3.
- `local_ci.sh --dev-mode --no-safari` (with export, so it tested this tree's editor + templates; gpu lease held;
  Oblivion was running with 6.5 of 16 GB): **28 passed, 1 failed, 5 skipped**. Skips: Safari x2, Chrome fog smoothness
  and Forward+ matrix/parity (need WEBGPU_REAL_GPU=1). The failure is web audio end to end, **nothreads only**
  (threads all PASS): the headless Chrome page logs "WebGPU adapter not found" after "A valid external Instance
  reference no longer exists". Control: the lead's 2026-10-23 nothreads export (Tint era, untouched by this branch)
  fails identically in the same harness, so it is environmental (headless adapter, CLAUDE.md trap 43 territory), not
  this change. (First local_ci run aborted on missing node_modules in the fresh worktree; installed, rerun.)
- Export-time bake with the renamed tool: `BAKE=1 WEBGPU_BAKE_CACHE=0 forward_plus/export.sh` with this tree's editor
  produced a 35 MB `index.shaders.pck`, no "not found next to the editor" warning.

## What the lead must still run

- After merging: rebuild editor + `wgsl_convert_cli` + templates together (an editor built before this looks for
  `tint_convert_cli`; see for-tasks trap). `bin/tint_convert_cli*` in the lead's checkout are now unused (untracked,
  left alone).
- Forward+ `--parity` (task 07's final run) on the merged tree; web audio e2e nothreads headed / on a free GPU if you
  want it green.
- Fold for-docs.md / for-tasks.md.

## Risks

- CI: workflow edits (matrix key, artifact name, cache keys) were not run on GitHub; checked by reading only.
- GLSL comment edits change shader source text, so ShaderRD cache keys / the bake fingerprint move once (one re-bake).
- `webgpu_texture3d_array_inc.glsl`: pre-commit's clang-format-glsl wants to re-align an untouched macro in that file;
  I kept the original formatting (comment-only change). If CI runs pre-commit on changed files it may flag it.

## Questions for the user

1. Rename the pass `lower_tint_unsupported_ops()` (and fixture `tint_crash_inputs.frag`) to something
   converter-neutral, e.g. `lower_wgsl_unsupported_ops`? Default: **keep the names** (they are cited across TASKS,
   the docs and other branches; the comments now explain the name).
