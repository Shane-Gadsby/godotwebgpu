# 01: FSR2 luminance pyramid barrier uniformity

Cause: SPIR-V has `OpControlBarrier; %v = OpLoad spdCounter (Workgroup); OpControlBarrier` in SpdExitWorkgroup, whose result drives an early return before
more workgroupBarrier() calls. SPIRV-Cross emitted barrier / plain load / barrier; WGSL treats a workgroup-var load as non-uniform, so the later barriers
were in non-uniform control flow. Tint folds the triple into `workgroupUniformLoad(&spdCounter)`.

Fix (thirdparty/spirv-cross-wgsl/spirv_wgsl.cpp/.hpp, patches/0001-workgroup-uniform-load.patch): in OpControlBarrier handling, detect barrier(workgroup scope,
workgroup-memory-only semantics) + OpLoad of a non-struct, non-array, non-atomic Workgroup variable + identical barrier; skip both barriers and emit the load as
workgroupUniformLoad(&var). General construct, not file specific.

Evidence: output now `let _726 = workgroupUniformLoad(&spdCounter); return _726 != (numWorkGroups - 1u);`. Diff of all 276 wgsl/ files before/after: only the FSR2
file changed (see below). smoke_expected_invalid.txt emptied of its entry.

Note: worktree needed a symlink webgpu_tests/scene_smoketest/node_modules -> lead tree's (untracked, not committed) for the Chrome validator.
Header edits need removal of .build/*/spirv_cross and cli-spirv-cross objects (stale objects crashed with free(): invalid pointer).

Smoke last lines:
WGSL bytes: reference 6287157, candidate 6686203 (1.063x)
== Chrome validation
adapter: {"vendor":"nvidia","architecture":"lovelace","fallback":false,"features":["shader-f16","subgroups","clip-distances","dual-source-blending"]}
/tmp/claude-1000/w1/smoke-20261025-095311-4141011/wgsl: 276 files, 0 invalid
validation: 276 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261025-095311-4141011)

wgsl diff before/after:
Files /tmp/claude-1000/w1/smoke-20261025-094255-4035410//wgsl/servers_rendering_renderer_rd_shaders_effects_fsr2_fsr2_compute_luminance_pyramid_pass.glsl_no_image_atomics_comp.wgsl and /tmp/claude-1000/w1/smoke-20261025-095311-4141011//wgsl/servers_rendering_renderer_rd_shaders_effects_fsr2_fsr2_compute_luminance_pyramid_pass.glsl_no_image_atomics_comp.wgsl differ
