# 25-metahuman-skin-coarse: why MetaHuman skin looks coarse, with huge pores and follicles

State: ready-for-integration
Owner: worker-25
Claimed: 2026-10-26 15:50
Needs: gpu (captures), build only if a fix needs an engine rebuild
Branch: work/25
Results: webgpu_notes/work/results/25-metahuman-skin-coarse/

## Context

User report, 2026-10-26: "MetaHumans have a very coarse looking skin texture, with pore/follicle locations being huge".
The importer is `modules/metahuman/` (TASKS.md §2.33; CLAUDE.md "MetaHuman importer"). The skin material is built in
`editor/editor_scene_importer_metahuman.cpp` (`_material()`, types `head` / `body`) with the `SKIN` shader in
`editor/metahuman_shaders.h`. The user's real MetaHuman is Ava in the cameraSim project
(`~/Downloads/cameraSim_2026-09-09_10-26-06/testing`, main scene `uid://etk7cti113sw`); **never modify the user's
project**, work on a scratch copy (`webgpu_tests/screenshot_comparison/LIVE_REPRO_METHODOLOGY.md`). Test tier:
`webgpu_tests/metahuman/run.sh`.

Unverified lead from a glance (check it, don't assume it): the skin shader samples `detail_normal_map` at a fixed
`detail_normal_tiling = 16.0` with strength 0.5 and no distance fade, and the importer never sets either from the
export. Whether that texture is a tiling micro-normal at all, which UV set and tiling Unreal's MetaHuman skin material
uses for it, and whether the main normal / base color are the right resolution and LOD are all open. Other suspects:
texture import settings (mipmaps, VRAM compression, size limit), the anisotropic/mip filtering on web vs native,
`SamplerLodBias`, the SSS / roughness maps.

## Goal

1. Find the cause, with evidence: close-ups of the face at a few distances, native Vulkan and WebGPU, and the same with
   each suspect toggled (detail normal off, tiling changed, full-res textures). Say whether it is web-only or also native.
2. Fix it in the importer / shader if the cause is ours, taking values from the MetaHuman export (its manifest or
   Unreal's material defaults) rather than tuning by eye, and fade the micro detail with distance if Unreal does.
3. Before/after frame pairs in `results/25-metahuman-skin-coarse/looks/` for the user to judge on the hand-offs page.

## Verification

- `webgpu_tests/metahuman/run.sh <export> --shots` still passes; native and WebGPU close-ups before and after.
- `webgpu_notes/work/smoke.sh` passes if any engine code changed.
