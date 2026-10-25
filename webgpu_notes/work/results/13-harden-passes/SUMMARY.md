# 13-harden-passes: summary

## What changed (branch work/13)

- `spirv_opt_passes::validate_module()`: SPIRV-Tools' validator, `SPV_ENV_VULKAN_1_2` (the env every pass builds its IR
  in; accepts SPIR-V 1.0-1.5, the driver asks glslang for 1.3), first error collapsed to one line.
- `spirv_preprocess::run_pipeline(bytes, std::string *r_error = nullptr)` validates its input before any pass; rejected
  -> empty module + `invalid SPIR-V, not converted: <validator message>`. Callers: `spirv_to_wgsl()` (runtime) prints it
  (ERR_PRINT) and returns nullptr, which the driver already treats as a failed stage; `tint_convert_cli` reports it as
  that module's error (exit 1 / JSON error; the export baker already leaves such a stage to the runtime);
  `fuzz_spirv_to_wgsl` returns.
- `spirv_opt_passes::check_ir_preconditions()`: one linear `spvBinaryParse` scan for what the IR loader/analyses assert
  on: ids defined once and below the bound, a bound of at most 0x3FFFFF/2 (IRContext's `TakeNextId()` returns an
  unchecked 0 past 0x3FFFFF), every id operand defined, OpEntryPoint/OpFunctionCall naming an OpFunction, blocks
  terminated. Every optimizer-based pass checks it first (`inline_opaque_functions`, `eliminate_dead_resources`,
  `eliminate_local_single_block_vars`, and `build()` for the 3 passes in spirv_opt_passes.cpp); on failure it prints
  `WebGPU: <pass>: skipped, the module is malformed: ...` and returns its input. Not a validator (the fuzzer found an
  ADCE assert it lets through); inside run_pipeline() it can only fire on a bug in an earlier raw pass.
- **OOM bounded, `rewrite_copy_logical`:** it expanded an OpCopyLogical of an N-element array into N extracts with no
  limit. A *valid* module with a 1e8-element array copy took 6.2 GB RSS and 51 s before failing; now 8 MB, 0.00 s,
  converts. Caps: at most 65532 components (the OpCompositeConstruct word-count limit, which it also overflowed), 16 MB
  of emitted words per copy and per module; past either the OpCopyLogical is kept.
- Fuzzers: `build_fuzzers.sh` compiles our three pass sources with `-fsanitize=fuzzer-no-link,address` (before, only the
  harness was instrumented: 192 coverage edges, no ASan inside the passes; now ~5,000). `fuzz_preprocess_passes` runs
  all 27 passes in run_pipeline()'s order (it ran 16), the optimizer-based ones only on validator-accepted modules.
- `webgpu_tests/preprocessing_tests/run_tests.mjs`: four hand-built modules were invalid SPIR-V, fixed: TerminateInvocation
  gets its `SPV_KHR_terminate_invocation` OpExtension; the push-constant module is SPIR-V 1.3 (StorageBuffer is core
  there); 18a is SDiv (UDiv needs an unsigned result type); 1k (OpSpecConstantOp of OpCopyLogical, not allowed in a
  spec op) now asserts a clean rejection naming it.

## Reproducers, before -> after (bin/tint_convert_cli, SPIRV-Cross build)

| reproducer | before | after |
|---|---|---|
| 06 `adce_missing_function.spv` | abort: ProcessCallTreeFromRoots "function does not exist" | rejected: `Invalid instruction word count: 0 (word 5)` |
| 06 `cfg.spv` | abort: CFG::RegisterBlock "must have a terminator" | rejected: `Invalid instruction word count: 0 (word 81)` |
| 06 `def_use_manager.spv` | abort: DefUseManager "Definition is not registered" | rejected: `ID '31[%31]' has not been defined ...` |
| 06 `inline_opaque_missing_function.spv` | abort: ProcessCallTreeFromRoots | rejected: `Result <id> '2147483652' must be less than the ID bound '35'` |
| 13 `id_bound_overflow.spv` (found here) | abort: DefUse assert via expand_binding_arrays (result id 0) | rejected (bound > 4194303); standalone pass skips (bound over 2097151) |
| 13 `adce_load_non_pointer.spv` (found here) | abort: AggressiveDCE "input is not a pointer" | rejected: `OpLoad Pointer <id> '54[%54]' is not a logical pointer` |
| 13 `copy_logical_huge_array.spv` (valid; .spvasm beside it) | 6.2 GB, 51 s, exit 1 | converts, 8 MB, 0.00 s |

