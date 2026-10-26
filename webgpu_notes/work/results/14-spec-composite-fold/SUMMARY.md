# 14-spec-composite-fold: result

**Outcome:** `freeze_spec_constant_ops()` now computes composite and vector spec-constant ops on the constant values,
so a frozen module computes what Vulkan does. Before the fix it folded every one of them to 0. The change is in
`drivers/webgpu/spirv_preprocess.cpp` only; nothing is backend specific.

## The fix

- A new `SpecFreezeState` records types (bool/int/float widths, vector/matrix/array element types, struct members),
  scalar values by id, and the constituent ids of every `OpConstantComposite` / `OpSpecConstantComposite` and of
  every composite result it builds itself.
- **CompositeExtract** walks the index chain and re-emits the constituent under the result id (a scalar `OpConstant`,
  or an `OpConstantComposite` for a sub-composite). The old code read the literal indices as ids.
- **CompositeInsert** copies the composite with the object swapped in. Each enclosing composite that changes gets a
  fresh id (multi-index insert) and the header bound is raised.
- **VectorShuffle** builds the `OpConstantComposite` from both vectors' constituents (0xFFFFFFFF picks any component).
- **Vector-typed ops** (IAdd etc. on `ivec`, Select with a bool-vector or scalar condition) run component by
  component, each component as a fresh `OpConstant`.
- `eval_spec_op()` gained **SRem and SMod**, which used to return 0 (sign of the dividend, sign of the divisor;
  INT_MIN/-1 is guarded). Same-width UConvert/SConvert/FConvert are now explicit. Results are masked to the type's
  width, so `IEqual(A+B, 0)` sees the 32-bit wrap. It returns false for an op it does not know. The freezer then
  prints a one-line `fprintf(stderr)` warning and emits `OpConstantNull` (valid, of the right shape) rather than
  silently writing a 0. The dropped Kernel-only ConvertF/S/U cases fall under this too.
- `OP_CONSTANT_NULL` moved from a function-local constant in `hoist_constant_local_arrays` to namespace scope (it
  would otherwise shadow the new one under `-Wshadow`).

## Probes (`webgpu_tests/spirv_cross_eval/run_spec_ops.sh`, headed system Chrome, nvidia lovelace, `gpu` lease)

Five probes were added for the new paths: `vector_iadd`, `vector_select`, `nested_insert` (two-level insert into an
array of vectors, with the untouched element read back), and `frozen_SRem` / `frozen_SMod` (scalar ops in a module
that a spec composite forces onto the frozen path). That makes 49 modules. The script now takes `CLI=` for the Tint
reference, and `OVERRIDE_EXPECT_FAIL` / `LEGACY_EXPECT_FAIL` for that backend's lists, which are given in its comments.

| run | override path (`--raw`) | legacy (frozen) path |
|---|---|---|
| SPIRV-Cross before | 43 pass / 6 | **40 pass / 9**: composite_extract, vector_shuffle, composite_insert, vector_iadd, vector_select, nested_insert, frozen_SRem, frozen_SMod all give 0; op_SMod |
| SPIRV-Cross after | 43 pass / 6 (unchanged: backend lowering) | **48 pass / 1** (op_SMod only, see below) |
| Tint before | 38 pass / 11 | 39 pass / 10: composite_extract and frozen_SRem/SMod give 0, the other 5 composites NOCONV, op_SMod, op_FConvert_f32 |
| Tint after | 38 pass / 11 (unchanged) | **47 pass / 2** (op_SMod, op_FConvert_f32: both pre-existing, both on the unfrozen override path) |

On the legacy path, op_SMod is overridable, so it reaches the backend unfrozen and fails on the truncated `%` noted in
task 04 (unchanged, by decision). The override path's own list gains frozen_SMod (the same SMod) and nested_insert
(SPIRV-Cross loops, like vector_shuffle). The official script exits 0 on both backends with those lists. Tint's
"Illegal instruction" crashes on `--raw` of the composite probes are Tint ICEs in the override path, pre-existing.

## Tests

- `webgpu_tests/spec_constant_overrides/run_tests.mjs`: **8 passed, 0 failed**.
- `preprocessing_tests`: 291 passed, 0 failed, 1 skipped (the skip is pre-existing). `driver_unit_tests`: 379/379.
  `shader_corpus`: 14/14.
- Frozen probe SPIR-V after `run_pipeline()` (`TINT_DEBUG_DUMP_PREPROCESSED`) passes `spirv-val --target-env vulkan1.1`.
- Web template compile check (`build` lease, capped scope, -j12): `scons platform=web target=template_release
  dlink_enabled=no webgpu=yes opengl3=no threads=no` built in 1:22, with no warnings or errors in spirv_preprocess.
- `pre-commit` is clean on the changed files.

## smoke.sh

```
identical interface: 276/276, mismatched: 0, missing in candidate: 0
WGSL bytes: reference 6287157, candidate 6681762 (1.063x)
== Chrome validation
adapter: {"vendor":"nvidia","architecture":"lovelace","fallback":false,"features":["shader-f16","subgroups","clip-distances","dual-source-blending"]}
.../smoke/smoke-20261025-121351-1663060/wgsl: 276 files, 0 invalid
validation: 276 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261025-121351-1663060)
```

## Lead must still run / risks

- Nothing merge-blocking. No engine shader is frozen (task 04: 0 of 276), so engine output is unchanged. smoke's
  interface comparison is identical.
- Composite `OpConstantNull` operands are still unknown to the freezer. An op that reads one hits the warning plus
  `OpConstantNull` fallback; it is not silent.
- `webgpu_tests/scene_smoketest/node_modules` in this worktree is an untracked symlink to the lead's install (not
  committed).
