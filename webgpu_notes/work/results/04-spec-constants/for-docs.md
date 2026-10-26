For TASKS.md §2.35 / CLAUDE.md "Specialization constants" / drivers/webgpu/README.md:

- The rules in `spec_constants_overridable()` stay as they are under SPIRV-Cross. Of the 276 precompiled engine variants,
  93 keep their spec constants as overrides and none is frozen, so the legacy re-convert path is not used by the engine.
- What SPIRV-Cross can lower (probe table in results/04-spec-constants/SUMMARY.md): every scalar OpSpecConstantOp kind
  Tint could; workgroup arrays and `@workgroup_size` from spec constants (valid but left frozen); composites only inline when
  consumed by CompositeExtract/Insert. It cannot: a spec composite used by runtime code or shuffled (no conversion), a
  function-scope array sized by a spec constant (invalid WGSL). SMod uses WGSL's truncated `%` (GLSL leaves negatives undefined).
- Known, backend independent: `freeze_spec_constant_ops()` folds CompositeExtract/Insert/VectorShuffle on a spec composite to 0.
- New tools: `tint_convert_cli --spec-info` (which path a module takes) and `--raw`; `webgpu_tests/spirv_cross_eval/run_spec_ops.sh`
  (needs the gpu lease). The spec_constant_overrides README still says "Tint" in places.
