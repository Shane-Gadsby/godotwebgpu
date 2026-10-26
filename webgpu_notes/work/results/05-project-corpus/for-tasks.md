# For TASKS.md / lead

## Questions for the user
- MetaHuman: no export ships in the repo and none was found on disk, so `webgpu_tests/metahuman` stages were not part of
  the corpus (its hair/skin materials are the likeliest remaining unknown shaders). Options: (a) point me at a
  MetaHuman export zip and a follow-up worker runs `project_export.sh` on the built project; (b) skip. Recommended: (a)
  when convenient; nothing else is blocked.
- sewer-shift was found (`/mnt/109313D2109313D2/godot-game-dev/sewer-shift`) and is in the corpus.

## Proposed task (not small)
screen_space_contact_shadows (4 SPIR-V variants) fails Chrome validation under SPIRV-Cross: `workgroupBarrier must only be
called from uniform control flow`. Cause: the backend turns builtins into `var<private>` (gl_WorkGroupID...), which WGSL's
uniformity analysis treats as non-uniform, so the early `return` guarded by a value derived from it precedes the barrier.
Tint passes builtins as entry-point parameters and so sees them as uniform. Fix idea: pass uniform builtins
(workgroup_id, num_workgroups) as parameters into the functions that read them, or keep them as entry-point locals
captured by the helpers. Reproducers: `repro/`.
