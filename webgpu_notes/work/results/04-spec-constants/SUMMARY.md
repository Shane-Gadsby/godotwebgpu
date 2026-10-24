# 04-spec-constants: result

**Outcome: `spec_constants_overridable()` is unchanged on purpose.** SPIRV-Cross lowers every OpSpecConstantOp kind the
rules accept, so the override path needs no change; widening has no engine benefit (no engine shader is frozen) and
two of the shapes it could widen to are unsafe. No SPIRV-Cross patch was needed (no 0004).

## Engine shaders per path (before = after, no rule changed)

`engine_spec_paths.txt` lists every one of the 276 variants `wgsl_precompile.py` builds (smoke's raw/ dir), from the new
`tint_convert_cli --spec-info`: **183 no spec constants, 93 overridable (216 overrides), 0 frozen.** So the legacy
re-patch + re-convert path (`_create_module_with_spec_constants`) is not taken by any engine shader, with Tint or
SPIRV-Cross; item 3 reduces to probe modules (below). Caveat: the corpus is what the precompiler builds; a user shader
with a frozen shape still takes the legacy path.

## Support map (44 probe modules, `webgpu_tests/spirv_cross_eval/run_spec_ops.sh`)

Each module is converted with `tint_convert_cli --raw` (no preprocessing, so the backend's own lowering is tested),
built in Chrome with default and with alternate pipeline constants, and the value it writes is compared to Python's.

| shape | rule today | SPIRV-Cross (override path) |
|---|---|---|
| 36 scalar OpSpecConstantOp kinds in the Tint list (int/bool arithmetic, shifts, compares, logical, Select, mixed-sign IAdd, FConvert f32) | overridable | all correct, except SMod with negative operands (below) |
| SConvert/UConvert/VectorShuffle/CompositeInsert as scalar ops | frozen | not reachable from GLSL (no 8/16/64-bit spec ints in WGSL) |
| spec composite, only consumed by CompositeExtract/Insert | frozen | converts and is correct (inline override expressions) |
| spec composite shuffled, or used by runtime code (glslang's `vec3(F,..)`) | frozen | **no conversion: "Maximum compilation loops detected"** |
| workgroup array sized by a spec constant | frozen | valid, correct |
| function-scope array sized by a spec constant | frozen | **invalid WGSL** ("override-sized arrays can only be used in the workgroup address space") |
| `@workgroup_size` from spec constants | frozen | valid, correct (still needs driver plumbing and limits checks) |

SMod: SPIRV-Cross emits WGSL `%` (truncated), SPIR-V SMod takes the divisor's sign. GLSL `%` is undefined for
negative operands and glslang emits SMod for it, so it is a note, not a defect worth a patch.

**Why not widen.** Frozen modules are zero of 276, so widening saves no re-convert for the engine. The two cheap
candidates (workgroup arrays, workgroup size) are valid WGSL but the driver/passes treat array lengths and
`LocalSize` as literals (`spec_constants_overridable()` comments), and the other shapes do not convert or are invalid.
Keep the rules; their comments still say "Tint" as the reason, but they now also match what SPIRV-Cross can do.

## Legacy path (frozen modules) with SPIRV-Cross

Patched defaults + full `run_pipeline()` + SPIRV-Cross, built with no constants: all 44 convert; 40 give the right value.
The 3 composite probes (CompositeExtract/Insert/VectorShuffle of a spec composite) give 0: `freeze_spec_constant_ops()`
evaluates them to 0 whatever the backend (Tint gave the same, and fails validation on two), GLSL cannot produce them.
Not fixed (out of scope, backend independent). The legacy path's WGSL text scans (`_create_module_with_spec_constants`:
`read_write>` storage texture splitting) were not exercised, nothing engine-side takes them.

## Tests

- `webgpu_tests/spec_constant_overrides/run_tests.mjs`: **8 passed, 0 failed** (5 offline + 3 browser, nvidia/lovelace,
  headed system Chrome, `gpu` lease taken). It already ran against `bin/tint_convert_cli` (now SPIRV-Cross); the only fix was
  that its playwright import failed in a worktree (no node_modules): it now falls back to scene_smoketest's install.
- `run_spec_ops.sh`: override path 40 pass / 4 expected failures (SMod, vector_shuffle, composite_use_runtime,
  local_array); legacy path 40 pass / 4 expected (SMod, 3 composites). Listed failures that start passing fail the run.
- New tool surface (`drivers/webgpu/tint_cli/main.cpp`): `--spec-info <spv>...` (none/overridable/frozen per file) and
  `--raw <spv>` (convert without run_pipeline, for probes).

## smoke.sh (final run on this branch)

```
identical interface: 276/276, mismatched: 0, missing in candidate: 0
WGSL bytes: reference 6287157, candidate 6686228 (1.063x)
== Chrome validation
adapter: {"vendor":"nvidia","architecture":"lovelace","fallback":false,"features":["shader-f16","subgroups","clip-distances","dual-source-blending"]}
/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/06ad18c7-2f4b-4e67-91c1-7151308515f2/scratchpad/smoke/smoke-20261025-095714-27104/wgsl: 276 files, 1 invalid
validation: 276 modules, 1 invalid, 1 of them expected
SMOKE OK (smoke-20261025-095714-27104)
```

## Lead must still run / risks

- Nothing merge-blocking. `webgpu_tests/scene_smoketest/node_modules` is a symlink I made in my worktree to the lead's
  install; it is untracked and not committed.
- GPU was shared (9.8 GB used by another process); the tests only build tiny compute pipelines.
