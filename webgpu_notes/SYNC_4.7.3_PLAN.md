# Plan: merge upstream `4.7` (4.7.3 pre-release) into `webgpu-4.7.2`

Status: NOT STARTED. Written 2026-09-21. Only upstream was fetched and diffs read; nothing was merged.

## What the merge contains

- `upstream/4.7` is 28 commits ahead of `4.7.2-stable` (already in our history). Whole delta: 54 files, +435/-157.
- Most of it is editor, platform or physics fixes we don't touch: FileDialog, TextEdit, Windows input/IME, Wayland clipboard, Jolt, FBX, Dictionary script fix, the `4.7-disable-physics-nav-3d` merge.
- Rendering-relevant commits (the ones to check):
  - `b21f5197f9` "Remove AreaLight3D samplers to reduce sampler count": touches `area_lights_inc.glsl`, `scene_forward_lights_inc.glsl`, both `*_inc.glsl` scene headers (forward_clustered, forward_mobile) and both forward renderers' `.cpp`.
  - `2fc2e56002` "Fix SSR inverted reflections in VR": touches `screen_space_reflection.glsl`.
- Our exposure: `scene_forward_lights_inc.glsl` contains the LTC area-light helper that triggered Task 8.2 (the Tint texture-parameter bug worked around by the 13th SPIR-V preprocessing pass).

## Steps

1. **Set up.** Branch `sync/4.7.3` off `webgpu-4.7.2`; note current `HEAD` as rollback. Run `git status` first: the tree had unrelated uncommitted work (`modules/box3d_physics`, `thirdparty/box3d`, `thirdparty/README.md`). Stash it or keep it out of the sync commits.
2. **Baseline before merging.** Run shader corpus, driver unit tests, preprocessing tests and scene smoketest on the current branch. Save Tint pass/fail numbers and screenshot-comparison images as the regression reference. Ideally build the web template once too, so later failures are attributable to the merge.
3. **Review the risky diffs on their own, before merging.**
   - Sampler-count change (`b21f5197f9`): may help us (WebGPU has a per-stage sampler limit of 16), but it changes the LTC texture-parameter path that pass 13 in `spirv_preprocess.cpp` targets. Decide whether the pass is still needed, still correct, or redundant. Confirm mobile `color_pass`, `uber_color_pass` and lightmap variants still convert, plus the Forward+ (clustered) variants.
   - `render_forward_mobile.cpp` / `render_forward_clustered.cpp`: check the edits don't collide with our WebGPU changes (`using_subpass_post_process` disabled under `WEB_ENABLED`, API_TRAIT-gated paths, Forward+ enablement work).
   - SSR fix: check it doesn't reintroduce constructs Tint rejects (`textureProj` variants, address-of-handle patterns).
   - Base-class headers (`rendering_device_driver.h`, `rendering_context_driver.h`, `rendering_shader_container.h`): none appear in the delta, so the `drivers/webgpu/` override audit should be a quick confirmation.
4. **Merge.** Merge commit, consistent with the 4.7.1/4.7.2 syncs. Expect few or no conflicts (shader/renderer files are the only overlap). On conflict, keep our WebGPU guards and take upstream logic.
5. **Verify, cheapest first.**
   1. Native editor build (`dev_build=yes`) to catch shared-code breakage.
   2. Rebuild `bin/tint_convert_cli`, run shader corpus, compare to baseline. New failures not in `expected_failures.json` are regressions; newly passing shaders mean the sampler change helped.
   3. Preprocessing tests, driver unit tests, pass 13 tests.
   4. Web WebGPU template build (Emscripten 6.0.9), then scene smoketest, resource lifecycle and screenshot comparison for both Forward Mobile and Forward+. Diff against baseline screenshots, especially area-light and SSR scenes.
   5. Manual check against the real test project (cameraSim) in Chrome.
6. **Fix, keep improvements.** Fix regressions in the preprocessing passes or with a targeted guard, not by dropping the upstream change. If the sampler reduction makes pass 13 unnecessary, note it but keep the pass until a corpus run proves removal is safe.
7. **Record.** Add a Phase 8 entry to `webgpu_notes/TASKS.md` (Status/Issue/Investigation format). Local commits only: no push, no Claude attribution trailers.

## Risks and open questions

- `4.7` has no `4.7.3-stable` tag yet, so a re-merge will be needed when it lands. This plan reaches the current tip only.
- The fork's own CI does not exercise Closure or the upstream-style web builds (`web_builds.yml`); those are a separate workflow and were failing separately.
- The `GODOT_DUMP_SPIRV`-based corpus baseline is unreliable for Forward+: it dumps SPIR-V 1.4, while WebGPU uses 1.3 (see TASKS.md ~line 3290). Use `tint_convert_cli` on 1.3 output for the Forward+ signal.

## Suggested first action

Run steps 1-3 only (baseline plus review of the sampler-reduction diff) to gauge the risk before merging anything.
