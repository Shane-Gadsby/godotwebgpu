# For the docs (task 13)

## CLAUDE.md, "Shader pipeline" (after the run_pipeline() sentence)

`run_pipeline()` validates its input with SPIRV-Tools' validator (`SPV_ENV_VULKAN_1_2`) before any pass runs and
returns an empty module plus a message for anything it rejects (`invalid SPIR-V, not converted: ...`); every caller
reports that as a failed conversion. SPIRV-Tools' optimizer *asserts* on malformed modules (or, with `NDEBUG` in release
templates, reads through null pointers), so a pass that builds its IR must never see unvalidated words. Hand-built test
SPIR-V must be valid too (`spirv-val --target-env vulkan1.2`), or the test now sees a rejection.

## drivers/webgpu/README.md (preprocessing section)

- Input gate: `spirv_opt_passes::validate_module()` in `run_pipeline()`, ~2.3 ms per module on average (26 ms on the
  288 KB Forward+ uber shader), +16% on the whole-corpus CLI conversion.
- Every pass that runs SPIRV-Tools' optimizer (`inline_opaque_functions`, `eliminate_dead_resources`,
  `eliminate_local_single_block_vars`, and the three in `spirv_opt_passes.cpp` through `build()`) first runs
  `check_ir_preconditions()`: one linear scan for what the IR loader asserts on (ids defined once and under a bound
  with headroom, every id operand defined, calls and entry points naming functions, every block terminated). It is not
  a validator: inside `run_pipeline()` it only fires when an earlier raw pass broke the module, and it then prints
  `WebGPU: <pass>: skipped, the module is malformed: ...` and skips that pass.
- `rewrite_copy_logical` caps its output: an `OpCopyLogical` of an array longer than an `OpCompositeConstruct` can hold
  (65532), or a copy past 16 MB of emitted words, keeps the `OpCopyLogical`. Before, a valid 1e8-element copy made the
  converter allocate 6.2 GB.

## drivers/webgpu/tint_cli/fuzz (README or build_fuzzers.sh header, if a README is ever written)

The fuzzers now compile our own pass sources with `-fsanitize=fuzzer-no-link,address`. Before, only the harness file
was instrumented: libFuzzer saw ~200 coverage edges instead of ~5,000 and ASan never looked inside the passes.
`fuzz_preprocess_passes` runs every pass in `run_pipeline()`'s order; the optimizer-based ones only on modules the
validator accepts. Run them from a scratch directory with `-close_fd_mask=2` (the skip messages are per input).

## thirdparty/README.md

Nothing: no SPIRV-Tools or SPIRV-Cross patch.
