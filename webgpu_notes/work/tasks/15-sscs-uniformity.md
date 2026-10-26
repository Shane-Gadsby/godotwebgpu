# 15-sscs-uniformity: screen-space contact shadows fail WGSL barrier uniformity under SPIRV-Cross

State: integrated (lead, 2026-10-25: patch 0015; smoke 276/276 valid; real-project corpus 2566/2566 valid and interface-identical; render check in task 07)
Owner: lead
Claimed:
Needs: — (Chrome compile; gpu lease only to see contact shadows render)
Branch: work/15
Results: webgpu_notes/work/results/15-sscs-uniformity/

## Context

Found by task 05 in every project's bake (results/05-project-corpus, reproducers in `repro/`): the 4
`screen_space_contact_shadows` compute variants are rejected by Chrome, "workgroupBarrier must only be called from uniform
control flow". SPIRV-Cross copies compute builtins (`gl_WorkGroupID`, `gl_NumWorkGroups`, ...) into module-scope
`var<private>` in its entry-point wrapper, and WGSL's uniformity analysis treats any `var<private>` read as non-uniform,
so an early `return` derived from the workgroup id that precedes a barrier fails. Tint passes builtins as entry-point
parameters, which the analysis knows are uniform (`workgroup_id`, `num_workgroups`). Barrier uniformity has no diagnostic
switch. Contact shadows are a Forward+ parity feature, so until this is fixed every export ships those stages unbaked
(runtime fallback, which converts with the same backend and fails the same way).

## Goal

A SPIRV-Cross patch (never upstreamed; next free number 0015) that keeps the uniform builtins uniform: e.g. read
`workgroup_id` / `num_workgroups` (and `local_invocation_index`/`global_invocation_id` stay private, they are non-uniform
anyway) from entry-point parameters passed into the functions that use them, or emit them as `let` values in the entry
point that helpers receive as arguments. General form, not keyed on the shader. Check whether task 01's FSR2 case and any
other compute shader change.

## Verification

`smoke.sh` OK; both reproducers valid in Chrome; task 05's harness (`project_eval.py`) over the stored corpus shows
2566/2566 valid; contact shadows render in the lead's parity run.
