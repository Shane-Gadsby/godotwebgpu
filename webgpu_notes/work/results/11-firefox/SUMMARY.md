# Task 11: Firefox desktop (Linux) on SPIRV-Cross WGSL vs Tint, 2026-10-25

(Written by worker-11; saved to the branch by the lead, because the agent harness refused the worker's report file.)

**Conclusion against the bar:** in Firefox desktop on Linux, SPIRV-Cross works exactly as Tint does, and it is as fast.
- **Correctness:** both converters give the same Forward+ `--parity` result (69/71, frame for frame), the same smoketest
  result (23/23), and the shader/pipeline audit finds 0 naga errors and 0 pipeline errors on both sides.
- **Speed:**
  - Heavy Forward+ GPU time is 3.82 ms/frame with SPIRV-Cross (with patch 0019) against 3.88 with Tint.
  - Every other scene is equal on GPU time.
  - Uncapped frame time is equal on the Forward+ scenes and better on benchmark_postfx.
  - The only gap is about 2% fewer fps in two CPU-bound Forward Mobile scenes running near 1000 fps. Their launch ranges
    overlap, their median ms/frame is equal, and their GPU time is identical.
- **No patch from this task.** Nothing failed or ran slower because of SPIRV-Cross.

## Setup
- **Browser:** Playwright's Firefox 155 (revision 1543), headed, on the 4080 SUPER. Prefs come from the new
  `scene_smoketest/firefox_prefs.mjs`: `dom.webgpu.enabled`, `gfx.webgpu.ignore-blocklist`, `gfx.webrender.all`,
  `gfx.webgpu.force-enabled`, and **`dom.webgpu.wgpu-backend=vulkan`**, which pins wgpu to its Vulkan backend (the pref
  was confirmed to exist in Firefox's libxul).
- **Proof that every number is real-GPU:** Firefox reports an empty `GPUAdapterInfo`. What it does report: Forward+
  banner, `maxSampledTexturesPerShaderStage` 64, `timestamp-query` true. In every timing log, nvidia-smi's process table
  shows the Firefox process on the GPU (`C+G .../firefox-1543/firefox/firefox ~1.6 GB`).
- **Configs.** One editor (the lead's, d1e5a67476) and three templates, all with version hash d1e5a67476, so every export
  loads baked:
  - **tint:** Tint template (`webgpu_wgsl_backend=tint`, `WGSL_BACKEND=tint`), built in the detached worktree
    `../godotwebgpu-11-tint`, baked by `tint_convert_cli` built with the Tint backend (byte-identical to the lead's
    `bin/tint_convert_cli.tint`).
  - **sc:** the lead's SPIRV-Cross template, baked by the lead's `tint_convert_cli`, from before patch 0019. Kept for
    context only.
  - **sc19:** SPIRV-Cross with **patch 0019**, built in the detached worktree `../godotwebgpu-11-sc` at d1e5a67476 with
    `thirdparty/spirv-cross-wgsl/{spirv_glsl.cpp,spirv_glsl.hpp,spirv_wgsl.cpp}` and the patch file checked out from
    42d08cc11a — the only engine-relevant changes in that range — so it is the 42d08cc11a engine with the editor's
    version hash (otherwise `baked: 0`).
  - 0019 took effect: across 300 corpus stages the old converter emits 1233 `for (` statements and sc19 emits 0.
- **Exports:** `BAKE=1 WEBGPU_BAKE_CACHE=0`: the fp fixture, `scene_i_forward_plus_heavy`, and all 22 smoketest exports.
- **Timing method:** `compare_ft.sh` (task 08's method), two uncapped launches per (run, scene, config), order rotated
  every run:
  - `perf_guard/frame_time.mjs`: rAF interval, uninstrumented, 12 s settle, 10 s window, uncapped through
    `layout.frame_rate=0`.
  - `gpu_time.mjs --browser firefox`: GPU busy time per frame from timestamp queries.
  - Prechecks: no SCons, load < 4, VRAM, GPU utilization < 45%, no game or LM Studio on the GPU. The utilization limit is
    45% rather than 25% because the user's desktop holds the GPU at 30–40%, the same for both configs.
  - Oblivion Remastered started twice during the session; one launch was marked CONTAMINATED, discarded and re-measured.

## Correctness (Firefox)
| check | Tint | SPIRV-Cross (pre-0019) | SPIRV-Cross + 0019 |
|---|---|---|---|
| Forward+ matrix `--parity --baked` (71 features, native Vulkan reference) | 69/71 | 69/71 | 69/71 |
| features whose verdict differs from Tint's | — | 0 | 0 |
| features whose native diff moved by > 0.005 against Tint's | — | 0 | 0 |
| largest per-frame difference from the Tint frames (142 frames, mean abs 0–255) | — | 0.0023 | — |
| audit, fp matrix: modules / pipelines / errors | 488 / 491 / 0 | 488 / 491 / 0 | 488 / 491 / 0 |
| scene smoketest `--baked` | 23/23 | 23/23 | 23/23 |
| audit, smoketest: modules / pipelines / errors | 3082 / 3302 / 0 | 3224 / 3481 / 0 | 2962 / 3156 / 0 |
| naga validation, 276 engine variants | 276/276 | 276/276 | 276/276 |
| naga validation, 2566-stage real-project corpus (task 05) | 2564/2566 | 2564/2566 | 2564/2566 |

The audit counts vary between runs: they depend on how many pipelines a scene creates before it ends.

- **The two features that fail on both sides** are `sdfgi` and `sdfgi_camera_move`: 0.277 from native against a
  tolerance of 0.08, identically for both converters; Chrome passes them. A Firefox/wgpu matter, not a converter one.
  Subtle by eye; frames in `sdfgi_frames/`. Proposed as a separate Firefox item (for-tasks.md).
- **The two corpus stages naga rejects** are CanvasSdf's `r16snorm` storage texture, the same on both sides (capability
  STORAGE_TEXTURE_16BIT_NORM_FORMATS). The driver remaps the format at load time (§4.14); no export logged an error.
- **The `--baked` "did not use its shader cache" line** (translated 97 > ceiling 80) appears on all three sides: task
  07's runtime-translation gap, not a Firefox difference.
- naga does not enforce barrier uniformity (a barrier under non-uniform control is accepted), so Chrome remains the
  stricter WGSL validator, which smoke.sh already runs.

## Performance (Firefox, uncapped, 1280x720): Tint vs SPIRV-Cross + 0019
Median over launches, min..max in brackets; 3 launches per config (6 for benchmark_pbr and benchmark_shadows). Full table:
`timing_sc0019.md`.

| scene | renderer | fps tint | fps sc19 | median ms/frame tint | sc19 | GPU busy ms/frame tint | sc19 |
|---|---|---|---|---|---|---|---|
| heavy Forward+ | Forward+ | 152 (102..161) | 149 (148..158) | 6.36 (6.06..9.22) | 6.48 (6.14..6.56) | **3.88** (3.81..3.88) | **3.82** (3.77..3.85) |
| demo_3d_platformer | Forward+ | 773 (762..780) | 770 (770..780) | 1.20 | 1.20 | 0.27 | 0.27 |
| benchmark_postfx | Mobile | 349 (337..355) | **383** (363..387) | 2.62 | **2.50** | 0.58 | 0.57 |
| benchmark_shadows | Mobile | 900 (876..924) | 884 (861..896) | 1.07 | 1.08 | 0.19 | 0.19 |
| benchmark_pbr | Mobile | 1017 (974..1024) | 998 (878..1038) | 0.94 | 0.94 | 0.09 | 0.09 |

- **Heavy Forward+:** GPU time 1.5% lower with SPIRV-Cross; uncapped frame time within noise (Tint had one slow launch).
- **benchmark_postfx:** about 10% faster with SPIRV-Cross (the pre-0019 single run had it equal, `timing_pre0019_run1.md`).
- **benchmark_shadows and benchmark_pbr:** about 1.8% fewer fps with SPIRV-Cross over 6 launches each, ranges
  overlapping. CPU-bound at about 1000 fps (GPU 0.09–0.19 ms of about 1 ms); GPU busy and median ms/frame equal, so not
  shader execution. Possibly the 20 stages these scenes translate at runtime through each template's converter; about
  20 µs/frame. Not chased.
- The benchmark scenes' 6 console errors per launch are the same on both sides ("res://benchmark_profiler.gd not found",
  a fixture issue).
- Task 08's `var<private>` LUT candidate was not needed.

## What changed on work/11 (harness only, no engine or converter change)
- `webgpu_tests/scene_smoketest/webgpu_audit.{js,mjs}`: `WEBGPU_AUDIT=<file.jsonl>` records every module's
  `getCompilationInfo()` errors and every pipeline's validation error, labelled; hooked into `run_scenes.mjs`,
  `run_forward_plus.mjs` and `frame_time.mjs`.
- `webgpu_tests/scene_smoketest/firefox_prefs.mjs`: one set of Firefox prefs for every runner.
- `webgpu_tests/perf_guard/frame_time.mjs` (new), `gpu_time.mjs --browser firefox`, `run_forward_plus.mjs --dir`,
  `spirv_cross_eval/validate_wgsl.mjs` `BROWSER=firefox`.
- `results/11-firefox/`: `compare_ft.sh`, `summarize_ft.mjs`, `timing_sc0019.md`, `timing_pre0019_run1.md`,
  `fp_firefox_{tint,sc,sc0019}.json`, `sdfgi_frames/`, log tarballs.

smoke.sh (work/11 after merging webgpu-4.8 at 42d08cc11a, patch 0019 included):
```
identical interface: 276/276, mismatched: 0, missing in candidate: 0
WGSL bytes: reference 6287157, candidate 6870429 (1.093x)
.../wgsl: 276 files, 0 invalid
validation: 276 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261025-170722-2380028)
```
project_eval.py on the 2566-stage corpus with 0019: 2566/2566 converted by both, interface identical 2566/2566, WGSL
1.121x Tint's bytes; Firefox 2564/2566 valid, the same two r16snorm stages as Tint.

## Risks
- Only Playwright's Firefox 155 was tested; the user's own Firefox may differ, and its naga version decides what it
  accepts.
- The SDFGI parity miss in Firefox is open, not converter-related.

## Questions for the user
None.
