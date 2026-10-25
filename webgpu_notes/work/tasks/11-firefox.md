# 11-firefox: Firefox (desktop) on SPIRV-Cross WGSL, works and is as fast as Tint

State: claimed
Owner: worker-11
Claimed: 2026-10-25 14:20
Needs: gpu
Branch: work/11
Results: webgpu_notes/work/results/11-firefox/

## Context

User bar (2026-10-25): Firefox must work, ideally as fast as or faster than with Tint. Firefox compiles WGSL with naga,
not Tint, so WGSL that Chrome accepts can still be rejected or lowered differently there. TASKS.md §4.13 and memory
"Firefox WebGPU blockers" cover how Firefox runs here; `run_forward_plus.mjs` has a Firefox mode (`browserName`).

## Goal

The Forward+ matrix and the scene smoketest in Firefox with SPIRV-Cross-baked exports, against the same exports baked
by `bin/tint_convert_cli.tint-pre-swap`: every module compiles in naga (collect `getCompilationInfo` errors), the same
features pass, and frame time (uncapped where Firefox allows, `api_profile.mjs`/`gpu_time.mjs` style) is no worse.
Fix what differs (SPIRV-Cross patch or pass; never upstreamed).

## Verification

Table in SUMMARY: feature / Tint / SPIRV-Cross, and frame times with spread, three runs each.
