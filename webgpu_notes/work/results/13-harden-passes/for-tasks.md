# For TASKS.md (task 13)

- §2.35 / §4: malformed SPIR-V is now rejected by `run_pipeline()` (SPIRV-Tools validator on the input) instead of
  asserting inside SPIRV-Tools' optimizer. Task 06's four reproducers and the three found here (results/13-harden-passes/
  fuzz_repro/) all end in `invalid SPIR-V, not converted: <validator message>` or convert cleanly; none aborts.
- Real bug found on *valid* input: `rewrite_copy_logical` expanded an `OpCopyLogical` of an N-element array into N
  extracts with no limit, so a valid module with a 1e8-element array copy took 6.2 GB and 51 s. Now capped
  (`fuzz_repro/copy_logical_huge_array.spvasm`). This is the "libFuzzer OOM" class from task 06.
- Fuzzers were mostly blind before: `build_fuzzers.sh` linked the plain -O2 pass objects from `build.sh`, so libFuzzer
  had coverage only from the harness file (192 edges) and ASan never checked the passes. Now instrumented (~5,000 edges
  in fuzz_preprocess_passes). `fuzz_preprocess_passes` also covered only 16 of the 27 passes; it now runs all of them.

## §3 trap candidate (cost about an hour)

**SPIRV-Tools' optimizer asserts on invalid SPIR-V; a structural pre-check is not enough.** It looks like ids,
functions and block terminators are all the IR loader needs, and a scan for those stops the three task-06 asserts. The
fuzzer then found AggressiveDCE asserting on an `OpLoad` from a non-pointer, and `expand_binding_arrays` handing out
result id 0 once the id bound is near IRContext's 0x3FFFFF limit (`TakeNextId()` returns 0, unchecked). Only the
validator rules out the type errors; validate untrusted input, use the cheap check only against our own passes' bugs.

## Follow-ups (not done here)

- `webgpu_tests/spirv_cross_eval/spec_op_probes.py`'s `op_FConvert_f32` probe is invalid SPIR-V (FConvert to the same
  width; the validator says "Expected component type of Value to be different"). Its `--raw` path is unchanged, but its
  legacy path (`run_pipeline()`) is now rejected. If `run_spec_ops.sh` (gpu lease) reports it as a new failure, drop the
  probe or list it. Not run here (needs the GPU).
- Cost option if +16% on conversion matters: the validation could be skipped in `tint_convert_cli` (build-time
  precompile and export bake), which already runs in a subprocess the baker survives; the runtime (wasm, in-process,
  no `assert`, so UB instead of an abort) is where it protects most. Not done: one gate for every caller is simpler,
  and baked exports convert nothing at run time.
- Raw word passes allocate ids past the header bound freely (r_next_id++) and the validator caps the bound at 0x3FFFFF;
  `check_ir_preconditions()` refuses bounds over half that so an optimizer pass always has ids left. Fine for every
  real shader (the largest engine variant's bound is 10,854); noted in case a pass ever adds ids per array element.

## Questions for the user

None.
