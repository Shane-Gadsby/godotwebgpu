# 16-baked-fog-race: volumetric fog in baked exports lands in one of two states per run

State: ready-for-integration
Owner: worker-16
Claimed: 2026-10-25 12:15
Needs: gpu
Branch: work/16
Results: webgpu_notes/work/results/16-baked-fog-race/

## Context

Task 07 (results/07-forward-plus-parity/SUMMARY.md): in a baked export, `run_forward_plus.mjs --baked --parity --feature
volumetric_fog` gives exactly 0.046 from native (pass) or exactly 0.082 (fail), per run, with identical shader stats.
It happens with Tint-baked exports from before the swap (3 of 9) and with SPIRV-Cross-baked ones (8 of 11); unbaked
SPIRV-Cross exports passed 5 of 5. A 600-frame settle does not change it. Two exact states chosen at load point at
ordering, not arithmetic: a pipeline or resource created in a different order on the baked path (no runtime
conversion delay), a fog history/density texture used before it is initialized, a read-write storage texture bound
twice in one dispatch, or an async pipeline (`createComputePipelineAsync` prewarm, TASKS.md §2.4) that is or is not
ready on the first fog frame and leaves the history in a different state.

## Goal

Find why the baked path has two outcomes and make it deterministic and at parity (0.046). Fog's shaders are
`servers/rendering/renderer_rd/shaders/environment/volumetric_fog*.glsl`; the baked fog modules (6 compute) are in the
export's `index.shaders.pck` as plain WGSL. Compare a passing and a failing run's console with `--verbose` in the
export's GODOT_CONFIG args, and the order of fog pipeline creation and first dispatch.

## Verification

`--baked --parity --feature volumetric_fog` passes 10 of 10 runs, with SPIRV-Cross-baked and Tint-baked exports alike;
full matrix 71/71 baked.
