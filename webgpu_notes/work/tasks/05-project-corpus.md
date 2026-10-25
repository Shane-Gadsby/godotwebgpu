# 05-project-corpus: real projects' shaders (materials, user shaders) through SPIRV-Cross

State: integrated (merged 5ec31c3aa4; corpus re-run after patch 0015: 2566/2566 valid, interface identical)
Owner: worker-05
Claimed: 2026-10-25
Needs: — (the existing editor bin/godot.linuxbsd.editor.x86_64 + your tint_convert_cli; Chrome compile, no lease)
Branch: work/5
Results: webgpu_notes/work/results/05-project-corpus/

## Context

The engine corpus (276 variants) has no material or user shaders: those are compiled per project and baked at export
(`ShaderBakerExportPlugin`, `tint_convert_cli --batch`, TASKS.md §4.23). The user's real test project
(`~/Downloads/cameraSim_*/testing`, memory "User's test project location") bakes ~924 unique stages; `sewer-shift`
(find it; TASKS.md §4.27) had 28 stages Tint could not convert. The fixtures `webgpu_tests/forward_plus/godot/fp_check`,
`webgpu_tests/scene_smoketest` scenes and `webgpu_tests/metahuman` also bake material shaders.

## Goal

1. A repeatable harness (in `webgpu_tests/spirv_cross_eval/`): export a project with the editor using a given
   `tint_convert_cli` (the editor runs the one next to its own executable: copy the editor binary into your worktree's
   `bin/` beside your converter rather than replacing anything in the lead's `bin/`), capture every stage's SPIR-V and
   WGSL (`WEBGPU_BAKE_CACHE_DIR` to a scratch dir; see `wgsl_bake_subprocess.cpp`), Chrome-validate every WGSL, and
   compare the interface with Tint's (`bin/tint_convert_cli.tint`).
2. Run it on cameraSim, sewer-shift (if found), fp_check, the smoketest project and metahuman.
3. Triage every conversion failure, invalid module and interface difference: fix in the passes or as a SPIRV-Cross
   patch if small and clear; otherwise write it up in results with a reproducer `.spv` and propose a task.

## Verification

Per-project table in SUMMARY.md: stages, converted, valid, interface-identical — Tint vs SPIRV-Cross. Zero
SPIRV-Cross-only failures, or each one has a proposed task.
