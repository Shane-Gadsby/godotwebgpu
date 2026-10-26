# For TASKS.md (§2.35, spec constants)

- Task 14 (2026-10-25): `freeze_spec_constant_ops()` evaluates CompositeExtract, CompositeInsert (multi-index),
  VectorShuffle and component-wise vector ops on the constant composites, and SRem/SMod (previously 0). An op it
  cannot evaluate warns on stderr and becomes `OpConstantNull`, never a silent 0. run_spec_ops.sh legacy path:
  SPIRV-Cross 48/49 (op_SMod, unfrozen, by decision), Tint 47/49 (+ op_FConvert_f32, pre-existing).

## Questions for the user

None.
