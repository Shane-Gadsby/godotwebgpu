# 05-project-corpus: SUMMARY

Harness (new, `webgpu_tests/spirv_cross_eval/`): `project_export.sh <name> <project> <scratch>` copies the project, imports
it, exports it with the lead's editor + nothreads release template under xvfb (Vulkan, for baking) in a
`systemd-run` scope (MemoryMax=8G), with `WEBGPU_BAKE_CACHE=0` and a copy of the editor whose neighboring
`tint_convert_cli` is a wrapper that saves each `.spv` it is handed, then runs the real converter. `project_eval.py` converts
the captured stages with both backends (`--batch`); `compare_interface.py` and `validate_wgsl.mjs` do the rest.
The editor exports stages through the real baker, so what is captured is exactly what an export bakes (stages declined by
`_wgsl_unsupported_reason()` never reach the converter and are not in the corpus).

## Per-project table (Tint vs SPIRV-Cross, after patch 0005)

| project | stages | converted Tint | converted SX | interface identical | Chrome-valid SX |
|---|---|---|---|---|---|
| fp_check | 882 | 882 | 882 | 882/882 | union of all four: 2566 unique, 2562 valid |
| sewer-shift (`/mnt/109313D2109313D2/godot-game-dev/sewer-shift`) | 1224 | 1224 | 1224 | 1224/1224 | " |
| cameraSim `.../testing` | 1318 | 1318 | 1318 | 1318/1318 | " |
| scene_smoketest projects (22 scenes, 20 distinct projects; benchmark a-h, 7 demo projects, font/fog/audio/braced fixtures) | 1936 unique | 1936 | 1936 | 1936/1936 | " |
| metahuman | not run: no export on disk (see for-tasks.md) | | | | |

Output size SX/Tint: 1.09-1.14x.  Before patch 0005 each export logged 2 `leaving one shader stage unbaked` warnings
(SPIRV-Cross only; Tint converted both).

## Triage
1. FIXED, patch 0005: CanvasSdf compute (2 stages in every project) used `r16snorm` storage image;
   `image_format_to_wgsl` threw. Added R16/Rg16/Rgba16 (+Snorm) -> r16unorm...rgba16snorm. These validate in Chrome.
2. OPEN, proposed task: 4 stages of `screen_space_contact_shadows` (in every project; not in the 276 engine corpus)
   are invalid in Chrome: `workgroupBarrier` in non-uniform control flow, because builtins are `var<private>` and WGSL
   uniformity analysis treats them as non-uniform (Tint passes them as entry parameters and validates). Tried
   `diagnostic(off, derivative_uniformity)`: does not cover barriers; reverted. Reproducers:
   `repro/sscs_barrier_*.spv` (two of the four; the other two are the same shader's variants; all in scratch under
   `corp/fp_check/spv`). Hash names are SHA-256 of the SPIR-V as the baker keys it.
3. No SPIRV-Cross-only conversion failures and no interface differences remain.

Scratch (not committed): `/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/06ad18c7-2f4b-4e67-91c1-7151308515f2/scratchpad/corp/`
(`<project>/spv`, `eval/{sx,tint}`, `UNION/validation.json`). The lead should re-run `project_export.sh` after
integration to refresh the corpus (about 20 s to 1 min per project).

## smoke.sh (last lines)
```
identical interface: 276/276, mismatched: 0, missing in candidate: 0
validation: 276 modules, 1 invalid, 1 of them expected
SMOKE OK
```