All also run through each fuzzer binary without a crash report.

## Fuzzing (libFuzzer + ASan, -rss_limit_mb=2048; seeds: 14 fixtures, 176 engine variants under 24 KB, reproducers)

Final round on the final code, 10 minutes each, **0 crash/OOM/leak artifacts**:

| fuzzer | execs | cov | peak RSS |
|---|---|---|---|
| fuzz_preprocess_passes | 144,277 | 5,148 | 1,729 MB |
| fuzz_spirv_to_wgsl | 298,419 | 3,996 | 1,028 MB |
| fuzz_split_samplers | 11,948,548 | 1,313 | 551 MB |

Before it: 90 s and 120 s probes (found the two new reproducers), a 180 s probe, and a clean first 10-minute round
(before the copy-logical cap): ~33 minutes per fuzzer in all. fuzz_preprocess_passes' RSS climbs slowly (corpus + ASan
quarantine, no leak reported); a much longer run may want `-rss_limit_mb=4096`.

## Cost on the 276-variant corpus

- Validation alone: 0.63 s over the corpus (2.3 ms/module average, 26 ms on the 288 KB Forward+ uber shader).
  `check_ir_preconditions()`: 0.05 s per call over the corpus, 3-6 calls per module.
- In-process `run_pipeline()` (avg of 3 passes; other workers busy): 3.90 -> 4.93 s and 4.83 -> 5.96 s, about +1.0 s
  (+21-26%). Backend unchanged (~0.6 s).
- `tint_convert_cli`, one process per module: 5.71 -> 6.61 s (quiet), 6.52 -> 7.46 s (busy): +15%.
- WGSL byte-identical to before for all 276 variants and the 14 fixtures.
- Kept on every path: ~3 ms per stage, baked exports convert nothing at run time, and the wasm runtime (in-process,
  NDEBUG, so UB rather than an abort) is what it protects most. Skipping it in the subprocess-isolated
  `tint_convert_cli` is the option if bake cost matters (for-tasks.md).

## Other tiers (final converter)

preprocessing_tests 291 / 0 failed / 1 skipped (as before); shader_corpus 14/14; spec_constant_overrides offline 5/5;
driver_unit_tests 379/379. `spirv_to_wgsl.cpp` syntax-checked against the engine headers; no SCons build run.
pre-commit clean on every changed file.

## smoke.sh (last lines)

```
identical interface: 276/276, mismatched: 0, missing in candidate: 0
WGSL bytes: reference 6287157, candidate 6681762 (1.063x)
== Chrome validation
adapter: {"vendor":"nvidia","architecture":"lovelace","fallback":false,"features":["shader-f16","subgroups","clip-distances","dual-source-blending"]}
.../smoke2/smoke-20261025-125352-1820071/wgsl: 276 files, 0 invalid
validation: 276 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261025-125352-1820071)
```

## For the lead / risks

- A web template build (build lease) is the real compile check for `spirv_to_wgsl.cpp` and the `libspirv.hpp` include
  in `spirv_opt_passes.cpp` (validator sources are already in drivers/webgpu/SCsub).
- SPIR-V the validator rejects but a backend used to accept is now refused. All 276 engine variants and 14 fixtures
  validate. One eval probe does not (`op_FConvert_f32`, for-tasks.md); `run_spec_ops.sh` (gpu) not run.
