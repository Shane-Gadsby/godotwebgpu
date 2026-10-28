# Godot WebGPU — Tasks

> The single task/status doc for this fork. It replaces 36 separate notes files
> (`HANDOFF.md`, `plan-of-attack.md`, `webrtc_web.md`, `webgpu_tests/TASKS.md`, the per-investigation
> write-ups, `review_v3/`, `stubs/`) which were consolidated here on **2026-10-14** and deleted; §5
> lists each one, what it was for, and how to read it out of git history if ever needed.
>
> **Read §1 before building, §2 before picking work up, §3 before debugging anything.**
> §4 is the archive: one entry per completed phase, kept only for cross-reference.
>
> Conventions: statuses are `OPEN` / `BLOCKED (reason)` / `DONE`. Effort estimates in §2 are for a
> **Claude Opus 5 session at medium effort**, where "session" means one focused run including builds
> and verification; items that cannot be done from this machine say so and name what they need.

---

## 1. Current state

**Branch `webgpu-4.8`** — `webgpu-4.7.2` merged with upstream `godotengine/godot`
`e7cfa294a0b81bed7986be04a848cc1832a3f083` (4.8, code freeze, pinned). `webgpu-4.7.2` is the last
4.7-line green state. Git lineage is **repaired** (§4.16): `merge-base` against upstream is correct,
syncs are ordinary merges. `backup/pre-lineage-repair/*` and `backup/pre-claude-author-strip*` on
`origin` **must not be deleted**.

**Forward+ is at parity with native Vulkan feature by feature** as of §4.29 (2026-10-17), measured by
`run_forward_plus.mjs --parity` rather than the delta-only matrix. The earlier claim here, "no known-broken
shipped feature", was wrong: the delta matrix could not see SDFGI, volumetric fog shadows, omni cube shadows,
`DEPTH_TEXTURE` or mipmap bias diverging from native (§3 trap 28). What remains is spec-limited and listed in
§2.31; everything else in §2 is hardening, measurement, coverage or a rare non-visual error.

**Web audio is at parity with native for everything Sample playback can do** as of §4.30 (2026-10-18):
the web default (`audio/general/default_playback_type.web` = Sample) re-implements `AudioServer`'s mixer on a Web
Audio graph, and is now checked against ports of the native code sample for sample (`webgpu_tests/web_audio`) and
end to end (the smoketest's `audio_parity` scene). Bus effects and streaming are what Sample playback cannot do; they
are warned about at export and at runtime, and the ways to close them are §2.32 (deferred by decision). The editor's
lists all take drag-and-drop the same way since §4.31.

**The SPIR-V → WGSL converter is SPIRV-Cross** as of §4.36 (2026-10-25): SPIRV-Cross's WGSL backend
(`thirdparty/spirv-cross-wgsl`, four local patches, never upstreamed) after the same `run_pipeline()` passes, in the
runtime, the build-time precompile, the export-time bake and every test tier. Tint is deleted (`thirdparty/tint`,
`tint_wrapper.*`); the host tool is `bin/wgsl_convert_cli` (`drivers/webgpu/wgsl_cli/`). Forward+ parity stayed 71/71
(every arm the matrix had then), and desktop Chrome, Firefox and Chrome for Android are as fast or faster. The tag `pre-SPIR-V-Cross` marks the tree
before. Rebuild editor, `wgsl_convert_cli` and templates together after pulling it: an older editor looks for
`tint_convert_cli` (§3 trap 61). Browser-side Tint (Chrome/Dawn compiling our WGSL) is untouched.

**2026-10-26 (evening): the Forward+ matrix has 87 arms and is 87/87 against native Vulkan**, unbaked and baked, on
the merged tree (lead, 20:24). It had 71 on 2026-10-25, 82 after §4.39 (82/82 with the raycast module in the templates,
§4.38; 81/82 before, `occlusion_culling` KNOWN), then gained `dof_nan` (§4.41), `white_balance` (§4.42), `glow_nan`,
`ssr_nan` and `auto_exposure_nan` (§4.44). The bake gate's ceiling is 150 translated stages (§2.39). What landed that
day, all merged into `webgpu-4.8` and nothing pushed: **camera white balance** on `CameraAttributes` (2000–10000 K, tint
-150..150, all three renderers and the web, §4.42); **NaN containment** in bokeh DOF, glow, SSR and auto exposure
(`finite_inc.glsl`), and the MetaHuman strand NaN that showed it (§4.41, §4.44); **bake isolation**, the shader baker's
WebGPU capability answers confined to the baker's own callbacks, so the editor's frames during a web export no longer
log FSR 2 uniform mismatches and FSR 2 bakes for the target (§4.43); **MetaHuman skin pores** scaled to each mesh's UV
density (§2.33, follow-ups §2.46); and **the editor's update check** against this fork's GitHub releases (§4.45,
follow-ups §2.47). §3 traps 70–73 are what it cost. **Occlusion
culling works on the web** (§4.38): the raycast module (Embree) is in every web template by default
(`build-linux.sh --no-web-raycast` leaves it out), and the Web export option `rendering/occlusion_culling` turns it off
per export. **The performance numbers to quote come from the load-ramp benchmark** (`webgpu_tests/benchmark/run_ramp.sh`,
`webgpu_tests/README.md` §11; first results at the end of §2.30 and in `results_ramp/2026-10-26/ramp_summary.md`):
`README.md`'s performance table is generated from it, and §2.17's fps matrix is superseded. **User documentation lives in
the GitHub wiki** (§4.40), and the README was rewritten around it.

**2026-10-26 (night): `main` is green on GitHub with all of the above.** `webgpu-4.8` was merged into `main` as #20,
#21 and #22 (pushed at the user's request, for this CI fix only); 🔗 GHA on the #22 merge passed every job, the three
sanitizer editors included, and drafted release `main-41def1729690f515f19130b4a4dc099b80c80416`. #20 failed static checks on
files the work board had committed, and #21 failed the GCC sanitizer editor's link because SCU never compiled the
update-check test (§4.46, §3 traps 74 and 75).

### Verified tiers

| tier | result | when / where |
|---|---|---|
| Forward+ `--parity` and `--parity --baked`, real GPU, Chrome, full 87-arm matrix on the merged tree (tasks 24–30) | **87 / 87** both; bake gate green under the 150 ceiling (§2.39) | 2026-10-26 20:24, lead |
| `webgpu_tests/dof_nan/run.sh` (one NaN pixel through every bokeh DOF mode, glow, SSR, auto exposure; Forward+ and Mobile, native) | **0 failures** (24 DOF cases + glow, SSR, exposure; 7 failures before §4.44, 18/24 DOF before §4.41) | 2026-10-26, lead, §4.41, §4.44 |
| `webgpu_tests/bake_isolation/run.sh` (interactive Web export from a windowed editor with FSR 2 + TAA, Xvfb + Vulkan) | **PASS**: 8 editor frames during the bake, 0 mismatch errors, 8/8 FSR 2 passes baked for WebGPU (FAIL on the old editor) | 2026-10-26, lead, §4.43 |
| `webgpu_tests/metahuman/run.sh` with `nan_check.gd` | verify and nan **0 failures** after a fresh import (`run.sh` reimports when the editor is newer, §3 trap 71); `--shots` 0 failures with the pore fix | 2026-10-26, lead, §2.33, §4.41 |
| `tests/editor/project_manager/test_engine_update_label.cpp` (`--test --test-case="*EngineUpdateLabel*"`) | **3 / 3** cases; merged editor compiles | 2026-10-26, lead, §4.45 |
| 🔗 GHA on `main` (static checks, every platform's editors and templates, the 8 web templates, Linux sanitizer editors, editor plugins, draft release) | **all green** on the #22 merge, run 40321431357 | 2026-10-26, GitHub, §4.46 |
| converter smoke (`webgpu_notes/work/smoke.sh`) after each of tasks 24–28 | 276/276 interfaces, 552/552 scan facts, 0 invalid; only change override id 18 in `tonemap_mobile` (§4.42, regenerated on purpose) | 2026-10-26, lead |
| Forward+ `--parity` and `--parity --baked`, real GPU, Chrome, nothreads templates **with** the raycast module | **82 / 82**; `occlusion_culling` 29 → 8 objects, parity 0.002; scene smoketest 23 / 23 Chrome and Firefox | 2026-10-26, work/22, §4.38 |
| Forward+ `--parity`, unbaked and `--baked`, full run on the 82-arm matrix, templates without the raycast module | **81 / 82** both, `occlusion_culling` KNOWN (no culler on the web then); bake gate green at the raised ceiling (translated ~117 ≤ 130, §2.39) | 2026-10-26, lead, §4.39 |
| load-ramp benchmark (`run_ramp.sh`, 6 legs × 5 ramps × 2 runs, RTX 4080 SUPER, Chrome 156) | 60 / 60 ramp-runs converged; WebGPU Forward+ / WebGL: draws 1.23x, nodes 1.61x, fill 0.67x, lit 6.42x, particles 1.50x | 2026-10-26, `results_ramp/2026-10-26/`, §2.30 |
| Forward+ `--parity --baked`, real GPU, Chrome, after SPIRV-Cross patch 0019 | **71 / 71** (fsr2 0.142, volumetric_fog 0.001, taa 0.006); RESULT FAIL only on the pre-existing bake gate (translated 97 > 80, §2.39) | 2026-10-25, merged swap tree, §4.36 |
| Forward+ `--parity`, unbaked (every stage converted at runtime by SPIRV-Cross) | **71 / 71** | 2026-10-25, `62a570d31f` pair, §4.36 |
| Firefox desktop (Linux, wgpu on Vulkan): Forward+ `--parity`, smoketest, naga | **69 / 71** with SPIRV-Cross *and* Tint (SDFGI 0.277, Firefox-only, §2.40); smoketest **23 / 23**; naga 276/276 engine variants, 2564/2566 project stages | 2026-10-25, §4.36 |
| Chrome for Android (Razr 60): GPU ms/frame, Tint vs SPIRV-Cross+0019; fp_check Look set | fp 16.9 / 16.8, heavy 73.8 / 71.3, postfx 23.3 / 23.2, platformer 29.6 / 28.7, stress 28.8 / 28.1; 34 frames identical to Tint's | 2026-10-25, §4.36 |
| user's `cameraSim` (Ava MetaHuman), 5 views, SPIRV-Cross baked vs native Vulkan | MAD **0.00–0.03**, same as Tint-baked and as run-to-run noise; `baked 1570 / translated 0`; user: "Matches" | 2026-10-25, §4.36 |
| swap removal branch (work/9): editor + all 8 web variants `dev_mode=yes`; offline tiers | builds clean, 276 compiled / 0 failures; interface 276/276 and scan facts 552/552 against the stored references; shader_corpus 14/14, preprocessing 291/0/1, driver unit 379/0, fuzzers smoke PASS; `local_ci.sh --dev-mode --no-safari` 28 / 1 / 5 (web audio e2e nothreads, environmental: the 2026-10-23 export fails the same way) | 2026-10-25, §4.36 |
| `raytracing_probe --software` (swiftshader, 320×180, 18 k and 190 k triangles) | **12 / 12 checks**: GPU BVH traversal = CPU brute force on 66/66 sampled primary rays per scene (max \|Δt\| 1.4e-5), hit / occlusion / bounce fractions as the fixed camera predicts, depth 21 within the 48-entry stack. Throughput numbers from this adapter mean nothing | 2026-10-23, §2.34 |
| `local_ci.sh --dev-mode --no-safari --bake`, real GPU, on `e0b5115389` (PrimitiveMesh change included) | **partial, stopped by request**: werror editor + both nothreads templates built, shader corpus, every unit tier (379 / 291 / overrides / precompile / self-tests / audio 40) PASS, all re-exports PASS, **scene smoketest Chrome 22 / 22 PASS**; stopped during the Firefox smoketest — Forward+ matrix, parity, perf guard, WebRTC, lifecycle, screenshots, baked fixture **not run** | 2026-10-23, §1 "Experiments in flight" |
| `raytracing_probe` on the Razr 60 (`run_probe_phone.mjs`, Mali-G615, 1280×720, 93 k triangles, 10 dispatches) | **6 / 6 checks**, 32/32 rays = brute force; **primary 45 Mrays/s, shadow 40, incoherent bounce 19**; 1 spp primary+shadow+bounce 64.6 ms — RT is desktop-only | 2026-10-23, §2.34 |
| `raytracing_probe` on the real GPU (RTX 4080 SUPER, 1920×1080, 93 k and 945 k triangles, 20 dispatches) | **12 / 12 checks**, 66/66 rays = brute force per scene; **primary 5.3 / 3.7 Grays/s, shadow 5.4 / 3.8, incoherent bounce 2.3 / 1.9**; 1 spp primary+shadow+bounce 1.17 / 1.55 ms; timestamps = wall clock − 50–100 µs; GPU idle (4 %) during the run | 2026-10-23, §2.34 |
| web audio sample driver vs native, offline (`webgpu_tests/web_audio/run_tests.mjs`) | **40 / 40**: loop modes sample-identical to `AudioStreamPlaybackWAV`, ramps/fades, bus rules, positions, 3D high shelf within 1 dB | 2026-10-18, §4.30 |
| web audio end to end, native vs nothreads (no COOP/COEP) and threads (`run_e2e.mjs`) | `[AUDIO-CHECK] PASS` on both, every boolean equal to native, each limitation warning exactly once on web and none natively | 2026-10-18, §4.30 |
| scene smoketest with `audio_parity` (real GPU) | **22 / 22 Chrome**; `audio_parity` also passes on Firefox | 2026-10-18 |
| 3D platformer audio glue (`tests/web_audio_profiling.mjs`) | **0.002 ms/frame** (0.029 before §4.30, 0.148 before §2.30) | 2026-10-18 |
| editor drag-and-drop onto every list (`tests/editor_drop/run.sh`, Xvfb) | **32 / 32** real drags; editor `-Werror` clean as a `dev_mode=yes` build | 2026-10-18, §4.31 |
| native editor + web template build | 0 errors, 0 warnings, incl. `warnings=extra werror=yes` | 2026-10-05, 4.8 port (§4.15) |
| `wgsl_precompile.py` | 274 compiled, 0 glsl / 0 tint failures; **deterministic** (two runs, different `PYTHONHASHSEED`, byte-identical 1,806,893 B header) | 2026-10-14, `597d27e09` (§2.16 gap 5) |
| `shader_corpus` | 14 / 0 | 4.8 |
| `driver_unit_tests` | 379 / 0 | 2026-10-17, §4.29 |
| `preprocessing_tests` | 291 / 0 / 1 skip | 2026-10-17, §4.29 (tests 24-27, `spirv-val` on each output) |
| full `local_ci.sh --dev-mode --no-safari` with `WEBGPU_REAL_GPU=1`, after §4.29 | **29 passed / 0 failed / 2 skipped (Safari)**: werror editor + both nothreads templates, every unit tier, scene smoketest 21/21 Chrome + Firefox, perf guard, Forward+ matrix **71/71**, **parity vs native Vulkan 71/71**, WebRTC 9 fixtures, lifecycle, screenshots | 2026-10-17, uncommitted §4.29 tree |
| `resource_lifecycle` | 6 / 0 | 2026-10-14, real GPU |
| `screenshot_comparison` | 8 / 0 | 2026-10-14, real GPU (was last recorded at 4.7.2) |
| scene smoketest (Chrome, container) | 20 pass / 0 fail / 0 skip | 4.8, swiftshader → **Forward Mobile** |
| scene smoketest (real GPU) | 21 / 0 on Chrome **and** Firefox | 2026-10-14, after §2.1 |
| scene smoketest (swiftshader) | 21 / 0 | 2026-10-14, after §2.12's deletion and §2.13's warning |
| Forward+ feature matrix | **64 / 64** on real GPU, Chrome; VoxelGI now 7.743 vs Vulkan 7.742 | 2026-10-14, after §2.11's fix (`4d407aacc`); re-run in the final `local_ci.sh --dev-mode` at `23246cb66` |
| full `local_ci.sh --dev-mode` with `WEBGPU_REAL_GPU=1` | all stages green incl. fog, fonts, smoketest Chrome+Firefox, lifecycle, screenshots 8/8; WebRTC 516 assertions (rerun by hand — the stage skipped on a Playwright mismatch, §3 trap 3a) | 2026-10-14, `23246cb66` |
| full `local_ci.sh --dev-mode --bake` with `WEBGPU_REAL_GPU=1` | green through the baked matrix (`baked: 415, translated: 33`); the WebRTC stage SKIPped on trap 3a's Playwright mismatch, so WebRTC (258 assertions, web half), lifecycle and screenshots (8/8) were rerun by hand and passed | 2026-10-14, `af3d57ddd1` (2 MB push-constant ring) |
| Forward+ matrix, unbaked **and** `--baked` | 64 / 64 both; 60 of 64 deltas within 3% of the pre-optimisation run (`ambient_light` bimodal on unbaked, particles noisy) | 2026-10-15, real GPU, after the encoder-split removal, range flushes, staging coalescing and `firstInstance` for Forward+ (§2.17) |
| scene smoketest (real GPU, Chrome) | 21 / 21 on fresh exports (needs `GODOT_EDITOR_BIN`/`GODOT_TEMPLATE_ZIP` set, §3 measurement traps) | 2026-10-15 |
| `perf_guard` (new, §2.16 gap 4) | green on real GPU and on swiftshader; 2 encoders + 2 submits per frame, 64-350 KB `writeBuffer` per frame; `PERF_GUARD_SCALE=0.1` goes red | 2026-10-15 |
| WebRTC parity (real WebGPU path) | **353 / 353** across nothreads, threads, dlink and baked; the fixture had never run on WebGPU before today (§2.29) | 2026-10-15, real GPU |
| user's `cameraSim` project, baked export | 163 fps, `baked 427 / translated 0`, 2.0 s stall, renders correctly (§2.14) | 2026-10-15 |
| user's `cameraSim` project, **export time** (§4.23) | baked export **27–31 s → 7.8 s** cold (924 unique stages through Tint in 1.8 s), **4.2 s** with a warm WGSL cache (the unbaked export is 3.8 s); containers byte-identical to the old path; `index.pck` 14.9 MB + `index.shaders.pck` 35.6 MB, the latter md5-stable across exports; the browser fetches both, `baked 747 / translated 0`, Forward+ | 2026-10-17 |
| user's `sewer-shift` project, baked export | **0 unbaked warnings** (was 28 from a stale editor, §4.27): 2317 stages, 558 duplicates, 913 from the WGSL cache, 846 declined up front by `_wgsl_unsupported_reason()` at `print_verbose`; `index.shaders.pck` produced | 2026-10-17, `fae7d95feb` |
| warning channel (§4.28) | `_godot_js_os_print_warning` present in the linked release nothreads template with its `GodotConfig.on_print_warning … else console.warn` body (survives DCE); eslint + clang-format clean | 2026-10-17, `d937802c50` |
| cache prunes (§4.23) | project copy: `.godot/exported` 2 → 1 dir, `.godot/shader_cache` 1,107 → 80 group dirs (94 → 3 MB); warm export unchanged afterwards | 2026-10-17 |
| heavy Forward+ scene, baked | `baked 759 / translated 0`, stall 18.5 -> 7.7 s, 155 fps uncapped, push-constant writes 3608 -> 106 per frame | 2026-10-15, §2.6 / §2.17 |
| benchmark matrix (7 scenes x native / WebGL / WebGPU Mobile / WebGPU Forward+) | capped: everything at the 165 Hz cap, post-fx fixed 91 -> 165; uncapped (corrected): WebGPU 25-50% of native, Forward+ within 3-10% of Mobile except post-fx (414 vs 583) | 2026-10-15, §2.17 |
| heavy Forward+ scene, uncapped, after all CPU work | ~220 fps (was 176), 1109 draws (was 3549), `setVertexBuffer` 82 (was 3371), push-constant writes 148 (was 3608); pixel-identical (max diff 1/255) | 2026-10-15, `bc9f4d3768`, §2.17 / §2.30 |
| Forward+ matrix, **baked** export | **64 / 64**, `baked: 415, translated: 33`; now a CI tier (`local_ci.sh --bake`, fails on `baked == 0`, `translated > 80` or a cache-mismatch warning — verified red on an unbaked export) | 2026-10-14, `64de4bda4`, §2.29 |
| scene smoketest, **baked** export (real GPU, Chrome) | **21 / 21**; `--baked` asserts the shader cache was used (gl_compatibility projects exempt); export takes ~6.6 min | 2026-10-14, `231e6ca1e`, §2.29 |
| heavy Forward+ scene (3549 draws/frame) | 164 fps, `ringOverflows: 0` (was 1), `rwShadowRefreshes: 40` | 2026-10-14, `af3d57ddd`, §2.6 |
| WebRTC parity | 9 fixtures × 3 export variants × 2 browsers = 54 identical native-vs-web diffs | 4.8 |
| WebRTC congestion | native/web agree at 91-97% on every transfer mode | 4.8, after the F10 fix |
| real-hardware Forward+ (RTX 4080 SUPER) | Chrome 0 errors, Firefox 0 real errors, both report Forward+, output matches native Vulkan | 2026-10-05 (§4.15) |
| editor (`webgpu=yes`) + nothreads + threads + threads/dlink templates, one tree | all four build; changed files `-Werror` clean as `dev_mode=yes` objects in editor, nothreads and threads configs | 2026-10-16, `db437ca832` (§2.5) |
| scene smoketest (real GPU) after the render thread, and again after PR #123996 | **21 / 21 Chrome and Firefox**, then 21 / 21 Chrome on the audio rebuild | 2026-10-16 |
| Forward+ feature matrix | **64 / 64** | 2026-10-16, `db437ca832` tree |
| Separate render thread (`threads=yes`, `thread_model=2`) | platformer, cameraSim, heavy and four benchmark scenes at the 165 Hz cap on Chrome **and Firefox 155**, dlink too; cameraSim pixel-identical to nothreads (FPS counter text aside); `requestQuit()` clean | 2026-10-16, §2.5 |
| before/after matrix, 7 scenes × {before, nothreads, Safe, Separate, dlink+Separate} | main thread −8% (fixes) / −82% (with the render thread) averaged; heavy GPU 3.40 → 1.46 ms, 229 → 292 fps uncapped; table in §2.5 and `README.md` | 2026-10-16 |
| user's `cameraSim` project, Separate mode | 164 fps capped, main thread 0.51 → 0.09 ms/frame, GPU 0.64 → 0.58 ms | 2026-10-16 |
| Safari | **never run** | §2.8 — macOS only |
| Android (Chrome 154, Motorola Razr 60 / Mali-G615) | **first run 2026-10-17**: cameraSim Forward+ 1080p 20.5 fps, GPU-bound (48.7 ms GPU/frame: lighting pass 28, bokeh DOF 14.5); `scaling_3d/scale=0.5` → 59.6 fps | §2.9 |

Reference numbers: the user's project cold-loads with a **~1.0 s** stall (was ~9.2 s),
`{baked: 360, translated: 0}`; BC1 compression is equivalent to uncompressed at RMSE 0.0006.
Over a **20 Mbit/s** link with gzip, a baked export of `3d/platformer` reaches its first frame in
**12.9 s** (was 25.7 s before §2.27's SPIR-V drop; the unbaked export takes 17.7 s), pck 65.4 MB / 11.2 MB
gzipped — see §2.27 and §4.15 before optimizing anything CPU-side for load time.

### Experiments in flight — 2026-10-23 (read this first next session)

Two things were started and deliberately not finished; each has everything it needs to be picked up without
re-deriving anything. Both commits are local on `webgpu-4.8` (`127ce9a34c`, `d64e8a2602` and this doc); nothing pushed.

| experiment | state | how to run it | what the result decides |
|---|---|---|---|
| **PrimitiveMesh arrays rebuilt on the CPU** (§3 trap 54, `127ce9a34c`) | committed. **Partly verified 2026-10-23** by `WEBGPU_REAL_GPU=1 ./webgpu_tests/local_ci.sh --dev-mode --no-safari --bake`: the dev-mode (`werror`) editor and both web templates built clean, every unit tier passed, every scene re-exported, and the **scene smoketest passed 22/22 on Chrome on the real GPU** (the first real-scene run of the change). **Stopped by request during the Firefox smoketest**; the Forward+ matrix, parity, perf guard, WebRTC, lifecycle, screenshots and the baked fixture did not run. `bin/` now holds dev-mode builds of `e0b5115389`; the smoketest exports match them. | Same command, when wanted: `pgrep -ax scons` (idle), then `WEBGPU_REAL_GPU=1 ./webgpu_tests/local_ci.sh --dev-mode --no-safari --bake` (~1 h; `--no-rebuild` reuses the 2026-10-23 builds and exports, which are from `e0b5115389` + docs-only commits, so they are current). Then re-export `/mnt/109313D2109313D2/godot-game-dev/webgpu-test` from `bin/godot.linuxbsd.editor.x86_64` and load it: the TressFX collider's `mesh has no extent` must be gone. | green → done, record it in §4. A regression could only come from a `surface_get_arrays()` caller that relied on the quantized readback values. Worth a sweep for other synchronous readbacks in driver-agnostic code (trap 54's general lesson). |
| **Ray / path tracing on Forward+** (§2.34) | investigation written; probe `webgpu_tests/raytracing_probe/` written, self-checking, **measured on both devices 2026-10-23**. Desktop (RTX 4080 SUPER, 1080p): **1.9–2.3 Grays/s incoherent, 3.7–5.3 Grays/s coherent**, 1 spp primary + shadow + bounce = 1.2–1.6 ms — the gate for full-resolution RT shadows/AO was 600 Mrays/s. Phone (Razr 60, Mali-G615, 720p): **19 Mrays/s incoherent, 40–45 coherent**, 1 spp = 65 ms — a hundred times less. Traversal matched brute force on every sampled ray on both. | Desktop: `cd webgpu_tests/raytracing_probe && node run_probe.mjs --shot probe.png` on an idle GPU (`nvidia-smi`). Phone on USB: `node run_probe_phone.mjs --shot phone.png --display 4627039422300187648` (serves, `adb reverse`, opens the tab through CDP, reads the result, closes only its tab). Both exit 0/1 on the checks. | **Decided: build Tier A as a desktop-class opt-in** (§2.34 step 2 — RT AO writing the SSAO texture, then RT shadows through `sscs_buffer`, behind a project setting, default off, disabled on mobile-class adapters). Nothing left to measure before starting. |

How to talk about them: `webgpu_site/RAY_TRACING_DESIGN.md` is the design (and §7 there is the full path tracer, saved for
later); §2.34 holds the research trail and measurements (why software-only, upstream's RT API and who uses it, the four tiers,
the shared acceleration-structure layer, WebGPU's constraints, the budget arithmetic); `webgpu_tests/raytracing_probe/README.md`
says what each kernel stands for and how to read Mrays/s; §3 trap 54 is the readback lesson behind the first experiment.
The probe's `--software` run is a correctness test with an exit code and could join `local_ci.sh`; it is not wired in yet.

### What is built right now — snapshot 2026-10-16 (end of the render-thread day), HEAD after `2c3544102e`

**Read the 2026-10-18 additions first:** §4.30 (web audio parity, its two test tiers, the warnings), §2.32 (the
deferred audio gaps, with trade-offs), §4.31 (editor drag-and-drop), §4.32 (AccessKit), §3 traps 32-34. Every
commit is local; everything in `bin/` (editor, Linux templates, all eight web templates) is from a full `build-linux.sh`
run of this tree on 2026-10-18 17:08-17:17.

**Read the 2026-10-16 additions next:** §2.5 (the separate web render thread is implemented and measured — design, traps,
before/after table), §2.30 (SSIL hoist pass, read-write storage textures, the upstream shadow-atlas bug, prepare-for-use trait,
Web Audio writes, PR #123996 cherry-picked), §3 traps 21 and 22. `bin/` editor (`webgpu=yes`) is from the `b4de0b0b4b` tree;
all eight web templates (release/debug × nothreads/threads × dlink or not) were relinked after it with the
`SharedArrayBuffer` guard in `library_godot_audio.js` (trap 22 — the cherry-picked PR #123996 broke every `threads=no` export
served without COOP/COEP). The last full verification: Forward+ 64/64, scene smoketest 21/21 Chrome + Firefox, unit tests,
`-Werror` objects (§1 tiers) on `db437ca832`; the export option and the guard were verified by export + browser run. Scratch exports/measurement scripts for the
before/after table live in this session's scratchpad only (`exports_matrix.sh`, `measure_matrix.sh`, `thrprof.mjs`).

**Read the 2026-10-15 additions next:** §2.6, §2.14-2.17, §2.19, §2.22, §2.29 changed state today; the driver changed (encoder split removed, `buffer_flush_range`, deferred staging uploads, Forward+ `firstInstance`) and the full `local_ci.sh` was *not* re-run on the final tree — the individual tiers in "Verified tiers" were, each against fresh exports. Working tree clean; **nothing pushed** (every commit below is local on `webgpu-4.8`, per the standing
no-push rule). The last full `WEBGPU_REAL_GPU=1 ./webgpu_tests/local_ci.sh --dev-mode --bake` was green on the
source of `af3d57ddd1` (the commits since are docs, tests, `wgsl_precompile.py` include roots and the `features.js` WebGL probe flag) (see "Verified tiers" caveats: the WebRTC stage needed a manual rerun, §3 trap 3a).

| artifact | state |
|---|---|
| **2026-10-18, full `build-linux.sh` (17:08-17:17)** | editor, Linux templates and all eight web templates (dlink included) rebuilt together from this tree: after §4.30-4.32, so every template matches the editor's baker and the editor's export (no position-worklet file) |
| **after §4.30's follow-up (2026-10-18)** | editor and the four non-dlink web templates rebuilt again for the warnings, the `changed` re-render and the `AudioServer::finish()` leak fix |
| **after §4.30 (2026-10-18)** | `build-linux.sh --skip-native-templates --web-dlink no` rebuilt `godot.linuxbsd.editor.x86_64` and all four non-dlink web templates (debug/release × threads/nothreads) on the §4.30 tree. The editor no longer lists `*.audio.position.worklet.js` in a PWA cache, so pair it with templates from this tree or later; the dlink zips (Oct 17) predate §4.30 |
| **after §4.29 (`f59c88668d`)** | `local_ci.sh --dev-mode` rebuilt `godot.linuxbsd.editor.x86_64`, `godot.web.template_release.wasm32.nothreads.zip` and `…nothreads.dlink.zip` as **`dev_mode=yes`** builds of that tree. Fine for tests, not for benchmarks. The **installed** export templates (`~/.local/share/godot/export_templates/4.8.dev/`) predate §4.29's shader changes — run `./build-linux.sh` before exporting real projects. The rows below describe the earlier state |
| `godot.linuxbsd.editor.x86_64` | rebuilt 2026-10-17 with `webgpu=yes` on the uncommitted §4.23 tree (batched bake, WGSL cache, separate shader pack, cache prunes). Matches the nothreads release template below only |
| `godot.web.template_release.wasm32.nothreads.zip` | rebuilt 2026-10-17 on the same tree and installed over `~/.local/share/godot/export_templates/4.8.dev/web_nothreads_release.zip` (previous zip backed up in the session scratchpad). Loads `index.shaders.pck`, `baked 747 / translated 0`. **The other seven installed web templates predate the shell change** — rerun `build-linux.sh` before exporting other preset combinations |
| `godot.web.template_release.wasm32.nothreads.dlink.zip` | from the CI run before the late rebuilds (19:10). Compile check only — nothing loads it. **Stale** w.r.t. HEAD |
| `godot.linuxbsd.editor.dev.x86_64` | `dev_build=yes` native build from 2026-10-16 (compile check of the §2.5 server changes); not for exports |
| `godot.web.template_release.wasm32.zip` (threads) | rebuilt 2026-10-16 at the same tree; runs the Separate render thread with `thread_model=2` (§2.5). `template_debug` zips are **stale** (Oct 14-16, various trees) |
| `godot.linuxbsd.editor.double.x86_64` | §2.23's `precision=double` check; keep or delete freely |

Scratch state worth knowing about (all outside git): `/tmp/plat_baked`, `/tmp/plat_unbaked` (platformer
exports used for the §2.27 re-measurement), `/tmp/fpref*` (native-Vulkan reference frames). `webgpu_tests/
scene_smoketest/exports/*` and `forward_plus/export{,_heavy}` are regenerated by CI and gitignored.

#### What landed 2026-10-20, in order (on `webgpu-4.8`)

| commit | item | what it did |
|---|---|---|
| `98b210017` | §2.27, §2.18 | Shader variant pruning (`ShaderRD::GroupBakeRequirement`, `shader_baker/variant_pruning`): unreachable groups not baked, unverified ones into the lazily fetched `<name>.shaders_extra.bin` (platformer startup shaders 10.7 → 1.84 MB gzipped). Per-family texture packs (`vram_texture_compression/separate_packs`): the browser fetches only the family it samples |
| `c19a9a73e` | §2.4 | Driver creates shader modules and compute pipelines on first use: the startup stall was the GPU process compiling ~90 unused compute pipelines and ~500 modules while the startup waited on frames (callMain → first frame 6.8 → 2.8 s, swiftshader) |
| — | §2.4 | `startup_phases/gpu_process_trace.mjs`: traces the page and the GPU process with Dawn's categories; `webgpu_site` docs and FAQ updated |
| `fcec15d2d` | §2.4 | Deferred compute pipelines prewarmed in the background (`createComputePipelineAsync()`) from ~120 frames after startup |
| `3ce02f33e` | §4.34 | Editor: per-project default folders for new files |
| `84ab9213d` | §2.4 | Render pipelines compiled in the background created with `createRenderPipelineAsync()` (`PipelineHashMapRD` + two new `RenderingDeviceDriver` virtuals); `gpu_process_trace.mjs --after`. First real-GPU numbers (sewer-shift, RTX 4080): `CrGpuMain` before the first frame 810 → 550 ms, sync render pipelines after it 1.35 → 0.17 s, no pipeline stall looking around |
| `8bf30f3c2` | §4.35, §3 trap 41-42 | CI: editors zipped on the build runner (they shipped without their executable bit), manual per-platform editor workflows; `local_ci.sh` no longer dies at the first SKIP or FAIL |
| `8443c40c5` | §2.4, §3 trap 43 | `webrtc_parity`'s Chrome device loss: the local launch ran headless (swiftshader, no canvas present); bisected to the innocent `c19a9a73e`, fixed with `headless: isCI` (353/353) |
| after `8443c40c5` | — | `braced_gdscript/fixture_project/.godot/` untracked and ignored: `c19a9a73e` committed the editor cache by accident, and every export changed it |

Measured in a container with no GPU (headless Chromium, swiftshader): a scratch template that skips the
<48-textures fallback was used to run Forward+ there. The first real-GPU run came the same evening: a user's game
(sewer-shift, RTX 4080), whose GPU-process trace led to the asynchronous render pipelines (§2.4); `local_ci.sh` with
`WEBGPU_REAL_GPU=1` covers the Forward+ matrix and `--parity`.

#### What landed 2026-10-18, in order (all on `webgpu-4.8`, all local)

| commit | item | what it did |
|---|---|---|
| `be304f5cdc` | §4.30 | Web audio: Sample playback brought to parity with `AudioServer` (surround, loop modes, ramps/fades, bus rules, `set_bus_layout()` use-after-free, 3D high shelf, positions without a worklet, shared buffers, integer handles; stream-path precision, mono mic, capture wrap); `webgpu_tests/web_audio` tier |
| `bcb8905fa7` | §4.30, §2.32 | Self-checking audio suite as the smoketest's `audio_parity` scene; WAV/Ogg/MP3 setters emit `changed` (re-render of stale samples); "cannot be sampled" warning only when Sample is explicit; **upstream** `AudioServer::finish()` leak; warnings for Sample playback's gaps at export and runtime |
| `16d2e2bf98` | §4.31 | Editor: one drag-and-drop behavior for arrays, dictionaries, property arrays, the Autoload/Localization lists, bus effects and the Animation Library; `tests/editor_drop` real-drag test |
| `aa40d03317` | §4.32 | `install_accesskit.py` skips the download when the pinned version is installed |

#### What landed 2026-10-17, in order (all on `webgpu-4.8`, all local)

| commit | item | what it did |
|---|---|---|
| `c7e60e8d11`, `b07b195649` | §2.9 | Android profiling tools and the first on-device (Mali-G615) results |
| `db0d854a03` | §2.5 / §2.30 | Web audio profiler (`tests/web_audio_profiling.mjs`); §2.5 closed |
| `d862f8f0ed` | §4.23 | Export-time bake batched and parallel, WGSL cache, separate shader pack, cache prunes; `_wgsl_unsupported_reason()` extended — **this is the commit a stale editor predated**, see §4.27 |
| `365d0a3b01` | §4.24 | Loading screen: per-stage text and visibility from the export preset, flex layout |
| `3be3b34f99` | §4.25 | Per-variant generated sources under `gen/<variant>/`: cross-variant invalidation 30 objects → 0 |
| `58c5c6af6a` | §4.26 | ccache on by default; `--no-ccache` keeps the compiler path and sets `CCACHE_DISABLE=1`, so toggling costs no rebuild |
| `fae7d95feb` | §4.27 | Bake messages name the shader and stage; readback note → `print_verbose`; `build-linux.sh` installs the editor through a temp file + `rename()` |
| `719758b96f` | §4.28 | Pointer lock promise rejection handled (no more unhandled `WrongDocumentError`) |
| `d937802c50` | §4.28 | `WebLogger`: engine warnings go to `console.warn`, errors stay on stderr |
| `f59c88668d` | §4.29, §2.31 | Forward+ parity with native Vulkan: driver, three IR passes, shared-renderer shader fixes, the `POINT_COORD` upstream bug, FSR2 patch 0004, `--parity` tier; `local_ci.sh --dev-mode` 29/0/2 |

#### What landed 2026-10-16, in order (all on `webgpu-4.8`, all local)

| commit | item | what it did |
|---|---|---|
| `f44c233f2b` | §2.30 | SPIR-V pass `hoist_constant_local_arrays`: SSIL's GPU pass 0.6 → <0.05 ms; `perf_guard/gpu_time.mjs` |
| `31d796b8dc` | docs | History rewrite note: the unpushed range no longer carries `export_heavy` blobs |
| `a79c6f242f` | §2.30 | Read-write storage textures bound as `read_write` per format: heavy scene GPU 2.63 → ~1.6 ms |
| `3c1bdbeeb9` | docs | cameraSim retest (GPU 0.65 → 0.58, pixel-identical) and the 16-scene per-pass sweep |
| `eb1a92ce84` | §2.30 | Web Audio: skip unchanged `AudioParam` writes (platformer audio 0.148 → 0.029 ms/frame) |
| `ef5eb79b64` | docs | `-O2`/thin LTO measured on CPU time: no gain; native main thread 0.66 vs web wasm ~1.25 ms |
| `a87c84d4a5` | §2.30 | **Upstream Godot bug**: shadow atlas never stored the light version when a reallocation found no slot → every positional shadow redrawn every frame; heavy CPU −37% |
| `efcbbd4a7c` | §2.30 | `API_TRAIT_UNIFORM_SET_PREPARE_FOR_USE` (no-op prepares skipped on WebGPU); 4-entry rebind-cache front |
| `e03ea2e182`, `574594617c` | §2.5 | Function-by-function web vs native comparison; threading investigation, `threading_proto/`, design |
| `55e1e5c450` | §2.5 | **Separate render thread** for `threads=yes` + `thread_model=2`: own pthread, own device, OffscreenCanvas, `Atomics.waitAsync` both ways, `MessageChannel` yield, `RenderingServer::sync_previous_draw()`, worker-WebGPU probe + Safe fallback |
| `16a69c7728` | §2.30 | PR godotengine/godot#123996 (sample audio driver rewrite) cherry-picked; the skip-unchanged cache re-applied on it |
| `db437ca832` | §2.5 | Render-thread shutdown made synchronous (`emscripten_proxy_sync`); `requestQuit()` clean |
| `135a0f542e`, `2c3544102e` | docs | Before/after table (7 scenes × 5 variants) in §2.5 and `README.md` with percentage changes |

#### What landed 2026-10-15, in order

| commit | item | what it did |
|---|---|---|
| `dff4c12811` | §2.28 | `command_end_compute_pass()` now ends the active compute encoder — each compute list is its own pass / sync scope. Forward+ matrix 64/64 after |
| `24419e3238`, `9c9cc1a67d`, `37a7b4359b` | §2.6 | Frame stats under load: Forward Mobile benchmarks, a Forward+ run, then a new heavy Forward+ scene (`benchmark/godot/scene_i_forward_plus_heavy`, `forward_plus/export_heavy.sh`): 3549 draws/frame, `ringOverflows: 1`, `rwShadowRefreshes: 40` |
| `4d407aacc4` | §2.11 | **Real bug**: `command_copy_buffer_to_texture` ignored `texture_offset.z` and depth for 3D textures → VoxelGI lighting was missing the directional light. VoxelGI now 7.743 vs Vulkan 7.742; also `run_forward_plus.mjs --save-dir` |
| `78c5d49b46` | tests | `test_font_visual.mjs` retries until the first frame is presented (Firefox raced READY) |
| `a31e2bcff3` | §2.27 | Re-measured: baking now **wins** at 20 Mbit/gzip (12.9 s vs 17.7 s; pck gz 11.2 MB). `SMOKETEST_BAKE=1` for baked exports. Pruning variants judged unsafe without an opt-in |
| `b78bf7bb2f` | §2.29 | New task: make the baked export a real test tier |
| `23246cb663` | §2.4 | Baked stage loop 391 → ~180 ms by removing per-stage whole-text rescans (`_remap_unsupported_wgsl_storage_formats`, unbounded `strstr` in the read/write-split loops) |

### Build and environment state — read before rebuilding

- **The renderer depends on the adapter.** `RendererCompositorRD::initialize()` falls back to Forward
  Mobile below 48 textures per shader stage, and a software adapter (swiftshader, `CI=1`) reports 16.
  So a container run proves the engine boots and loads scenes; it **never** proves Forward+ renders,
  and nothing renderer-dependent can be validated there. Byte counts and call counts from such a run
  are still good; timings and renderer behavior are not.
- **A scratch project needs `renderer/rendering_method.web="forward_plus"`**, or the export picks
  `opengl3` and no WebGPU code runs at all.
- **The editor and the web template must be built from the same commit.** Baked shader caches are
  keyed to `GODOT_VERSION_HASH`; rebuild one and the whole baked cache goes unreachable. The engine
  says so outright — "This export ships a baked shader cache, but none of it matches what this build
  asks for" (`shader_rd.cpp:756`) — alongside `{baked: 0, translated: N}` and a ~3.5× slower load,
  which otherwise reads as a performance regression in whatever was just changed. Cost of ignoring it,
  measured: a 150.8 MB pck that buys nothing. Rebuild both, or neither, then re-export. Build the **web one last**.
  Incremental cost on this machine: editor ~30 s, template ~47 s.
- **Since 2026-10-17 the web export ships its baked shaders as `index.shaders.pck`** (preset
  `shader_baker/separate_pack`, default on, §4.23). The shell has to preload it (`config.shaderPack`,
  `engine.js`) and the engine mounts `<main pack>.shaders.pck` itself (`ProjectSettings::_setup`), so a
  template built before that change starts the game without its shaders — not a hash mismatch this time,
  the cache directory is simply absent, so it is silent apart from the slow start. The exporter warns when
  the extracted `index.js` lacks `shaderPack`; the fix is the same: rebuild the templates.
- **`~/emsdk` must carry both patches in `misc/emsdk_patches/`** for any `dlink_enabled=yes` build
  (`apply.sh --apply`, `--status`, `--revert`). `emsdk install` silently reverts them. Every build
  helper and both web CI workflows apply them automatically. scons relinks when the toolchain moves
  (`get_toolchain_js_fingerprint()`), so nothing needs deleting by hand.
- **Interleaving editor and web builds in one tree ships a template that dies in `callMain()`** with
  `TypeError: resolved is not a function` (§2.19 — diagnosed, not fixed). Workaround:
  `rm -f bin/obj/modules/register_module_types.gen.<platform>.<target>.*.o bin/obj/modules/libmodules.<...>.a`.
  Check with `grep -ac initialize_betsy_module bin/godot.side.web.*.wasm` (want 0).
- **An export made with `--headless` silently skips the shader baker** (15 MB pck instead of 135 MB).
  Use `xvfb-run --rendering-driver vulkan` (lavapipe is enough, no GPU needed).
- **A stale `.godot` in a demo project kills its export** with `ERROR: Parameter "singleton" is null.
  at: is_cmdline_mode (editor_node.cpp:6622)` + `Aborted`, which reads like an engine crash.
  `rm -rf <project>/.godot`.
- `godot-demo-projects` is cloned at `/mnt/109313D2109313D2/godot-editors/godot-demo-projects`; the
  smoketest expects exactly that path, and 11 scenes skip without it.
- Threaded builds need COOP/COEP cross-origin-isolation headers; the `perf_guard` tools and the scratch
  servers send them now, the scene smoketest server does not (it exports nothreads).
- A `threads=yes` export renders on the main thread (Safe model) unless the project sets
  `rendering/driver/threads/thread_model=2`; then `DisplayServerWeb` runs the render thread (§2.5). Chrome's
  `--disable-frame-rate-limit --disable-gpu-vsync` drive such a page's rAF at ~60 Hz, so uncapped fps is
  not a usable metric in that mode — measure per-thread ms at the cap.
- Never run two `scons` invocations in one tree at once — not even a no-op read or a timing probe. They share
  `.sconsign5.dblite`, so an overlap can lose a run's recorded signatures. The worse half — an object compiled
  against another variant's generated header and then "up to date" forever — is fixed since §4.25: each variant
  writes its own copy of the option-dependent generated sources under `gen/<variant>/` (§3 trap 24, §4.22).
  `pgrep -ax scons` first.
- The user's real test project is `~/Downloads/cameraSim_*/testing`, not `webgpu_tests/test_project`.

### Tools

```bash
# Startup / perf profiling (no engine rebuild; works on any existing export)
node cpu_profile.mjs --dir <export-dir>   # per-function CPU up to first frame; needs a debug_symbols=yes template
cd webgpu_tests/startup_phases
node profile_phases.mjs --dir <export-dir> [--browser firefox] [--warm] [--args --benchmark]
                        [--shader-errors] [--eager-pipelines] [--flush-every-mb N] [--no-console]
node features.mjs                       # WebGPU adapter feature matrix, Chrome vs Firefox
node shot.mjs <export-dir> <out.png> <wait-ms> [chromium|firefox]
node trace_block.mjs --dir <export-dir> # Chrome GPU trace around the longest block

# Scene smoketest
cd webgpu_tests/scene_smoketest
GODOT_EDITOR_BIN=../../bin/godot.linuxbsd.editor.x86_64 \
GODOT_TEMPLATE_ZIP=../../bin/godot.web.template_release.wasm32.nothreads.zip \
  node run_scenes.mjs --export-only
node run_scenes.mjs --browser firefox|chrome [--scenes <name> --export]
node capture_errors.mjs exports/<scene> [--browser firefox] [--wait 25000]  # full Dawn errors

# SPIR-V / WGSL investigation
GODOT_DUMP_SPIRV=/tmp/spv ./bin/godot.linuxbsd.editor.x86_64 --path <proj> --quit-after 120
WGSL_DEBUG_DUMP_PREPROCESSED=/tmp/out.spv ./bin/wgsl_convert_cli <file.spv>   # --spec-info, --raw: see CLAUDE.md
WGSL_DEBUG_DUMP=<substring> drivers/webgpu/wgsl_precompile.py   # dumps raw SPIR-V to /tmp/wgsl_debug_dump/
```

`capture_errors.mjs` and `run_scenes.mjs` share three Chrome modes: `WEBGPU_REAL_GPU=1`, `CI=1`
(bundled Chromium on swiftshader — **this is the CI adapter**, use it to reproduce a CI-only failure
locally instead of pushing), or neither (the system's headed Chrome, which is what most recorded
results were measured on). Playwright cannot drive the user's own Firefox build.

---

## 2. Outstanding work — most to least critical

**Added 2026-10-17 (§4.29):** Forward+ now matches native Vulkan on every matrix feature; the sweep's follow-ups are at the end of §2.31 (omni cube shadow cost not re-measured, `POINT_COORD` bug to report upstream). Judge any renderer change with `run_forward_plus.mjs --parity`.

**Added 2026-10-23:** §2.34 opens the ray / path tracing investigation. WebGPU has no hardware ray tracing and none is scheduled, so it is a software (compute-shader BVH) design; the first step was a measurement, `webgpu_tests/raytracing_probe/`: on the 4080 SUPER a WGSL BVH traversal does 1.9–2.3 Grays/s incoherent and 3.7–5.3 Grays/s coherent at 1080p, 3–4× over the gate for full-resolution RT shadows/AO — so Tier A (RT AO through the SSAO texture, then RT shadows) is the next thing to build. The Razr 60 does 19 Mrays/s incoherent (100× less): RT is a desktop feature, off on mobile.

**Added 2026-10-25:** §2.35 evaluates [Ludicon/spirv-cross-wgsl](https://github.com/Ludicon/spirv-cross-wgsl), a SPIRV-Cross fork with a WGSL backend, as a replacement for Tint. Fed our preprocessed SPIR-V it emits all 276 engine variants with the same bindings and override ids as Tint, 275 pass Chrome's compiler (FSR2's luminance pyramid fails a barrier-uniformity check), and it converts about 10× faster. Not yet checked for rendering parity. The swap was started right after (tag `pre-SPIR-V-Cross` marks the tree before it).

**Added 2026-10-25 (evening):** §2.37 (replay each render pass from one JS call) and §2.21 (the four deferred emdawnwebgpu features) are prioritized by the user, after alternatives to emdawnwebgpu (juj's wasm_webgpu, Rust wgpu, Emscripten's removed `USE_WEBGPU`) were evaluated and none was better: the glue is 8–10% of main-thread CPU, and emdawnwebgpu's `webgpu.h` is the API native Dawn shares.

**Added 2026-10-25 (later):** §2.36, requested by the user: the web export should keep the screen awake (Wake Lock), ask for high performance, and lock to the orientation set in the export panel, with a non-PWA orientation option. Today only the PWA manifest carries an orientation and `keep_screen_on` does nothing on the web.

**Added 2026-10-25 (night):** the Tint → SPIRV-Cross swap is done (§2.35 → §4.36), and so is §2.36 (→ §4.37). The swap left seven small items, none of them a regression it caused: §2.38 (move the converter smoke check out of the work board before it retires), §2.39 (the baked fixture's 97 runtime stages), §2.40 (Firefox-only SDFGI gap), §2.41 (SSS on the phone), §2.42 (FSR2's blank depth fallback), §2.43 (swizzled views), §2.44 (an invalid spec-constant probe). §3 traps 55–65 are what the swap cost.

**Added 2026-10-26:** §2.45 (occlusion culling on the web) opened and closed the same day (→ §4.38). The Forward+ matrix grew to 82 arms (§4.39), the README was rewritten and the wiki published (§4.40), and the load-ramp benchmark (end of §2.30) replaced §2.17's fps matrix as the performance source; its four open questions (fill 0.67x, particles 0.31x native, how to quote lit, Firefox spread) are listed there. §3 traps 66–71 are the day's. Work tasks 24–27 (white balance, MetaHuman skin, the strand NaN / DOF hexagon, FSR 2's uniform mismatch on web exports) went out the same day; 26 closed as §4.41, 24 as §4.42, 27 as §4.43, 25 is recorded in §2.33. Tasks 28 (NaN guards for glow, SSR, auto exposure, §4.44) and 29 (white balance units, §4.42) followed from the user's answers; 30 (§4.45) points the editor's update check at this fork's releases. The full matrix is 87 arms; the bake gate's ceiling went 130 → 150 (§2.39).

**Added 2026-10-26 (evening):** tasks 24–30 are all merged, and the 87-arm matrix is 87/87 at parity unbaked and baked on the merged tree (§1). They left two small open items: §2.46 (MetaHuman skin follow-ups: the cloth shader's normal z, pores on the lips, the WebGPU after-capture that hung, the user's look) and §2.47 (the update check's blind spot and the two user questions it waits on). §3 traps 70–73 are tasks 25–27's.

**Pick up here (written 2026-10-16, end of the render-thread day).** Nothing is broken. In rough value order, all non-human:
1. **§2.5 follow-ups** — `perf_guard`/`cpu_steady` cannot see the render thread (attach the CDP profiler to the worker, as the scratch `thrprof.mjs` did); the uncapped-fps artifact; decide whether Separate should become the default for threaded exports (needs more real projects, a long soak, and Safari/mobile which have never run). **§2.30** is mostly done: what is left is the JS marshaling path and command batching, both small or a redesign.
2. **§2.16** remaining test gaps (regression corpus, error-path, async readback tests).
3. **§2.20 / §2.21** (half-float, emdawnwebgpu features) — each needs a verification pass and a real GPU; risky, do one at a time.
4. **§2.10 / §2.14** more coverage: more real projects through the live sweep, the dlink export of `cameraSim`, a native-Vulkan side-by-side.
5. **§2.27** variant pruning needs an opt-in design first.
Blocked/human: §2.7, §2.8 (Mac), §2.9 (devices), §2.18, §2.24, §2.25, §2.26.
**Before measuring anything:** `bin/` holds `dev_mode=yes` builds that do not match HEAD — rebuild editor and template together with `webgpu=yes` (no `dev_mode` for benchmarks),
check `nvidia-smi` and the adapter (nvidia/lovelace, >= 48 textures/stage), launch Chrome only the way the scripts now do (system Chrome, `--enable-unsafe-webgpu --enable-features=Vulkan --use-vulkan --ignore-gpu-blocklist`), and re-export (§3 "Measurement traps").

**Numbers are stable.** A closed item keeps its number, is marked `CLOSED` here and points into §4,
so references from source comments and from this file stay valid. Re-order by moving rows, not by
renumbering.

**Status reconciled 2026-10-15** (2.6, 2.14-2.17, 2.19, 2.22, 2.29 updated after the real-GPU session; the rest as of 2026-10-14). Every row below was checked against the tree
on that date; anything with no dated note is untouched since it was first written.

**Every item at a glance (2026-10-15, end of the real-GPU / CPU-profiling session, after `bc9f4d3768`).** Rows marked ✔ were touched on 2026-10-15; the rest are as of 2026-10-14.

| § | item | status | what is left |
|---|---|---|---|
| 2.1 | bind-group layout mismatch | CLOSED | — |
| 2.2 | non-dlink export abort | CLOSED | — |
| 2.3 | dlink under throttling | CLOSED | — |
| 2.27 | baked containers are 79% of the download | **DONE 2026-10-20** — automatic variant pruning + lazily loaded shader extras: platformer startup shaders 10.7 → 1.84 MB gzipped | a real-GPU Forward+ run; WGSL compression in containers (optional) |
| 2.4 | startup fixed floor | **ROOT-CAUSED 2026-10-20** — GPU process compiling unused compute pipelines and shader modules while the startup waited on frames; both now created on first use (callMain → first frame 6.8 → 2.8 s, swiftshader), deferred compute pipelines prewarmed, background render pipelines compiled asynchronously (real GPU: sync render pipelines after the first frame 1.35 → 0.17 s, no stall when new surfaces come into view) | synchronous `createShaderModule` (0.3-0.7 s after the first frame) and the compute pipelines frame 1 binds (~0.44 s) |
| 2.5 ✔ | threading decision | **IMPLEMENTED 2026-10-16**, open for tooling — separate render thread for `threads=yes` + `thread_model=2` (Chrome + Firefox, dlink too); main thread −57% (platformer) to −97% (heavy) per frame; `requestQuit()` clean | `perf_guard` tools see only the main thread under Separate; uncapped fps unmeasurable there (Chrome rAF ~60 Hz with a worker-owned canvas); Safari/mobile never run |
| 2.6 ✔ | real-GPU profile | DONE 2026-10-15 (baked heavy: `translated 0`, stall 18.5 -> 7.7 s) | `rwShadowRefreshes` 40 -> 4/frame on 2026-10-16: they were a detection bug, not inherent (§2.30) |
| 2.7 | upstream F10 default | OPEN, human | — |
| 2.8 | Safari | BLOCKED | needs a Mac |
| 2.9 | Android / iOS | Android measured 2026-10-17; iOS BLOCKED | §2.9 findings; iOS needs a device |
| 2.10 | storage-format-by-variant audit | OPEN (tables clean) | coverage |
| 2.11 | VoxelGI 1.6× Vulkan | CLOSED (7.743 vs 7.742) | — |
| 2.12 | dead `_depth_alias` | CLOSED | — |
| 2.13 | stale Phase 7 TODOs | CLOSED | — |
| 2.14 ✔ | base-class interface audit | OPEN (audit done; cameraSim swept clean 2026-10-15) | more real projects, dlink export, native side-by-side |
| 2.28 | compute lists share one encoder | FIXED (end hook) | optional per-bind bookkeeping cleanup (the backstop is still needed within a list) |
| 2.29 ✔ | baked export as a test tier | DONE 2026-10-15 (matrix, smoketest, WebRTC legs; deltas re-baselined) | `ambient_light` bimodal on unbaked, not chased |
| 2.30 ✔ | unbottleneck the CPU (new) | OPEN, mostly done — 2026-10-16: SSIL hoist pass, read-write storage textures, upstream shadow-atlas bug (heavy CPU −37%), prepare-for-use trait, Web Audio writes, PR #123996; heavy GPU 3.40 → 1.46 ms, 229 → 292 fps; engine wasm CPU is flat (per-function comparison, `-O2`/LTO no gain) | the JS marshaling path (~0.5 ms/frame ceiling 0.02-0.05 ms), batching a pass's commands into one JS call (driver redesign) |
| 2.31 | Forward+ gaps WebGPU cannot close (new, 2026-10-17) | OPEN, spec-limited — parity 87/87 otherwise (2026-10-26) | re-measure omni cube shadow cost, report the `POINT_COORD` bug upstream, trace the 4x4 depth placeholder message, a lightmap/VoxelGI array test |
| 2.32 | Sample playback's remaining gaps: bus effects, streaming (new, 2026-10-18) | OPEN, deferred by decision — warned about at export and at runtime | pick an approach (auto-route to Stream, Web Audio effect equivalents, or both); options and trade-offs in §2.32 |
| 2.33 | MetaHuman importer: DNA rig, clothing, hair (new, 2026-10-20) | DONE 2026-10-20 — rig, clothing, groom strands, baked shell, guide simulation; Ava with dynamic hair 1.52 ms/frame uncapped in WebGPU, 0.43 ms GPU | measure on an M1; wrinkle masks (not exported); skin-pore follow-ups in §2.46 |
| 2.35 | SPIRV-Cross WGSL backend instead of Tint (new, 2026-10-25) | **DONE 2026-10-25 → §4.36** — Tint deleted; parity 71/71, Firefox/Android/desktop as fast or faster, cameraSim matches | follow-ups §2.38–2.44 |
| 2.36 | Web export: performance hint, keep screen awake, orientation from the export panel (new, 2026-10-25) | **DONE 2026-10-25 → §4.37** — Wake Lock, `html/orientation`, fullscreen on the first tap, high-performance everywhere | a feature-tag orientation override still beats `html/orientation` |
| 2.37 | Record each render pass in wasm memory, replay in one JS call (new, 2026-10-25) | OPEN — prioritized by the user | ~7% of main-thread CPU on desktop heavy_i, more on phones; measure on the Razr 60 first |
| 2.38 | Converter smoke check and stored references out of the work board (new, 2026-10-25) | OPEN — before the board retires | move `smoke.sh` into `webgpu_tests/`, wire it into `local_ci.sh`/CI; first GitHub run of the renamed CI jobs |
| 2.39 | Baked Forward+ fixture still converts ~97 stages at load (new, 2026-10-25) | OPEN — pre-existing, every converter | the bake gate passes only at a raised ceiling (150, was 80); 8 scene-shader groups and a particles stage are never baked |
| 2.40 | SDFGI differs from native in Firefox only (new, 2026-10-25) | OPEN — 0.277 vs tolerance 0.08, Tint and SPIRV-Cross alike | find the Firefox/wgpu pass |
| 2.41 | Subsurface scattering on the Razr 60 is 11.1 from desktop Vulkan (new, 2026-10-25) | OPEN — pre-existing, both converters | phone-only; bisect like 0019 |
| 2.42 | FSR2 accumulate and scene set 1 binding 24 get the blank depth fallback (new, 2026-10-25) | DONE 2026-10-28 → §4.74 | the fallback is never read |
| 2.43 | Audit swizzled texture views — WebGPU drops the swizzle silently (new, 2026-10-25) | DONE 2026-10-28 → §4.73 | `texture_storage.cpp`'s image-format swizzles, any other `texture_create_shared()` with a swizzle |
| 2.44 | `run_spec_ops.sh`'s `op_FConvert_f32` probe is invalid SPIR-V (new, 2026-10-25) | DONE 2026-10-28 → §4.72 | probe dropped |
| 2.45 | Occlusion culling on the web: the raycast module in web templates (new, 2026-10-26) | **DONE 2026-10-26 → §4.38** — on by default, Web export option `rendering/occlusion_culling` | upstream the nothreads culler fix (§3 trap 69) |
| 2.46 | MetaHuman skin follow-ups after the pore fix (new, 2026-10-26) | OPEN — the pore scale itself is fixed (§2.33) | cloth shader reads RG normals' blue as z (§3 trap 72); lips get pores (no region masks exported); the WebGPU after-capture of the skin hung twice; the user's look `look-skin-pores` |
| 2.47 | Editor update check: blind spot and two pending user questions (new, 2026-10-26) | OPEN — the check itself is done (§4.45) | answers to `update-newer-rule` and `update-repo` on the hand-offs page; a build from a branch committed after a release it lacks is not told |
| 2.48 | Survey: post-processing effects Godot lacks (new, 2026-10-28, user request) | SURVEYED (task 41) — waiting on the `pp-*` answers | gap list against Unreal / Unity HDRP / Blender, excluding the camera batch (work tasks 33–38); ranked with effort and web cost, for the user to pick from |
| 2.49 | Web loading screen: progress bar toggle and colors (new, 2026-10-28, user request) | **DONE 2026-10-28 → §4.56** |
| 2.51 | Web export: a Start button and "preload before run" (new, 2026-10-28, user request) | DONE 2026-10-28 → §4.61, §4.63 | export options; the Start button's look customizable (border color/radius, font family/color, cursor, hover/press darkening) |
| 2.52 | Web: `DisplayServer.window_set_size()` never emits `size_changed` (new, 2026-10-28, camera-sim) | DONE 2026-10-28 → §4.62 | canvas resized synchronously in `godot_js_display_desired_size_set()`, so `process_events()` sees no change and the viewport/surface keep the old size |
| 2.53 | Web: fullscreen driven by the embedding page (new, 2026-10-28, camera-sim) | DONE 2026-10-28 → §4.67 | `postMessage` protocol (`godot:fullscreen` / `ready` / `exit-fullscreen`), export options, allowed origins |
| 2.50 | Build scripts: the no-threads, no-dlink web templates first (new, 2026-10-28, user request) | **DONE 2026-10-28 → §4.60** | build and install `template_debug` + `template_release` with `threads=no dlink_enabled=no` right after the editor, before every other template | export options: an on/off toggle for the bar (default on) and, while on, its border, background and fill colors, in the default shells and `$GODOT_LOADING_SCREEN` |
| 2.15 ✔ | `generate_lods()` returns 0 on web | CLOSED 2026-10-15 (GPU readback returns zeros, §2.13; not LOD code) | — |
| 2.16 ✔ | test-suite gaps | OPEN (gap 4 partly via `perf_guard`; gaps 5, 7 closed) | regression corpus, error-path, async readback, frame-time/compile/texture perf guards, Safari |
| 2.17 ✔ | Forward+ benchmark leg / README numbers | DONE 2026-10-15 (capped + corrected uncapped matrices; F+ vs Mobile profiled; heavy scene 176 -> ~220 fps via shadow-variant batching, `firstInstance`, mesh-first order) | superseded 2026-10-26 by the load-ramp benchmark (end of §2.30), which measures WebGL without the display cap |
| 2.18 | texture compression export option | **DONE 2026-10-20** — per-family texture packs, the browser fetches only the one it samples | Safari/iOS and an Android device run |
| 2.19 ✔ | interleaved-build stale object | OPEN (not reproduced on 4.8, 2026-10-15) | wait for a recurrence; capture `--debug=explain` |
| 2.20 | `SUPPORTS_HALF_FLOAT` experiment | OPEN | — |
| 2.21 | deferred emdawnwebgpu features | OPEN — prioritized by the user 2026-10-25 | — |
| 2.22 ✔ | small perf items | DONE 2026-10-15 (INITIAL_MEMORY: 0.1 ms of grows, not a lever; serve-compressed documented) | side-module size is module-level only |
| 2.23 | Box3D remainders | OPEN (`precision=double` builds) | — |
| 2.24 | upstream the driver | OPEN, human | — |
| 2.25 | side project (Node server) | not started | — |
| 2.26 ✔ | braced GDScript (`.gdb`) | DONE 2026-10-18, 1.0 (tag `gdb-1.0`, §4.33) | hand tests in Rider and the editor |

| what happened | items | commits |
|---|---|---|
| **Closed** | §2.1 bind-group layout mismatch (fixed), §2.2 non-dlink abort (gone), §2.3 dlink under throttling (measured, premise refuted), §2.12 dead `_depth_alias` (deleted), §2.13 stale Phase 7 TODOs (all three answered) | `8cdfb073e`, `b62681b3e`, `67d3b53bf`, `9622f9c12`, `fa954729c` |
| **Landed, still open** | §2.27 — multiview SPIR-V no longer baked: pck 150.76 → 62.36 MB (−59%) | `9aabb1e73`, `6c5d83a57`, `b0be6b752` |
| **Opened** | §2.27 (from §2.3's measurement), §2.28 (from §2.14's audit), §2.29 (baked tier) | `325b8d297`, `d5206c11c`, `b78bf7bb2` |
| **Closed later the same day** | §2.11 VoxelGI (a real 3D-upload bug), §2.28 (compute pass end), §2.6 frame-stats half | `4d407aacc`, `dff4c1281`, `37a7b4359` |
| **Advanced, still open** | §2.4 (stage loop 391 → ~180 ms), §2.27 (baking now wins; pruning deferred) | `23246cb66`, `a31e2bcff` |
| **Audited, narrowed on evidence** | §2.6 (half measured), §2.10 (tables clean; reframed as coverage), §2.14 (interface audit done, baseline `9973b23ec7`), §2.23 (`precision=double` builds and runs) | `9973b23ec`, `d5206c11c`, `0f0b1025b` |
| **Landed after the snapshot** | §2.29 baked tier — Forward+ matrix leg and scene-smoketest leg (WebRTC leg still open); §2.6 push-constant ring 512 KB → 2 MB (`ringOverflows` 1 → 0); §2.16 gap 5 WGSL determinism closed, plus a cwd-dependent include bug in `wgsl_precompile.py` | `64de4bda4`, `231e6ca1e`, `af3d57ddd`, `597d27e09` |
| **Description updated with new numbers** | §2.4, §2.16 (a seventh gap), §2.22 | `325b8d297`, `67d3b53bf` |

| **2026-10-15 — closed** | §2.6 (baked heavy), §2.15 (readback zeros), §2.17 (benchmarks + post-fx fix), §2.22 (INITIAL_MEMORY), §2.29 (baked tier complete) | `065997c38`, `cfb2063c0`, `740e45e42`, `ea640a65d`, `1f3280103` |
| **2026-10-16 — §2.5 implemented, §2.30 mostly done** | §2.5 (separate web render thread, before/after table), §2.30 (SSIL, rw storage textures, shadow-atlas bug, audio, PR #123996), §3 trap 21 | `f44c233f2b` … `2c3544102e` (see §1 "What landed 2026-10-16") |
| **2026-10-15 — advanced, still open** | §2.14 (first live project), §2.16 (`perf_guard`), §2.19 (not reproduced), §2.5 (data), new §2.30 | `5224fdb1e`, `620638b19`, `ab67f470e`, `272a28566`, `d1683048b`, `bc9f4d376` |
| **2026-10-15 — driver/renderer changes** | per-pass encoder split removed; `buffer_flush_range` (instance + canvas buffers); staging uploads coalesced; Forward+ `firstInstance`; shadow lists batched by shadow variant; opaque list mesh-first on WebGPU | see §2.17 and `drivers/webgpu/README.md` |
| **2026-10-15 — new tests/tools** | `perf_guard/run_perf_guard.mjs` (+`api_profile.mjs`, `cpu_steady.mjs`), WebRTC baked variant, `BAKE=1 export_heavy.sh`, `run_matrix.sh`, `BENCH_UNCAPPED`, `scene_j_forward_plus_depth`, startup_phases/tests launch system Chrome with the standard flags | — |

Nothing was pushed; every commit above is local on `webgpu-4.8`.

**Where to pick up (written 2026-10-14, end of day).** Nothing is half-done in the tree. In order of
value for effort:

1. **§2.29 — remaining legs:** the WebRTC fixture export in `local_ci.sh --bake`, and re-baselining baked-vs-unbaked deltas.
2. **§2.16 gaps 1-4:** regression corpus (needs a decision on which fixed bugs count), error-path tests, async-readback tests, perf-in-CI guards.
3. **§2.6 remainder:** a *baked* heavy Forward+ export (`translated` check) and `rwShadowRefreshes: 40`/frame.
4. **§2.14's live-project sweep** (~1 session).
5. **§2.28 follow-up** (optional, behavior-neutral): per-bind texture bookkeeping; the backstop stays.

§2.4 is effectively done (tail attributed, no dominant term) and is no longer on this list.

Not worth doing yet: §2.27's variant pruning (unsafe without an opt-in design — see its section),
WGSL compression in containers (the download is already 11 MB gzipped), §2.5 threading (needs a decision
and a real GPU). Human-only: §2.7, §2.8, §2.9, §2.24.

**Known environment traps hit today** (full text in §3): pixel tests need `WEBGPU_REAL_GPU=1` (trap 1);
`local_ci.sh`'s `ensure_playwright` can install a Playwright whose browsers are not installed and make a
whole tier SKIP (trap 3a); a baked re-export needs the editor rebuilt with the template (trap 3a).

| # | item | status | effort (Opus 5, medium) |
|---|---|---|---|
| 2.1 | ~~Bind-group layout mismatch — rare residual~~ | **`CLOSED` 2026-10-14 → §4.19** | — |
| 2.2 | ~~Confirm the non-dlink export abort is gone on 6.0.9~~ | **`CLOSED` 2026-10-14 → §4.5** | — |
| 2.3 | ~~Profile a dlink export under network throttling~~ | **`CLOSED` 2026-10-14 → §4.15** | — |
| 2.27 | Baked containers: multiview SPIR-V dropped (−59%), then unreachable groups pruned and unverified ones deferred to lazily loaded extras (startup shaders −83% gzipped) | `DONE` 2026-10-20 | real-GPU check only |
| 2.4 | Startup's fixed floor: `Servers:Rendering` + first frame | `ROOT-CAUSED` 2026-10-20 — lazy shader modules and compute pipelines in the driver (−59% callMain → first frame), compute prewarm, asynchronous background render pipelines; what is left is main-thread CPU (a third of it V8 lazy compilation) and synchronous shader modules on the GPU process | shader-module creation spread over frames, if the first-frame hitch matters |
| 2.5 | Decide whether threading is worth having | `IMPLEMENTED 2026-10-16` (open: perf tooling for the render thread, Safari/mobile) | done; tooling ~1 session |
| 2.6 ✔ | real-GPU profile | DONE 2026-10-15 (baked heavy: `translated 0`, stall 18.5 -> 7.7 s) | `rwShadowRefreshes` 40 -> 4/frame on 2026-10-16: they were a detection bug, not inherent (§2.30) |
| 2.7 | Offer F10's default change upstream | `OPEN` | **human** |
| 2.8 | Safari has never been run | `BLOCKED` | **human, needs a Mac** |
| 2.9 | Android / iOS browsers | `OPEN` — Android profiled 2026-10-17 (GPU-bound at 1080p), iOS never run | engine-side follow-ups listed in §2.9 |
| 2.10 | Storage-format class: tables audited clean; binding half open | `OPEN` — audited 2026-10-14 | coverage, not a session |
| 2.11 | ~~VoxelGI measures 1.6× native Vulkan~~ — real 3D-upload bug, fixed | **`CLOSED` 2026-10-14** | — |
| 2.12 | ~~Delete dead `_depth_alias`~~; reverse fallback deliberately not added | **`CLOSED` 2026-10-14 → §4.12** | — |
| 2.13 | ~~Stale Phase 7 TODOs: 7.8, 7.15, 7.17~~ | **`CLOSED` 2026-10-14 → §4.2** | — |
| 2.14 ✔ | base-class interface audit | OPEN (audit done; cameraSim swept clean 2026-10-15) | more real projects, dlink export, native side-by-side |
| 2.28 | Compute lists share one encoder: now `command_end_compute_pass` ends the pass | `FIXED` 2026-10-14, Forward+ matrix 64/64; optional per-bind bookkeeping cleanup not done (backstop still needed) | — |
| 2.29 ✔ | baked export as a test tier | DONE 2026-10-15 (matrix, smoketest, WebRTC legs; deltas re-baselined) | `ambient_light` bimodal on unbaked, not chased |
| 2.15 ✔ | `generate_lods()` returns 0 on web | CLOSED 2026-10-15 (GPU readback returns zeros, §2.13; not LOD code) | — |
| 2.16 ✔ | test-suite gaps | OPEN (gap 4 partly via `perf_guard`; gaps 5, 7 closed) | regression corpus, error-path, async readback, frame-time/compile/texture perf guards, Safari |
| 2.17 ✔ | Forward+ benchmark leg / README numbers | DONE 2026-10-15 (capped + corrected uncapped matrices; F+ vs Mobile profiled; heavy scene 176 -> ~220 fps via shadow-variant batching, `firstInstance`, mesh-first order) | superseded 2026-10-26 by the load-ramp benchmark (end of §2.30), which measures WebGL without the display cap |
| 2.18 | Texture compression as an export option: per-family texture packs | `DONE` 2026-10-20 | device runs (Safari/iOS, Android) |
| 2.19 ✔ | interleaved-build stale object | OPEN (not reproduced on 4.8, 2026-10-15) | wait for a recurrence; capture `--debug=explain` |
| 2.20 | `SUPPORTS_HALF_FLOAT` experiment | `OPEN` | 1-2 sessions, needs real GPU |
| 2.21 | Deferred emdawnwebgpu feature adoption (4 items) | `OPEN` — prioritized by the user 2026-10-25 | ~1 session each |
| 2.22 ✔ | small perf items | DONE 2026-10-15 (INITIAL_MEMORY: 0.1 ms of grows, not a lever; serve-compressed documented) | side-module size is module-level only |
| 2.23 | Box3D remainders: `precision=double` checked; gaps deliberate | `OPEN` — checked 2026-10-14 | open-ended gaps only |
| 2.24 | Upstream contribution of the driver | `OPEN` | **human** |
| 2.25 | Side project: Node dedicated-server backend | not started | weeks, **human-scoped** |
| 2.26 ✔ | Braced GDScript (`.gdb`) + editor plugins | DONE 2026-10-18 — 1.0, merged into `webgpu-4.8` (tag `gdb-1.0`); §4.33 | hand tests in Rider and the editor |
| 2.31 | Forward+ gaps WebGPU cannot close (new, 2026-10-17) | OPEN, spec-limited — parity 87/87 otherwise (2026-10-26) | re-measure omni cube shadow cost, report the `POINT_COORD` bug upstream, trace the 4x4 depth placeholder message, a lightmap/VoxelGI array test |
| 2.32 | Sample playback's remaining gaps: bus effects, streaming (new, 2026-10-18) | OPEN, deferred by decision — warned about at export and at runtime | pick an approach (auto-route to Stream, Web Audio effect equivalents, or both); options and trade-offs in §2.32 |
| 2.33 | MetaHuman importer: DNA rig, clothing, hair (new, 2026-10-20) | DONE 2026-10-20 — rig, clothing, groom strands, baked shell, guide simulation; Ava with dynamic hair 1.52 ms/frame uncapped in WebGPU, 0.43 ms GPU | measure on an M1; wrinkle masks (not exported); skin-pore follow-ups in §2.46 |
| 2.35 | ~~SPIRV-Cross's WGSL backend instead of Tint~~ — swapped, Tint deleted | **`DONE` 2026-10-25 → §4.36** | — |
| 2.36 | ~~Web export: performance hint, keep screen awake, orientation~~ | **`DONE` 2026-10-25 → §4.37** | — |
| 2.37 | Record each render pass in wasm memory, replay in one JS call | `OPEN` — prioritized by the user 2026-10-25 | ~2–4 sessions |
| 2.38 | Converter smoke check and stored references out of the work board | `OPEN` | ~0.5 session |
| 2.39 | Baked Forward+ fixture converts ~97 stages at load (bake gate) | `OPEN` — pre-existing | ~1 session, needs the GPU |
| 2.40 | SDFGI differs from native in Firefox only (0.277) | `OPEN` | ~1 session, needs the GPU and Firefox |
| 2.41 | Subsurface scattering on the Razr 60, 11.1 from desktop Vulkan | `OPEN` — pre-existing | ~1 session, needs the phone |
| 2.42 | FSR2 accumulate / scene set 1 binding 24: blank depth fallback | `DONE` → §4.74 | — |
| 2.43 | Audit swizzled texture views (dropped silently on WebGPU) | `DONE` → §4.73 | — |
| 2.44 | `op_FConvert_f32` spec-constant probe is invalid SPIR-V | `DONE` → §4.72 | — |
| 2.45 | ~~Occlusion culling on the web: raycast module in web templates~~ | **`DONE` 2026-10-26 → §4.38** | — |
| 2.46 | MetaHuman skin follow-ups: cloth normal z, lip pores, WebGPU after-capture, user look | `OPEN` | ~0.5 session for the cloth fix; lip masks need source data; capture needs the GPU |
| 2.47 | Editor update check: blind spot, two pending user questions | `OPEN` — waits on the user | <0.5 session once answered |
| 2.48 | Survey: post-processing effects Godot lacks | `SURVEYED` — `pp-*` answers pending | ~0.5 session for the survey; each effect is its own task afterwards |
| 2.49 | ~~Web loading screen: progress bar toggle + colors~~ | **`DONE` 2026-10-28 → §4.56** | — |
| 2.50 | ~~Build scripts: no-threads non-dlink web templates first~~ | **`DONE` 2026-10-28 → §4.60** | — |
| 2.51 | Web export: Start button + preload before run, customizable button | `DONE` → §4.61, §4.63 | — |
| 2.52 | Web: `window_set_size()` never emits `size_changed` | `DONE` → §4.62 | — |
| 2.53 | Web: fullscreen driven by the embedding page | `DONE` → §4.67 | — |

### 2.1 Bind-group layout mismatch — rare residual `CLOSED 2026-10-14`

**Fixed.** A cache keyed on a pointer it did not own: `WGUniformSet::rebind_cache` is keyed on the
*target* shader's `WGPUBindGroupLayout`, and `shader_free()` releases those layouts, so a recycled
address made the cache return a bind group built for a dead layout. The Forward+ matrix is now
**64/64**. Details and evidence in §4.19.

### 2.2 Confirm the non-dlink export abort is gone on Emscripten 6.0.9 `CLOSED 2026-10-14`

**It is gone.** Verified on 6.0.9, both by loading the configuration and by reading the vendored
port. The result and its evidence are recorded in §4.5; nothing is left to do.

### 2.3 Profile a dlink export under network throttling `CLOSED 2026-10-14`

**Measured, and the premise was wrong.** The non-streaming compile of the side module costs **~50 ms**,
not seconds, so next to nothing is lost by it not streaming. dlink's real cost is bytes (+4% of a
realistic payload) and engine CPU (+12%). The preload mitigation is **refuted** — the connection is
never idle, so overlapping cannot speed up a bandwidth-bound load. The lever that does matter is
transport compression, worth 3.5×. Figures, and the far larger thing they turned up (§2.27), are in
§4.15.

### 2.27 Baked shader containers are 79% of the download `DONE 2026-10-20` — automatic variant pruning and lazily loaded shader extras *(found closing §2.3)*

**Landed 2026-10-20 — the pruning, made safe by deferring instead of dropping.** The objection below ("a pruned
variant that gets requested ... is a hard failure") turned out to be wrong for whole *groups*: a group's cache
file that is simply absent is an ordinary cache miss, and `ShaderRD::_load_from_cache()` falls back to compiling
the group from GLSL, as an unbaked export would. (It is only a container *present but stripped* that cannot be
used.) So pruning works at group granularity, and it now does, in three tiers:

1. **Groups the target can never select are not baked.** `ShaderRD::set_group_bake_requirements()` tags each
   lazily enabled group with what it needs (`ShaderRD::GroupBakeRequirement`); the baker evaluates the bits
   against the target's capability answers and the project. Dropped for WebGPU: Forward Mobile's FP16 groups
   (`use_fp16` is exactly `SUPPORTS_HALF_FLOAT`), the GI VRS group, three of the four fog groups (image atomics /
   Vulkan memory model), and every multiview group unless `xr/shaders/enabled`.
2. **Groups the export cannot verify go to the shader extras.** Forward+'s advanced group (motion vectors,
   separate specular, lightmaps, VoxelGI and SDF depth passes — ~70% of each material's bytes) is enabled only
   when a frame needs TAA, FSR 2, a LightmapGI, VoxelGI, SDFGI, subsurface scattering or a compositor effect
   needing motion vectors or separate specular. `_compute_unmet_group_requirements()` checks the project
   settings and scans every scene, resource, shader and script in the project for those features (property and
   API names as bytes, which also works on binary and compressed resources, since property names live in their
   string table). If nothing mentions any of them, the group is baked into `<name>.shaders_extra.bin` instead
   of the startup pack.
3. **The shell fetches the extras at low priority as the engine starts** (`Engine.loadShaderExtras()`), after
   every startup download has finished, and writes them to `/godot_shader_cache_extras.bin`; `ShaderRD` indexes
   that file on its next cache miss (`_open_from_extras()`). The extras are deliberately **not** mounted as a pack:
   `PackedData` has no lock, and the render thread may be opening shader cache files at the time.

Export option `shader_baker/variant_pruning` (`Disabled` / `Automatic`, default Automatic). Without a separate
shader pack (or with an encrypted pack) there is nowhere to put extras, and tier 2 drops the group instead. A
group the game needs before the extras arrive compiles from source (verbose log says so); one needed after they
arrived but missing from them (a wrong scan, e.g. a feature enabled from GDExtension code) warns once at runtime
with the fix. The decision is part of the customization hash, and `file_cache` re-shipping respects it.

Measured on `3d/platformer` (Forward+, nothing advanced), same editor and template:

| | startup shader pack | gzipped | extras (fetched after start) | gzipped |
|---|---|---|---|---|
| `variant_pruning` Disabled | 68.9 MB | 10.7 MB | — | — |
| Automatic | **14.5 MB** | **1.84 MB** | 51.4 MB | 8.7 MB |

So the gzipped download before the first frame drops by ~8.9 MB (−83% of the shaders); ~3 MB of unreachable
groups are no longer shipped at all. Verified in headless Chromium (swiftshader) with a scratch template that
skips the <48-textures Forward Mobile fallback, and a probe that enables TAA through a spelling the scan cannot
see: TAA turned on after startup → the extras were indexed on the first miss and `translated` stayed at 1 (an
unrelated octmap shader, below); TAA turned on in an autoload's `_ready()`, i.e. needed by the first frame →
the extras still arrived mid-startup (the engine yields between startup steps) and nothing compiled. Before the
fetch was moved from "after the first frame" to "as the engine starts", that last case compiled 482 stages,
because the first frame never finished to start the fetch.

**Not yet done:** a real-GPU Forward+ run (swiftshader cannot run Forward+ properly — CLAUDE.md), and the
Forward+ matrix with pruning on (its fixture builds materials in GDScript, so the scan keeps the advanced group
for any feature it toggles by name). **Found on the way, and fixed the same day:** `OctmapFilterRasterShaderRD`, `OctmapDownsamplerRasterShaderRD`,
`OctmapRoughnessRasterShaderRD` and `FsrUpscaleShaderRD` were never in the baked cache, so every export compiled them
at startup (`translated: 1`, four cache misses). Two causes, both "the editor's device decides what exists":
`FSR::FSR()` picked variant 0's define text from `SUPPORTS_HALF_FLOAT` at construction, so the Vulkan editor baked a
group the WebGPU runtime never asks for -- it now registers a general-defines refresh callback
(`FSR::_refresh_variant_defines()`), the same mechanism as the clustered SDF variant; and `CopyEffects` creates the
raster octmap shaders only when the device has fewer than six storage images per stage, which an editor never has
and a WebGPU adapter may (swiftshader, some phones), so they never existed to be baked -- its refresh callback
(`CopyEffects::_create_raster_octmap_versions_for_bake()`) now creates bake-only versions when a target override is
active. Platformer, forced Forward+: all four in `index.shaders.pck`, `translated: 0`, **zero** shader cache misses
during startup.

*(First cut landed 2026-10-14 — the numbers below are the original measurement; the pck is now
62.36 MB. See "Landed" at the end.)*

A baked export of `godot-demo-projects/3d/platformer` shipped an `index.pck` of **150.8 MB** (38.2 MB
gzipped); the same project exported without the baker is **1.09 MB**. So ~149.7 MB of it is baked
shader containers — and the run then consumes **9.0 MB** of WGSL out of them (`wgslBytes`, with
`baked: 599, translated: 0`). Roughly 16× more is shipped than any one run reads.

It is the single largest term in a real load, and it is big enough to invert the baker's own trade.
Same project, same build, 20 Mbit/s with gzip, to first presented frame:

| export | pck | payload | load | stall | first frame |
|---|---|---|---|---|---|
| **not** baked (`--headless`) | 1.09 MB | 11.9 MB | 6.2 s | 11.1 s | **17.5 s** |
| baked | 150.8 MB | 49.8 MB | 23.5 s | 2.3 s | **25.7 s** |

Baking does exactly what it claims to the CPU — the stall drops 11.1 s → 2.3 s, and
`containerCreateMs` 6615 → 442 ms — and still loses by 8 s, because it buys that with 37 MB of extra
download. Above ~100 Mbit it wins; on a typical link it does not. Note this cuts the other way for a
project whose shader set is small relative to its assets: the user's own project bakes to a 57.1 MB
pck (§4.15) and cold-loads in ~1.0 s, so the ratio, not the absolute size, is what decides.

Two directions, in order of expected value:

1. **Ship fewer variants.** Each container appears to carry WGSL for every variant of its shader
   while a run reaches one or two. The export already knows a lot about what the project can reach
   (SDFGI/VoxelGI/SSIL/volumetric fog on or off, MSAA, renderer), so pruning by project settings is
   the obvious first cut. This is the same class as §4.15's landed fix — stop shipping what will not
   be used — one level further in.
2. **Compress the containers in the pck.** WGSL is text and gzips ~4× (150.8 → 38.2 MB), but that
   only helps a host that is not already compressing the whole pck. Measure against 1 before doing it.

**Where it goes — measured 2026-10-14**, by reading the pck's directory (`GDPC` v4; the layout is in
`core/io/file_access_pack.cpp:290`) and scanning every container:

| | |
|---|---|
| `.godot/shader_cache/` | **149.67 MB** of the 150.7 MB of entries, in 216 containers |
| of which `SceneForwardClusteredShaderRD` | **145.77 MB** — 4 version hashes (one per material using it) × 33 variants, 1.1 MB each, worst 4.5 MB |
| containers still carrying SPIR-V | 74, holding **90.3 MB**; only 5 of those also carry WGSL |
| printable bytes (≈ WGSL text) | 76.3 MB |

So the biggest single component is **SPIR-V for variants whose WGSL was never baked** — and why they
were not baked is now known exactly. A verbose export logs 846 skipped stages:

| reason `_wgsl_unsupported_reason()` gave | count |
|---|---|
| `gl_ViewIndex` (multiview), which WGSL does not have | **836** |
| 16-bit integers | 6 |
| image atomics (`OpImageTexelPointer`) | 4 |

**They are multiview variants, and WebGPU has no multiview at all** (by design — `features.mjs`
`UNCOVERED`, CLAUDE.md). The runtime never requests them, and if it ever did the SPIR-V would be no
use either: Tint aborts on exactly these constructs, which is why
`wgsl_bake_subprocess.cpp:_wgsl_unsupported_reason()` screens them out before forking a child rather
than letting Tint abort inside it. `_set_code_from_spirv()` keeps the bytes anyway, because its
`spirv_needed` rule reads an empty `wgsl_code[i]` as "the runtime must run Tint on these bytes" and
cannot tell that case apart from "nothing could ever convert this". Teaching it the difference drops
~90 MB of the 150 MB for this project, generalizes to every project (it is the engine's own clustered
shader, multiplied by material count), and removes a loaded gun: the only path that would ever read
that SPIR-V is a runtime Tint call which aborts the wasm process.

**Landed 2026-10-14 — the SPIR-V half.** `bake_wgsl_via_subprocess()` now reports *why* its WGSL came
back empty, and `_set_code_from_spirv()`'s `spirv_needed` rule distinguishes the two cases: a
conversion that was tried and failed still ships its SPIR-V (the runtime can try Tint itself), while
a stage `_wgsl_unsupported_reason()` screened out does not, because nothing downstream can convert it.

| `index.pck`, same project and build | | |
|---|---|---|
| `WEBGPU_BAKE_KEEP_SPIRV=1` — nothing dropped | 157.76 MB | the A/B baseline §2.6 asks for |
| the old rule — drop only where every stage baked | 150.76 MB | −7.0 MB |
| **the new rule** — also drop what nothing can convert | **62.36 MB** | **−95.4 MB (−60.5%)** |

| | before | after |
|---|---|---|
| `.godot/shader_cache` | 149.67 MB | 61.27 MB |
| containers carrying SPIR-V | 74 (90.3 MB) | **0** |
| containers carrying WGSL | 147 | 147 |

The escape hatch still works, which matters because it is the only way back without a rebuild:
`WEBGPU_BAKE_KEEP_SPIRV=1` restores all 157.76 MB.

Verified not to have dropped anything in use, which for an export-time change is the whole question:
a real-GPU Forward+ run of the new export reports `baked: 599, precompiled: 1, translated: 0` with
`wgslBytes` 8996919 and `spirvBytes` 111816 — **byte-identical** to the same run before the change,
so the engine consumed exactly the same shader bytes. Comparing the two pcks container by container,
every WGSL blob in the new one existed in the old one and none was altered or added; the only
difference is 88.4 MB of absent SPIR-V. The runtime path for the combination that is now reachable
(no WGSL, no SPIR-V) already reported a clean error rather than calling Tint with a null pointer; its
message now names the multiview case first, since that is what reaching it will usually mean.

**Render-side re-baseline — taken 2026-10-14, and it is clean.** The Forward+ matrix run against a
**baked** export (the first time any tier has ever loaded one — §2.16 gap 7) is **64/64 PASS** on the
real GPU, with the editor and template rebuilt from the same commit so the cache actually matched
(`baked: 423, precompiled: 1, translated: 33`). Comparing its 64 per-feature deltas against the
unbaked run's, three ways over the same 64 features:

| comparison | median | max | over 5% |
|---|---|---|---|
| unbaked before → unbaked after (the driver changes alone) | 0.00% | 2.1% | 0 |
| unbaked after → baked after (baking alone) | 0.00% | 2.0% | 0 |
| unbaked before → baked after (both together) | 0.00% | 2.3% | 0 |

The only features that move at all — `fsr2`, `taa`, `gpu_particles`, `particle_attractor` — are
exactly the temporally accumulating ones the fixture gives extra settle frames and which vary run to
run regardless. Baked WGSL and runtime-translated WGSL render the same thing, and §2.12's deletion
and §2.13's warning changed nothing on a real GPU either (the unbaked run at HEAD is also 64/64).

`translated: 33` is not a baking gap: this fixture builds its materials procedurally in GDScript, so
those shaders do not exist at export time and cannot be baked. Worth remembering when reading
`translated` on any fixture that generates materials at runtime.

**Re-measured 2026-10-14 — the trade has flipped.** Same project (`3d/platformer`), real GPU, 20 Mbit/s
(`--throttle cable`) with gzip, cold, current build, to first presented frame:

| export | pck | pck gzipped | load | stall | first frame |
|---|---|---|---|---|---|
| not baked (`--headless`) | 1.09 MB | 0.91 MB | 6.7 s | 10.9 s | 17.7 s |
| **baked** (`SMOKETEST_BAKE=1`, new rule) | 65.4 MB | **11.2 MB** (was 38.2) | 10.8 s | 2.1 s | **12.9 s** |

Baking used to lose by 8 s here (25.7 vs 17.5); it now **wins by 4.8 s**, and the gzipped download is
3.4× smaller. The SPIR-V drop alone moved this; the pruning below is now an optimization of a winning
configuration, not a rescue of a losing one. (`run_scenes.mjs` gained `SMOKETEST_BAKE=1`, which exports
under `xvfb-run` without `--headless` so the baker runs — the first step of §2.16 gap 7.)

**Why variant pruning is *not* a safe quick win, found while scoping it.** Runtime only creates the
shader groups it has enabled (`SHADER_GROUP_BASE`, `_ADVANCED`, `_MULTIVIEW`, `_ADVANCED_MULTIVIEW`);
`enable_advanced_shader_group()` flips on when something needs separate specular, lightmaps, motion
vectors (TAA/FSR2), VoxelGI depth, etc. — possibly from a script at runtime, which an export cannot
see. A pruned variant that gets requested has **no WGSL and, after the SPIR-V drop above, no SPIR-V
either**, so it is a hard "cannot create shader" failure, not a slow path. Pruning therefore needs an
explicit per-project opt-out/opt-in (e.g. an export option listing groups to keep) or a SPIR-V
fallback for pruned groups, which gives back much of the saving. Not done; decide with a real
measurement of what ADVANCED costs per project first.

**Still open, and what to do next:**

1. **Ship fewer *reachable* variants** — prune by project settings. 33 variants per material is the
   remaining structural cost: 4 materials in a demo project is 132 containers.
2. **Compress the WGSL inside the container.** `code_compression_flags` is hardcoded to 0
   (`rendering_shader_container_webgpu.cpp:149`) while the base class already has `compress_code()` /
   `code_decompressed_size` plumbed. WGSL is text; the remaining 61 MB is 56 MB of it. Worth ~4× on
   its own, but only where the host is not already compressing the pck — and it would cost the
   zero-copy footer read (`wgsl_view` is a window into the container's own bytes, deliberately), so
   it trades load CPU for bytes. Measure against 1 first.

### 2.4 Startup's fixed floor: `Servers:Rendering` and the first frame `OPEN` *(old Task 14, subtask 2)*

**Root-caused 2026-10-20 — the stall was the GPU process compiling things nobody used, serialized with the main
thread.** The tools below only ever looked at the page's main thread and at how long each WebGPU *call* took on it;
the work behind those calls happens on the GPU process, and nothing looked there. A Chrome trace with Dawn's
categories (`gpu.dawn`, `disabled-by-default-gpu.dawn`) over the platformer's startup (headless Chromium on
swiftshader, with a scratch template that skips the <48-textures Forward Mobile fallback so Forward+ runs)
shows, between `godot-before-callmain` and the first frame:

| where | what | ms |
|---|---|---|
| GPU process main thread (`CrGpuMain`) | `CreateComputePipeline` x89 — every effect's compute pipelines, built at renderer init | **~5,900** |
| | `CreateShaderModule` x220-494 — every baked variant of every loaded shader | 600-2,100 |
| page main thread | engine wasm, of which `wasm.CompileLazy` (V8 compiling 17,000 functions on first call) | 3,700 total, 1,140 lazy compile |
| page main thread | **idle**, between startup steps | **~4,900** |

The idle time is the link. The startup runs one step per `requestAnimationFrame` (`web_main.cpp`), and Chrome runs
Dawn on the same GPU-process thread that produces the page's frames, so after `setup2` has queued its pipelines
and modules, no frame -- and so no next startup step -- comes until that thread has compiled all of them. The two
processes took turns instead of overlapping. And the first frame needed **none** of the 89 compute pipelines and
~23 of the ~500 modules: they belong to effects and variants the scene never uses. (This is also what the
September "single blocking `writeTexture`" was: the call where the main thread's wire buffer filled up while the
GPU process was still compiling; it no longer shows since the glyph-atlas uploads were coalesced.)

**Fixed in the driver, no engine changes:** `compute_pipeline_create()` records the request and compiles on the
first `command_bind_compute_pipeline()` (`_ensure_compute_pipeline()`), and `shader_create_from_container()` keeps
each stage's final WGSL and creates the module when a pipeline first needs it (`_get_stage_module()`). Same
export, same commit, `callMain` to first frame:

| | runs (ms) | GPU process before the first frame |
|---|---|---|
| before | 6,787 / 6,734 / 6,872 | 89-91 compute pipelines (~5.9 s), 220 modules |
| lazy compute pipelines | 3,265 / 3,086 / 3,055 | 494 modules (~1.8 s) |
| + lazy shader modules | **2,775 / 2,857** | 22-24 modules (~0.4 s) |

Afterwards the startup steps run back to back (no idle gaps) and the GPU process works in parallel (~0.9 s busy).
What is left is main-thread CPU: `setup2` 1.6 s, `start` 0.65 s, first frame 0.37 s on this (slow, 4-core,
software-GPU) machine, about a third of it V8's lazy compilation. Absolute numbers on a real GPU will be smaller --
swiftshader compiles pipelines on the CPU -- but the structure is not specific to it: pipelines and modules are
compiled on `CrGpuMain` on every platform Chrome runs Dawn on.

The cost moves rather than vanishing for what *is* used: an effect's compute pipelines would compile on its first
dispatch (a hitch when, say, SDFGI is first turned on), and a shader variant's module on its first pipeline.

**Prewarm, landed the same day.** From ~120 frames after startup, `_prewarm_compute_pipelines()` (called from
`begin_segment()`) compiles the deferred compute pipelines in the background through
`createComputePipelineAsync()`, which Chrome compiles off `CrGpuMain`: one started per frame, at most two in flight,
in creation order. A pipeline bound before its result arrives compiles synchronously as before and the late result
is released; one whose specialization re-runs Tint (the legacy path) is left to first use. Progress is published as
`window.godotWebGPUPipelineStats` (`deferred`, `compiledOnFirstUse`, `prewarmStarted`, `prewarmDone`). On the
platformer (forced Forward+, swiftshader): 184 deferred, 5 compiled on first use (the effects of the first
seconds), 150 compiled in the background within 60 s. Scene smoketest unchanged (22/23, same audio check). If the
render thread never returns to its event loop (separate render thread), the callbacks never fire and everything
falls back to compile-on-first-use, which is the behavior without prewarm.

**Render pipelines compiled asynchronously, 2026-10-20.** A user trace of a game stuttering when a decal came into
view, then `gpu_process_trace.mjs --after 20000` on the same game (sewer-shift, Forward+, RTX 4080), showed the rest
of the stall: every render pipeline went through `createRenderPipeline()`, compiled on `CrGpuMain` at ~0.5 s per
Forward+ variant, because `API_TRAIT_REQUIRES_SYNCHRONOUS_PIPELINE_COMPILATION` makes `PipelineHashMapRD` run each
"background" compile inline -- 28 of them, 4.4 s, in bursts after the first frame and whenever a new surface was
seen, while Chrome's async path (149 pipelines, 2.9 s, all on `ThreadPoolForegroundWorker`) stalled nothing. Now
`PipelineHashMapRD::compile_pipeline()` -- ahead-of-time compiles (a material's ubershader while its scene loads) and
the specializations the renderer draws with the ubershader until they are ready -- wraps the creation in
`RD::set_pipeline_creation_async()`, the driver creates through `createRenderPipelineAsync()`, and the pipeline waits
in the queue until `RD::render_pipeline_is_ready()`. A `get_pipeline()` that waits (the ubershader at draw time) or a
pipeline pending 300 frames (a render thread that never runs its event loop) compiles synchronously as before and the
late result is dropped. The external `wait_for_pipeline()` after mesh loading deliberately leaves pending ones alone:
on web that load is not backgrounded, and forcing them would compile every specialization twice. Base defaults
(never async, always ready) leave Vulkan/Metal/D3D12 untouched. Strip pipelines (a second Uint16 variant) stay
synchronous.

| sewer-shift, idle RTX 4080, 2 runs | `CrGpuMain` before the first frame | sync render pipelines after it |
|---|---|---|
| before | 800-820 ms | 23, 1.34-1.36 s |
| async specializations only | 800-835 ms | 20, 1.20-1.23 s (the ubershaders, now in a burst after frame 1) |
| + async ahead-of-time compiles | **550 ms** | **11, 0.17 s** |

Looking around the level (scripted pointer-lock turns, 3 runs each): before, one run froze 521 ms on 4 synchronous
Forward+ pipelines; after, no pipeline was created synchronously in any run. Left on `CrGpuMain`: `createShaderModule`
(0.26-0.73 s after the first frame, synchronous in WebGPU, parsed by Chrome on that thread) and the 26 compute
pipelines frame 1 binds (SSAO/SSIL/SSR/fog/cluster store, ~0.44 s). Starting all compute pipelines asynchronously at
creation was considered and not done: sewer-shift creates 169, whose modules alone (~370 ms, synchronous) would land in
the load, and their compiles would compete with the ubershaders frame 1 needs.

On the Razr 60 (Mali-G615, Chrome 155, Forward+; same two exports, 2 runs each, raw CDP over `adb forward`) the
freeze when the scene first draws went **18.0 / 17.0 s → 6.8 / 7.1 s**, synchronous render pipelines 33 → 18; `Player
Loaded` unchanged (~9.5 s). What is left there is the same set as on the desktop -- 27 compute pipelines frame 1
binds, ~12 ubershaders requested at draw time, ~72 modules -- each far slower on that GPU. The phone fetched only
`textures_etc2_astc.pck` (§2.18 on a real mobile device for the first time), ran 998 baked / 0 translated, rendered
correctly and logged no errors. Once running the game is GPU-bound there at ~9 fps (full resolution, SSAO/SSIL/SSR/fog):
§2.9's `.web_android` scaling overrides apply.

**Found the same evening, then fixed: `webrtc_parity` in Chrome logged `ERROR: WebGPU: device was lost`** (20 of 706
assertions, every Chrome fixture's "no engine error" check; the WebRTC behavior itself passed, Firefox all of it). A
bisect pointed at `c19a9a73e` (5d3e4c672 and 98b210017 good in 7/7 runs, c19a9a73e bad 2/2; undoing only its lazy compute
pipelines made it pass), but that commit only moved the failure into the test's window. Locally `run_loopback.mjs`
launched Chrome with no `headless` option, so Playwright ran it headless: the swiftshader fallback adapter (16 textures,
hence the Mobile renderer in every fixture), and a GPU process that cannot back a WebGPU canvas -- at the first present
it logs `Could not find SharedImageBackingFactory ... WebgpuSwapChainTexture ... RGBA_8888` and the device is lost
(reason `destroyed`, or `unknown` with "A valid external Instance reference no longer exists"). Before `c19a9a73e` the
~26 compute pipelines compiled on swiftshader at init pushed the first present past the fixture's `END`, so the test
finished first; the eager-compute variant still lost the device, at 8.8 s instead of 5.0 s. Fixed with `headless: isCI`
(the comment already said "headed"): all four Chrome variants 353/353 on the current tree. The `CI=1` path (headless,
ANGLE on Vulkan) presents fine, 86/86. §3 trap 43.

**Investigated and not changed:** (1) V8's wasm code cache. The preloader hands `instantiateStreaming()` a
synthetic `Response` (for progress tracking), which in principle can never hit the cache; passing the real one
with progress read from a clone made no difference on a repeat visit (lazy compile ~0.95 s both ways, and no
cached module was ever deserialized, persistent profile or not), so it was reverted. (2) Wasm compilation hints:
Chrome has shipped explicit compile hints only for JavaScript, not Wasm, so the lazy compilation of
startup-only functions cannot be avoided from the page.

Measured (2026-09-26, `webgpu_tests/startup_phases`, user's real project; `OS_Web` now publishes
`main.cpp`'s benchmark marks to `window.godotStartupMarks` under `--benchmark`). Total stall 1925 ms
on the user's project vs 833 ms on a minimal control, decomposed:

| phase | minimal | user's project | nature |
|---|---|---|---|
| `Main::Setup` | 60 ms | 60 ms | fixed |
| **`Servers:Rendering`** | **509 ms** | **513 ms** | **fixed** |
| other `Servers:*` | ~70 ms | ~67 ms | fixed |
| `Startup:Scene` | 111 ms | 110 ms | fixed |
| `Startup:Main::Start` / of which `Load Game` | 16 / 8 ms | 328 / 325 ms | project |
| after `Main::start()` returns — main-loop init + first frame | 57 ms | **835 ms** | project |

Three terms, in order of size:

1. **First frame, ~835 ms** — holds a single blocking `queue.writeTexture` (761 ms in that run, about
   half of it GPU-process pipeline compilation). Largest term and nothing accounts for it yet.
2. **`Servers:Rendering`, ~510 ms, entirely fixed** — every project pays it, empty scene included.
   `createShaderModule` for all 363 modules is 22 ms and pipeline creation is 0 ms, so ~490 ms is CPU
   pulling 344 baked shader containers out of the pck and through the driver, not talking to WebGPU.
   Best target for a fix that helps every project.
3. **`Load Game`, ~325 ms** — genuine resource loading.

Do **not** re-derive these: shaders are no longer the cause (module + pipeline creation is ~10 ms of
~2000 ms), `verbose_stdout` costs ~1.5 s if left on, and the loading bar already covers the whole
startup (§4.11), so this is about wall-clock, not perceived progress.

**Scale check, added 2026-10-14 (§4.15):** on a 20 Mbit/s link with gzip, a baked export of
`3d/platformer` spends **23.5 s downloading and 2.3 s in this stall**. Everything in this item is
worth having, but §2.27 is 10× bigger on a real link — do that first. Two further data points from
the same runs: an *unbaked* export of the same project stalls **11.1 s** rather than 2.3 s
(`containerCreateMs` 6615 ms, `translated: 355`), which is what this stall costs without the baker;
and a dlink export's stall runs ~12% longer than a non-dlink one for identical work.

**Landed 2026-10-14 — term 2 was text scanning, not "pulling containers through the driver".** Section
timers inside `shader_create_from_container()`'s stage loop on a *baked* `3d/platformer` export (599
stages, 9.0 MB WGSL, real GPU) gave a 391 ms loop of which **256 ms (65%) was
`_remap_unsupported_wgsl_storage_formats()` plus the read/write-split blocks**; the rest was convert 23,
`createShaderModule` 67, texture-dim scans 27, reclassify 13, storage scans 10. Two causes, both pure
CPU waste on every stage of every shader:

1. `_remap_unsupported_wgsl_storage_formats()` ended in an *unconditional* block that decoded the whole
   WGSL into a `String`, ran 14 full-text `replace()` scans, re-encoded and re-`malloc`'d it — for stages
   that name no 16-bit format at all. The format names only occur inside `texture_storage_*<fmt, access>`
   declarations, so the function now returns after one `strstr` when a stage declares no storage texture
   (and the 16-bit block has its own substring guard).
2. The two read/write-split loops called `strstr(p, X)` from each `@group(` declaration, then compared
   the result to the declaration's `;` — but an unbounded `strstr` walks to the end of the text whenever
   `X` is absent, so each declaration scanned the whole remainder. They now use `_strstr_before(p, semi, X)`.
   Behavior is identical: a match at or after `semi` already took the `continue` branch.

| | before | after |
|---|---|---|
| stage loop, baked platformer | 391 ms | **~180 ms** (176 / 186 over two runs) |
| of which remap + rw-split section | 256 ms | **24 ms** |
| `containerCreateMs` | 476 ms | ~400 ms (container-level overhead outside the loop is ~80 ms) |

Full `local_ci.sh --dev-mode` (real GPU) green afterwards. What is left in the loop is `createShaderModule`
(~70 ms, the browser's), convert (~23) and the texture-dim scans (~27, same unbounded-`strstr` pattern —
the next cheap target). The unbaked path (`to_convert` ~6.3 s of Tint) is untouched. **Method note:** the
timers were temporary (`_SLMARK(n)` accumulators printed to the console, removed again); and a rebuilt
template needs a rebuilt *editor* before a baked re-export, or the version hash mismatches and the run
silently takes the unbaked path (`baked: 0, translated: 355`) — see the trap added in §3.

**Re-measured and attributed 2026-10-14 (late), baked `3d/platformer`, real GPU, loopback.** The texture-dim scan idea
is dead (~27 ms, ~1% of the stall). The real question was the ~1.1 s after `Main::start()`, and it now has an answer
from a working profiler (`startup_phases/cpu_profile.mjs`: CDP sampling, template built with `debug_symbols=yes` so
names resolve, **stopped at the first presented frame** — left running, the sampler distorts `queue.writeBuffer` from
14 ms to ~1.7 s):

| | ms | note |
|---|---|---|
| total stall (un-profiled `profile_phases.mjs`) | ~2100-2200 | `WebGPU API total` 40 ms (1.9%) |
| **engine wasm CPU up to the first frame** (profiled) | **~1140** | the rest of the stall is outside wasm: browser wasm tier-up, GPU-process round trips |
| `Main::setup2` | 580 | of which `RendererCompositorRD` 322 (`RendererSceneRenderRD::init` 205, `FSR::FSR()` 163 — loads FSR's baked containers though FSR is unused), `ShaderRD::_load_from_cache` 198 |
| `Main::start` | 255 | resource loads 158, `GridMap::_octant_update` 94, Jolt `add_shape` 89, `PackedScene::instantiate` 75 |
| runtime glslang (GLSL → SPIR-V) | ~150 | material shaders (`BaseMaterial3D::_update_shader` 108): a baked export drops the SPIR-V (§2.27), so each material shader is still compiled from GLSL to get the hash the WGSL cache is keyed on; ~90 ms of that is glslang's one-time builtin symbol-table setup |
| `isWebGLAvailable` (page, before the fetch) | ~110 | `getMissingFeatures()` creates a WebGL2 context a WebGPU page never uses. `getMissingFeatures({webgl: false})` now skips it (HTML shells pass it for WebGPU); **gain is inside run-to-run noise** (before-callmain 567-597 ms either side), so it is a logical cleanup, not a measured win |

So there is no single big CPU term left to cut: the 1.14 s is a long tail (shader container load 200, glslang 150, FSR 163,
scene load/instantiate ~300). The only structural lever is to **not construct what the project never uses** (FSR,
SDFGI/VoxelGI/particles storage are built at renderer init) — an engine-wide lazy-init change in shared code, high
risk for ~150-300 ms, not planned. §2.4 is therefore **effectively done**: what remains is design work, not tuning.

**Method traps found doing this** (add to §3 if they recur): an editor rebuilt with plain `scons platform=linuxbsd
target=editor` — i.e. **without `webgpu=yes`** — silently has no shader baker, exports a 1 MB pck, and the run reports
`baked: 0, translated: 355` with no warning (it cost three rebuilds here; `local_ci.sh` passes `webgpu=yes`). And the
version hash is the git HEAD at build time, so any commit between building the editor and the template unbaked the export.

### 2.5 Decide whether threading is worth having `DONE 2026-10-16` — yes: the separate render thread is the default for thread-enabled exports *(old Task 14 subtask 1.5.8.5, Task 12)*

**Closed 2026-10-16.** Implemented, measured (table below: main thread −82% on average across seven scenes) and made the
default through `variant/separate_render_thread` (`b4de0b0b4b`). What stays open underneath it, tracked here rather than as
new items: the perf tools (`api_profile.mjs`, `cpu_steady.mjs`, `perf_guard`) only see the main thread under Separate; no
uncapped fps in Separate (Chrome drives rAF of a worker-owned canvas at ~60 Hz whatever the flags); Safari and mobile are
unmeasured (§2.8, §2.9). The original question and the investigation that answered it are kept below for reference.

`threads=yes dlink_enabled=yes` — threads *and* GDExtension, which this fork never shipped — now
**boots clean** (§4.5), but booting is not being useful. Settle two things in order:

1. **Does it stay up?** Everything verified so far is headless swiftshader, a trivial scene, and
   startup only. Stability under load, audio, input and GDExtension are all unexercised under threads.
2. **Does it buy anything?** Threads cannot move shader or pipeline work off the critical path —
   WebGPU handles live in a per-thread JS table, so creation stays on the thread that imported the
   device (§4.5). Resource loading proper is only ~315 ms. The one legitimately parallelisable
   target is §2.4's ~490 ms of container-unpacking CPU, which touches no WGPU handle. **A hypothesis
   to measure, not a promise.**

Needs COOP/COEP wherever it is served (§1).

**2026-10-15 — data for the decision.** Profiling (§2.17, §2.30) shows every WebGPU export is 99-100% main-thread busy, so a
second thread is the only way to overlap work; but what is hot is *not* engine wasm (`-O2` changed nothing) — it is the
JS/browser call path: ~0.5 ms/frame of marshaling (`bigintToI53Checked`, `getJsObject`, `wasm-to-js`), ~1.4 ms/frame in
`submit` (largely waiting on the GPU process) on the heavy scene, and per-draw calls. A worker-owned renderer would overlap game
logic with that, but it needs `threads=yes` + COOP/COEP on the host, the device and its handle table on the render thread (§4.5),
and the canvas transferred as an `OffscreenCanvas`. Do §2.30's cheaper items first and re-measure how CPU-bound the heavy scene is.

**2026-10-16 — investigation: worth it for logic-heavy games, feasible, design below.**
 - **Prize.** Capped profiles split each frame into game logic (`Main::iteration` minus `RenderingServerDefault::_draw`) and
   rendering. 3D platformer: logic 0.54 ms (physics 0.26, `_process` 0.35 incl. animation 0.10 and GDScript 0.07, audio 0.12),
   render 0.60, browser 0.17 of 1.31 busy -> a render thread cuts the main thread's critical path ~45%. Heavy Forward+ scene:
   logic 0.02 ms -> nothing to overlap. Real games sit nearer the platformer.
 - **Baseline.** `threads=yes` (non-dlink) builds and runs Forward+ on the real GPU; with rendering still on the main thread it is
   no faster (platformer 1.00/1.35 vs 0.97/1.23 ms busy, noise). With `rendering/driver/threads/thread_model=2` (Separate) the
   render pump runs on a WorkerThreadPool worker and dies on its first WebGPU call (`Cannot read properties of undefined (reading
   'createTexture')`): RenderingDevice and every WGPU object were created on the main thread, whose JS handle table the worker
   does not have.
 - **Browser mechanics proven** (`webgpu_tests/threading_proto/`: `./build.sh && node run.mjs "logic=8&render=8"`): a pthread
   created with `emscripten_pthread_attr_settransferredcanvases(&attr, "#canvas")` (needs `-sOFFSCREENCANVAS_SUPPORT=1` and
   `Module.canvas`) owns the canvas; it requests its own adapter/device asynchronously, imports it with
   `WebGPU.importJsDevice`, creates the surface from `"#canvas"`, renders one frame per wake-up, and returns to its event loop
   (`emscripten_exit_with_live_runtime()`), so frames present and WebGPU callbacks run. The main thread wakes it through
   `emscripten_atomic_wait_async` on a counter (if the counter already moved, waitAsync returns "not-equal" and never calls
   back — handle it). 8 ms logic + 8 ms render -> main thread 8.04 ms/tick instead of 16; the render thread presents at
   165 Hz while its work fits one vsync (FIFO quantises 8 ms to 12.12).
 - **Why Godot's render thread cannot be used as is on the web:** it is a WorkerThreadPool task that blocks forever in
   `_thread_loop` (`flush_all` + `yield`). A web worker that never returns to its event loop never presents its canvas (WebGPU
   presents at the end of the task), never resolves `requestAdapter`/`requestDevice`, and never runs `onSubmittedWorkDone`/
   `mapAsync` callbacks. And the device must be *created* on that thread.
 - **Design (all under `WEB_ENABLED && THREADS_ENABLED`, active only with thread_model=Separate; `threads=no` and Safe mode are
   untouched):**
   1. `RenderingServerDefault::init()`: on web, start the render thread through a platform hook (a dedicated pthread with the
      canvas transferred) instead of a WorkerThreadPool pump task.
   2. That thread creates its adapter/device asynchronously (same features/limits as `engine.js`), then — on its own thread —
      the parts `DisplayServerWeb` does on the main thread today: `RenderingContextDriverWebGPU` init, surface on the
      OffscreenCanvas, `RenderingDevice::initialize` + `screen_create`. Main-thread code must not touch RD before that.
   3. Pump: `flush_all()`, then wait asynchronously for the next push (a counter + notify hook in `CommandQueueMT`, set only by
      the web render thread), yielding to the event loop between flushes so each `_draw` presents.
   4. Main-thread waits (`push_and_sync`, `RS::sync`) stay blocking; on the browser main thread Emscripten busy-waits and keeps
      servicing proxied calls, so they do not deadlock.
   5. Canvas resize: route `window_set_size` to the render thread; the OffscreenCanvas size can only be set there.
   6. Driver/context `EM_ASM` that assumes `window`/`document`/`Module.preinitializedWebGPUDevice` must work in a worker.

**2026-10-16 — implemented; worth it; default for threaded exports.** A `threads=yes` template with
`rendering/driver/threads/thread_model=2` (Separate) — which the new web export option `variant/separate_render_thread`
(default on, shown only with `variant/thread_support`) selects by passing `--render-thread separate` — renders from its own
pthread on Chrome and Firefox (both 165 fps capped, pixel-identical to Safe mode on the platformer); the
page must be served with COOP/COEP. `threads=no`, and `threads=yes` with the Safe model, run exactly the code they ran before
(everything below is behind `WEB_ENABLED && THREADS_ENABLED && WEBGPU_ENABLED` and the model check; the scene smoketest,
Forward+ matrix and WebRTC parity exports are nothreads).
 - **Measured (platformer, 165 Hz cap, main-thread busy per rAF tick from `Performance.getMetrics`, 2 runs each):** nothreads
   0.88-0.93 ms (14-15%), threads+Safe 0.81-0.84 ms (13-14%), threads+Separate **0.42-0.43 ms (7%)** — the ~45% cut predicted
   above, at the same 164 fps. The render thread draws the
   frame in ~0.6 ms and wakes within ~0.05 ms of the `_draw` push.
 - **Before/after table (2026-10-16 evening; before = `17d5ee6a85`, the end of 2026-10-15, built as `tmpl_before.zip`; after =
   `db437ca832`; 1280x720, 165 Hz display, same real GPU; capped = main-thread ms per frame from `Performance.getMetrics`,
   render = the render-thread worker's CPU ms per frame from a CDP profile, uncapped = `--disable-frame-rate-limit
   --disable-gpu-vsync` engine fps, GPU = `gpu_time.mjs` busy ms/frame; unbaked exports; scratch `thrprof.mjs`/`thrperf.mjs`):

   | scene | before nothreads: capped / uncapped / GPU | after nothreads | after threads Safe | after threads Separate: main + render | after threads+dlink Separate |
   |---|---|---|---|---|---|
   | cameraSim | 0.51 ms / 837 fps / 0.64 | 0.49 / 832 / 0.58 | 0.56 / 865 | **0.09** + 0.44 | 0.09 + 0.43 |
   | heavy Forward+ (scene_i) | 1.93 / 229 / 3.40 | 1.15 / 292 / 1.46 | 1.16 / 282 | **0.06** + 1.18 | 0.07 + 1.22 |
   | 3D platformer | 0.84 / 833 / 0.40 | 0.79 / 821 / 0.42 | 0.80 / 809 | **0.36** + 0.50 | 0.39 + 0.50 |
   | scene_b PBR | 0.28 / 1012 / 0.28 | 0.27 / 1022 / 0.28 | — | 0.05 + 0.22 | — |
   | scene_d particles | 0.26 / 982 / 0.13 | 0.27 / 974 / 0.14 | — | 0.05 + 0.19 | — |
   | scene_f post-fx | 0.96 / 633 / 0.50 | 0.99 / 619 / 0.49 | — | 0.09 + 0.99 | — |
   | scene_g shadows | 0.44 / 846 / 0.34 | 0.40 / 867 / 0.34 | — | 0.06 + 0.36 | — |

   Reading it: today's GPU/CPU fixes show on the heavy scene (GPU 3.40 -> 1.46 ms, main 1.93 -> 1.15 ms, 229 -> 292 fps
   uncapped) and cameraSim's GPU (0.64 -> 0.58); the small scenes were already at their floor. The Separate model moves all
   rendering off the main thread (cameraSim 0.51 -> 0.09 ms, heavy 1.93 -> 0.06) and leaves game logic there (platformer's
   0.36 is physics/scripts/audio); the render thread's ms are the same work the main thread used to do. The Safe model is
   the nothreads number within noise, which is also why "before, threaded" is not a column: before today the thread model
   did not change where rendering ran. Uncapped fps is not measurable for Separate: Chrome's uncapped mode drives the rAF
   of a page whose canvas is worker-owned at ~60 Hz (same with every flag combination tried). cameraSim nothreads vs
   Separate screenshots differ only in the FPS counter text.
 - **How it works** (`platform/web/display_server_web.cpp`, "Separate render thread"): `web_main.cpp` calls
   `DisplayServerWeb::render_thread_prestart()` right after `Main::setup()` — a pthread with the canvas transferred as an
   OffscreenCanvas requests its own adapter/device (the main thread's features/limits), then startup waits a frame at a time
   until it has one (Chrome only resolves a worker's device request while the page's main thread is free, so it cannot be
   requested from inside `RenderingServer::init()`'s `push_and_sync`). `RenderingServerDefault::init()` starts the thread
   through `external_render_thread_start` instead of a WorkerThreadPool pump task; the thread creates the context and
   RenderingDevice on its first wake and from then on pumps `CommandQueueMT` (a `push_notify` hook bumps a counter the thread
   waits on with `Atomics.waitAsync`); it yields to its event loop after every frame drawn (so it presents) through a
   `MessageChannel` message, never a timer. `RendererCompositorRD::can_create_resources_async()` is false on this driver
   (`API_TRAIT_REQUIRES_SYNCHRONOUS_PIPELINE_COMPILATION`), so shaders/textures are created on the render thread, whose handle
   table owns them. Resize goes through `call_on_render_thread`. Shutdown: `~DisplayServerWeb()` runs the RD/context deletion *on* the
   render thread synchronously (`emscripten_proxy_sync`; an async dispatch ran after ObjectDB was gone and crashed the
   worker), destroys that thread's device, then lets the thread exit. `requestQuit()` is clean in Separate mode.
 - **The main thread never blocks on the render thread in steady state.** The browser main thread cannot block; Emscripten
   spins (`futex_wait_main_browser_thread`, `_emscripten_get_now` was 1.8 ms/frame = 47% busy in the first attempt). So
   `Main::iteration` calls the new `RenderingServer::sync_previous_draw()` (= `sync()` everywhere else) which on this path only
   compares `external_draws_pushed`/`external_draws_done`, and `web_main.cpp`'s `main_loop_callback` first asks
   `DisplayServerWeb::render_thread_wait_for_draw()`: if the previous frame is still rendering it skips the tick and resumes from
   an `Atomics.waitAsync` callback on `external_draws_done`. Per-call `push_and_sync` RS getters still spin (rare, correct).
 - **Browser capability fallback:** `engine.js` probes WebGPU-in-a-worker (`Engine.probeWebGPUWorker`, a blob worker, in
   parallel with the device request) and reports it as the `webgpu_render_thread` OS feature; `Main::setup` drops to the Safe
   model without it, or with any driver but WebGPU. Firefox 155 (Playwright) passes the probe and runs the Separate model.
 - **Known limits / open:** (1) `perf_guard` tools profile the page's main thread only, so under Separate they no longer see the
   WebGPU calls (`api_profile.mjs`, `cpu_steady.mjs`); `gpu_time.mjs` timestamps are on the render thread's device. (2) With
   `--uncapped` (`--disable-gpu-vsync`) Chrome drives rAF of a page whose canvas is worker-owned at ~60 Hz, so an uncapped
   Separate export reports ~58 fps at 5% busy — a tool artifact, not throughput; compare modes capped. (3) The render thread
   takes one worker from the pthread pool (`emscriptenPoolSize`, default 8). (4) Device loss on the render thread latches the
   same `GODOT_WEBGPU_DEVICE_LOST` flag (on the worker's globalThis) the driver already polls. (5) `requestQuit()` warns about 7 leaked
   Texture RIDs in every mode (pre-existing); the `null.currentTime` audio error it used to log is gone with PR #123996. (6) Unbaked exports translate ~355 shader
   stages at startup on the render thread (same work as Safe does on the main thread); the main thread stays responsive now
   that it waits asynchronously.

### 2.6 Real-GPU profile of a baked Forward+ export `DONE 2026-10-15` — frame stats, ring overflow and baked heavy export all done *(old Task 46 §6)*

The configuration this fork actually ships, and the one no number covers. §4.15 measured the
shader-container change (`index.pck` 101.5 MB → 57.1 MB, −43.7%, every behavioral counter
byte-identical, SPIR-V 66.8% of a baked container's payload) on **swiftshader**, i.e. Forward Mobile.
Run `profile_phases.mjs` against a baked export twice — normally and with `WEBGPU_BAKE_KEEP_SPIRV=1`
— reading `godotWebGPUShaderStats` (`spirvBytes`, `wgslBytes`, `footerParseMs`) and
`godotWebGPUFrameStats`. Expect `translated` near 0; the one real-hardware data point on record (the
user's project) reports `translated: 0`, so anything else is a real baking gap worth chasing.

**Half-done 2026-10-14** (while closing §2.3): a baked export of `3d/platformer` on the real GPU
reports `baked: 599, precompiled: 1, translated: 0`, `containerCreateMs` 442 ms, `wgslBytes` 9.0 MB,
`spirvBytes` 112 KB — so baking does reach the shipping configuration on real hardware, and
`translated: 0` now holds on a second project. First frame 3.26 s on loopback, of which 2.5 s is the
engine CPU stall. The `WEBGPU_BAKE_KEEP_SPIRV=1` comparison is also taken — 157.76 / 150.76 / 62.36 MB
for nothing-dropped / old rule / new rule, tabulated in §2.27. Still missing: `godotWebGPUFrameStats`
under sustained load, which needs a real GPU that is free. Do not re-derive the pck-size half —
§2.27 has it.

**Frame stats taken 2026-10-14** (real GPU, unbaked `benchmark_*` exports, 30 s each, `profile_phases.mjs`):
`benchmark_pbr` 165 fps, 37 draws, 6 passes; `benchmark_shadows` 164 fps, 173 draws, 115 push-constant
writes; `benchmark_postfx` 105 fps, 256 draws, 79 passes, 30 dynamic bind groups. In all three
`ringOverflows: 0` and `rwShadowRefreshes: 0`, and `setBindGroup` ≈ draws (bind-group skips are low but
nothing is redundant at scale). **Caveat:** these exports ran `SceneForwardMobileShaderRD`, i.e. Forward
Mobile, even on the real GPU, so they are not Forward+ numbers; they are also unbaked (`translated` 173-186,
`containerCreateMs` ~3 s). A Forward+ under-load figure still needs a Forward+ project (e.g. the
`forward_plus` matrix fixture) run through the profiler.

**Forward+ run taken 2026-10-14** (`forward_plus/export` through `profile_phases.mjs`, 40 s, real GPU,
confirmed `SceneForwardClusteredShaderRD`): 164 fps, 31 draws, 11 passes, 38 `setBindGroup`, 7 dynamic
bind groups, 32 push-constant writes, `ringOverflows: 0`, `rwShadowRefreshes: 0`. Unbaked (`translated` 238,
`containerCreateMs` 3.8 s). Caveat: that fixture is a light, mostly static scene sitting at the display
rate, so it shows the counters are clean on the Forward+ path, not how it scales. A heavy Forward+ scene
(many lights/shadows, SDFGI) was still needed for a real load figure, and a *baked* export for `translated`.

**Heavy Forward+ scene taken 2026-10-14**: `webgpu_tests/benchmark/godot/scene_i_forward_plus_heavy` (600 meshes,
12 shadowed omni + 6 spot + 4-split directional, SSAO/SSIL/SDFGI/glow/volumetric fog), exported with
`forward_plus/export_heavy.sh`, screenshot-verified rendering. Per frame: 161 fps, **3549 draws, 31 passes,
1110 setBindGroup, 3371 setVertexBuffer, 3608 push-constant writes, `ringOverflows: 1`, `rwShadowRefreshes: 40`**.
So under load the push-constant ring overflows once a frame (the lighter scenes never did) and 40 RW-shadow
texture copies happen per frame; neither is an error but both are real per-frame cost worth a look. 161 fps may
still be display-rate-capped, so fps is a lower bound on headroom, not a measurement. Console: only the known
§2.13 readback-returned-zeros warning. Still unbaked (`translated` ~475 Forward+ variants).

**`ringOverflows: 1` fixed 2026-10-14.** It was plain arithmetic: 3608 push-constant writes × 256 B slot = 924 KB
against a 512 KB ring, so exactly one mid-pass flush + submit + encoder rebuild per frame. `PUSH_CONSTANT_RING_SIZE`
is now 2 MB (8192 draws; only the dirty range is uploaded, so the cost is address space, not bandwidth). Heavy
scene afterwards: `ringOverflows: 0`, same 164 fps / 3549 draws; Forward+ matrix 64/64. Still open:
`rwShadowRefreshes: 40` per frame (**corrected 2026-10-16, §2.30:** the GPU does not lack the feature; the driver queried a WGSL language feature as a device feature, so these were *not* the
documented emulation copies, not a bug), and a *baked* heavy export for `translated`.

**(Historical, superseded below by the re-run.) 2026-10-15 — blocked on the host, and a finding.** (1) `nvidia-smi` fails with a driver/library mismatch (kernel module 610.57.04, userspace 615.71.09), so Chromium cannot reach the RTX 4080 SUPER: the heavy scene ran Forward Mobile at 1-2 fps. A reboot or module reload is needed before any real-GPU number is valid; the figures from that run are discarded. (2) `startup_phases/*.mjs` launched Chromium without `--use-vulkan --use-angle=vulkan --use-vulkan=native --ignore-gpu-blocklist`; fixed to match `run_forward_plus.mjs`. (3) `BAKE=1 ./export_heavy.sh` now exists, but the heavy scene builds its meshes and materials in GDScript, so the exported pck is 11 KB and the baker has nothing to bake; a baked heavy export needs a scene saved as a `.tscn`/resources.

**(Historical — done, see "Re-run done" below.) RE-RUN AFTER THE REBOOT (paused 2026-10-15).** Everything measured on 2026-10-15 ran while `nvidia-smi` reported a driver/library mismatch (kernel 610.57.04 vs userspace 615.71.09), so none of it counts. Before re-running, confirm `nvidia-smi` works and that an adapter probe with the flags below reports the RTX 4080 SUPER with `maxSampledTexturesPerShaderStage` >= 48 (otherwise it is Forward Mobile and the run is meaningless). Required launch (now in all `startup_phases/*.mjs`): system Chrome `/usr/bin/google-chrome-stable`, headed, `--enable-unsafe-webgpu --enable-features=Vulkan --use-vulkan --ignore-gpu-blocklist`, Playwright's `--enable-unsafe-swiftshader` default dropped. Do NOT add `--use-angle=vulkan`, `--use-vulkan=native` or `--no-sandbox` to system Chrome: the page dies with "WebGL2 - Check web browser configuration" (found 2026-10-15). Re-run:
1. `forward_plus/export_heavy.sh` then `startup_phases/profile_phases.mjs --dir ../forward_plus/export_heavy --duration 40` (expect `SceneForwardClusteredShaderRD`, ~160 fps, `ringOverflows: 0`; the 1-2 fps Forward Mobile result of 2026-10-15 is invalid).
2. The same with `BAKE=1 ./export_heavy.sh` -> `export_heavy_baked` (only meaningful once the heavy scene's meshes/materials are saved resources; today the pck is 11 KB and nothing bakes).
3. Any other real-GPU tier touched this session: `run_forward_plus.mjs` (+ `--baked`), `local_ci.sh --bake`.
4. The editor/web interleave test of §2.19 was build-only and does not depend on the GPU; it stands.
Remaining plan after that: §2.29 WebRTC leg and re-baseline, §2.17, §2.14 sweep, §2.20/§2.21.

**Re-run done 2026-10-15 after the reboot** (system Chrome, flags above, adapter probe: nvidia / lovelace). Heavy Forward+ scene (`SceneForwardClusteredShaderRD`, 475 variants): 131-134 fps headed, 3549 draws, 31 passes, 1110 setBindGroup, 3608 push-constant writes, `ringOverflows: 0`, `rwShadowRefreshes: 40`; first frame after a ~19 s engine CPU stall (unbaked: `baked 0, translated 391`). The `BAKE=1` export is byte-identical in behavior (`baked 0`) because the scene is built in GDScript and the pck is 11 KB, so nothing can bake; converting it to saved resources is still needed for a baked heavy number. Forward+ matrix, unbaked and `--baked` (with `WEBGPU_REAL_GPU=1`): 64/64 both. `run_forward_plus.mjs` now launches system Chrome the same way. Other scripts under `webgpu_tests/` still launch bundled Chromium with `--use-angle=vulkan --use-vulkan=native --no-sandbox` (e.g. `screenshot_comparison`, `scene_smoketest/test_*_visual.mjs`, `resource_lifecycle`); audit them before their next real-GPU run.

**Baked heavy Forward+ export measured 2026-10-15** (real GPU, system Chrome; `scene_i_forward_plus_heavy` converted to a saved `main.tscn` by `generate_scene.gd`, `main_runtime.tscn` keeps the script-built original). `BAKE=1 export_heavy.sh`: pck 34.6 MB vs 0.19 MB unbaked. Unbaked: `baked 0, translated 391`, `containerCreateMs` 12.8 s, engine stall 18.5 s. Baked: **`baked 759, translated 0, precompiled 1`**, `containerCreateMs` 333 ms, `wgslBytes` 18.2 MB, `spirvBytes` 112 KB, stall **7.7 s**, 139 fps, 3549 draws, `ringOverflows 0`, `rwShadowRefreshes 40` unchanged. So baking works on the heavy Forward+ path with nothing falling back to runtime Tint; §2.6's remaining work is `rwShadowRefreshes` only (documented emulation copies). **Trap (cost an hour):** an editor built without `webgpu=yes` has no shader baker, so every `BAKE=1` export silently comes out unbaked (0.19 MB pck, no message); and after any commit the editor and template must be rebuilt together or the baked cache is discarded (`baked 0` with a 34 MB pck). Build both with `webgpu=yes`; check `baked > 0` in `godotWebGPUShaderStats`.

### 2.7 Offer F10's default change upstream `OPEN` — human *(old Task 18.16)*

This fork now differs from stock Godot by one default: `WebRTCMultiplayerPeer`'s
`unreliable_lifetime`, 1 ms → 50 ms (`DEFAULT_UNRELIABLE_LIFETIME_MS`, §4.20 / F10). The sooner
upstream has the numbers, the less that drift matters, and it is the only item here that affects
projects on other platforms too. Everything needed exists: the measurements, the controls isolating
it to the lifetime *value* rather than the mechanism, and two fixtures (`loss_modes.gd` for the
mechanism, `mp_modes.gd` for the default). What is missing is the conversation, which is a human's.

### 2.8 Safari has never been run `BLOCKED` — needs a Mac

Applies to the scene smoketest, WebRTC parity and congestion tiers. The drivers exist
(`RTC_BROWSER=safari`, AppleScript, as `scene_smoketest` does it) and `local_ci.sh` invokes them on
`Darwin`, but this work was all done on Linux and Playwright's `webkit` is **not** Safari and has no
WebGPU. One `./webgpu_tests/local_ci.sh` on a Mac closes it. **Treat a first failure as a new data
point, not a regression.**

### 2.9 Android / iOS browsers `OPEN` — Android profiled 2026-10-17, iOS never run *(old Task 5.2)*

Desktop is confirmed across Chrome, Firefox, Edge, Vivaldi and Safari on macOS/Windows/Linux (Linux
needs Vulkan+WebGPU flags and a native package, not Flatpak/Snap). Safari/iOS 18+ has never been tried.
This is also what §2.18 exists to serve.

**Chrome/Android, first run 2026-10-17** — Motorola Razr 60 (MediaTek MT6878, Mali-G615 MC2 "valhall",
Android 16, Chrome 154) over USB, tooling in `webgpu_tests/perf_guard/android/` (adb reverse so the phone
loads `http://localhost:8099/` as a secure context, raw CDP through `adb forward` to
`localabstract:chrome_devtools_remote`). The adapter reports `maxSampledTexturesPerShaderStage` = exactly 48
(engine.js asks for the adapter max), so a `forward_plus` project runs **Forward+** on this phone. Everything
boots and renders correctly; the only console lines are the known §2.13 readback warning and the
`float32-blendable` notice.

The user's `cameraSim` (Forward+, DOF near+far, directional + 3 shadowed spots, 7 opaque draws, 1920x1080
backbuffer, 2.6 dpr) does **20.5 fps**, and it is **GPU-bound**: main thread 29% busy, `gpu_time`-style
timestamps on the page put **48.7 ms/frame on the GPU (90% of the timeline)** — the opaque lighting pass
`SceneForwardClusteredShaderRD:18` (the specialized color pass, *not* the ubershader; 7 draws, depth prepass
for the same draws is 0.67 ms) **28.0 ms**, the four `BokehDofShaderRD` compute passes **14.5 ms**, tonemap
1.2, the 4096² shadow maps ~1.7, everything else under 1. Against the desktop profile of the same scene
(§2.14: opaque 0.29 ms, DOF 0.145) both big passes are ~100x, i.e. the hardware gap, not one pathological
shader. Variant matrix (release, main-thread rendering, one tab, cold boot, 10 s windows):

| variant | fps | p50 / p90 ms | main busy |
|---|---|---|---|
| release as-is / debug as-is / no dlink / nothreads / ASTC character texture | 20.5 / 20.3 / 19.2 / 20.8 / 20.5 | 41.5 / 83 | 19-32% |
| your project + the `.web_android` overrides below | **55.6** | 16.6 / 16.6 | 54% |
| shadows 2048 + soft filter quality 1 | 22.6 | 41.5 / 41.5 | 31% |
| bokeh quality Very Low + Box shape | 21.4 | 41.5 / 83 | 31% |
| `scaling_3d/scale` 0.75 | 30.1 | 33.2 / 33.2 | 42% |
| `scaling_3d/scale` 0.66 + cheap DOF + cheap shadows | 50.1 | 16.6 / 33.2 | 53% |
| same + `scaling_3d/mode` FSR 1.0 | 30.1 | 33.2 / 33.2 | 45% |
| `scaling_3d/scale` 0.5 (alone, or with the cheap DOF/shadows) | 59.6 / 56.9 | 16.6 / 16.6 | 53-56% |
| Forward Mobile (diagnostic only — the user's projects stay on Forward+) | 12.1 in the matrix (translation-contaminated); **21.6** settled | 124 / 124 | 20% |

So the template kind (debug/release, dlink, threads, the separate render thread) and texture compression
are irrelevant on this phone; the frame is per-pixel Forward+ work and scales with the 3D resolution almost
exactly (0.56x pixels → 1.47x fps, 0.25x → 2.9x). The DOF cost is fixed full-res bandwidth (sample count
barely matters). `.web_android`-suffixed project settings target this device without touching desktop
(`mobile` is not a web feature tag — the project's `*.mobile` overrides never applied here).

**Lighting-pass bisection, 2026-10-17** (`android/gpu_time.mjs --dump-wgsl` / `--replace-pair`: the three lit
materials' `mod:SceneForwardClusteredShaderRD:18:stg1` modules dumped from the phone, edited by function
*signature* — Tint mangles names but keeps parameter names — and substituted by exact code match; `full` control
reproduces 26.9 ms):

| cut in the fragment WGSL | `:18` ms | reading |
|---|---|---|
| none (control) | 26.9 | |
| fragment body returns at once | 1.85 | raster + RGBA16F write floor; ~25 ms is shading |
| `light_compute` (BRDF) returns at once | 8.4 | |
| every `textureSampleCompare` → 1.0 | 16.4 | shadow filtering ≈ 10 (8-tap PCF × 4 lights) |
| `light_process_omni/spot` return at once | 15.6 | the 3 spots ≈ 11.3 |
| spot shadow samplers → 1.0 | 18.6 | their shadows ≈ 8.3 |
| directional shadow samplers → 1.0 | 23.0 | ≈ 3.9 |
| `light_process_area` + `reflection_process` return at once | 27.2 | unused feature code is free |

The cuts are super-additive (BRDF 18.5 + shadows 10.5 > 25), i.e. part of the cost is register pressure /
occupancy of the whole program, not arithmetic alone. Everything that could have been *ours* was then checked and
cleared: (a) `:18` is the specialized colour pass (depth variants are 0-17, colour-pass flags start at 18), not the
ubershader; (b) the pipeline overrides reach Dawn — `--log-pipelines` shows `pso_sc_packed_0 = 0x308308B0`
(soft/penumbra samples 8/12, directional 8/12, soft-shadow flags off), so the PCF loops are compile-time;
(c) **f16 is a null result on this GPU**: Clustered's lighting locals are float where Mobile's were ported to half
(the shared `scene_forward_lights_inc.glsl` takes `half`/`hvec3`), so a boundary bridge was written (casts at the
six lighting calls, `F0`, `blur_shadow`, `reflection_process`, the clearcoat helpers; compiles with
`EXPLICIT_FP16` for every optional material path; Tint emits `enable f16;`) and substituted with the device
requesting `shader-f16`: **47.9 ms GPU vs a 48.7 ms f32 control** (`:18` unchanged within noise). The bridge is
not kept in the tree (no payoff here); the patch is kept as
`webgpu_tests/perf_guard/android/forward_clustered_explicit_fp16_bridge.patch`, worth re-trying on `web_ios` (Apple GPUs gain much more from f16). (d) `OpImageGather` is translated
natively (no gather emulation in the preprocessor), so FSR 1.0's 7.3 ms (two `FsrUpscaleShaderRD:1` passes at 1080p
output, plus a 1.2 ms `CopyToFbShaderRD`) is simply EASU+RCAS on this GPU — use bilinear. (e) **Forward Mobile
correction**: the 12.1 fps matrix row was contaminated by runtime shader translation during the window (unbaked
export); settled it is 21.6 fps, opaque pass 34.1 ms for 4 draws (no depth prepass → shaded overdraw), raster DOF
7.8 ms. So Forward+ is the better of the two here and the per-pixel lighting cost is inherent to these shaders on
a 2-core Mali, not a translation artifact. The user's project now carries `.web_android` (and milder, untested
`.web_ios`) overrides — `scaling_3d/scale` 0.6, bilinear, 2048 shadow maps, soft filter quality 1, bokeh Very
Low/Box — measured at **55.6 fps**.

Still open: a native-Vulkan number on the same phone (needs an Android SDK for an APK export — Java is present, the
SDK is not; a 4.7.2 official Android template with the 4.8 editor would also be a version mismatch); and the
separate-render-thread case, where the pthread worker never answers `Runtime.evaluate` (idle pool workers sit in
`Atomics.wait`, the render thread never idles) — `android/worker_gpu.mjs` is the unfinished self-driving in-worker
variant; measure such exports with `separate_render_thread=false`, which makes no fps difference on this phone.

### 2.10 Audit the storage-format-by-variant class `OPEN` — audited 2026-10-14

Three instances of the same bug have been found one at a time by running scenes: a shader declares
`layout(<fmt>, set = …) uniform image*` while its C++ caller binds a different format. Known instances
for shape: `ssao_interleave.glsl`'s `RB_FINAL` (§4.17), TAA's 2-channel velocity buffer (§4.12), the
depth back-copy's `rgba16f`-vs-`R32_SFLOAT` dispatch (§4.14).

**Audited 2026-10-14 — every statically decidable part is clean.** Three cross-checks, all mechanical:

1. **Every storage-image declaration in the engine's shaders has a path.** 147 declarations across
   139 `.glsl` files, 21 distinct format qualifiers, all either WebGPU core storage-texel formats or
   handled by `_promote_storage_format()`. The only non-literal qualifier is `OCTMAP_FORMAT`, which is
   `#define`d per variant in `copy_effects.cpp:182` to `rgb10_a2` or `rgba16f` — both covered, and a
   neat illustration of why this class is named "by variant" and why grepping for literals misses it.
2. **The capability switch and the promotion agree.** All 37 `DataFormat`s that
   `texture_get_usages_supported_by_format()` reports as `TEXTURE_USAGE_STORAGE_BIT`-capable are either
   core storage formats or have a `_promote_storage_format()` case. None is capable-but-unpromotable,
   which is the §4.17 omission (`R8G8_Uint`) and the earlier `R16_UINT` one.
3. **The texture-side promotion and the WGSL-side remap agree**, format by format, across all four
   `texture-formats-tier1` × `tier2` combinations — including the two-step cases where
   `_remap_unsupported_wgsl_storage_formats()` takes `r16snorm` → `r16float` → `r32float` while
   `_data_format_to_wgpu()` + `_promote_storage_format()` take `R16_SNORM` → `R16Float` → `R32Float`.
   The 16-bit remap block is unconditional, matching "16-bit formats: always promote" on the texture
   side; the 8-bit and 10/11-bit blocks are tier-gated on exactly the same tiers as their promotions.

**What is left is coverage, not analysis.** The remaining question — does the texture a caller
actually binds match the format its shader declares, for every variant — is not statically decidable
without tracing RD texture creation to uniform-set binding across the whole renderer, and the payoff
of doing that by hand is low: all three instances were found by *running* a scene, and Dawn rejects
the mismatch loudly when it happens, so this is a detectability problem only in the sense that no run
had covered the variant. Close it with coverage instead — the Forward+ matrix (64 features) and
§2.16's regression corpus and live sweep — rather than by a manual trace.

### 2.11 VoxelGI measures 1.6× native Vulkan `CLOSED 2026-10-14` *(old Task 17.1, bug 2)*

With its two fixture bugs fixed, VoxelGI measures **12.243** in the Forward+ matrix against native
Vulkan's **7.742** — plainly working (the matrix asserts a floor, so it passes) but unexplained. Note
the thresholds are deliberately calibrated on native Vulkan, never on WebGPU. Needs a real GPU.

**Closed 2026-10-14 — it was a real bug, not a calibration quirk.** The "1.6×" was WebGPU being *wrong in a way
that happened to produce a bigger delta*: with VoxelGI on, WebGPU's frame was 11 luminance units darker than
the off frame, Vulkan's 1.3 (ON frames differed by 9.8 mean-abs; OFF frames by 0.02). Bisected with
`Viewport.debug_draw`: `DEBUG_DRAW_VOXEL_GI_ALBEDO` matched Vulkan to 0.0006 (baked data upload and octree
traversal fine) but `..._LIGHTING` was 63 vs 119 mean luminance — the directional light was missing from the
voxel lighting. The fix is in `command_copy_buffer_to_texture`: the destination Z origin used only
`texture_subresource.layer` and ignored `texture_offset.z` (the 3D-texture slice origin), and the
`wgpuQueueWriteTexture` data size ignored depth (`aligned_bpr * rows_per_image`, not `* depth`). After it, the
lighting view matches Vulkan to 0.0006, the feature delta is **7.743 vs Vulkan 7.742**, ON frames differ by 0.002,
and the matrix is still 64/64. I did not separate which of the two edits carried the fix (they went in together);
both are correct by the WebGPU copy rules. Lesson: a matrix delta that is *larger* than the reference is still a
discrepancy — compare the frames, not just the number. `run_forward_plus.mjs --save-dir DIR` now keeps every
captured frame for exactly that comparison against `--fp-matrix` output.

### 2.12 Delete dead `_depth_alias` code; the missing reverse fallback `CLOSED 2026-10-14`

**Deleted**, and the reverse fallback deliberately **not** added. Both decisions and their evidence
are in §4.12.

### 2.13 Stale Phase 7 TODOs: 7.8, 7.15, 7.17 `CLOSED 2026-10-14`

All three answered; none needed a fix beyond one message. 7.15 and 7.17 are no-leaks, 7.8 is an
inherent property of asynchronous readback in single-threaded wasm, now stated in the code and in
§4.2. That message was a `WARN_PRINT_ONCE` until 2026-10-17 and is now `print_verbose` (§4.28): on a
backend where every readback's first result is a zero-filled shadow by design, warning about it made
a correct export look broken.

### 2.14 Base-class interface audit, and a live sweep `OPEN` — audit done; first project swept clean 2026-10-15

- **Interface audit** (old Task 10.2) — **done 2026-10-14, baseline `9973b23ec7`.** 187 virtuals
  across the three base headers; **every pure virtual is overridden** (0 gaps). 24 non-pure virtuals
  fall through to a base default, which breaks down as:
  - **12 `RenderingShaderContainer` extra-data hooks** — the WebGPU container overrides the five it
    needs (header and footer extra data, `_from_bytes_begin`) and genuinely does not use the
    per-shader / per-binding / per-specialization ones. Correct as is.
  - **2 compute-pass hooks** — `command_begin_compute_pass()` / `command_end_compute_pass()`. The
    render graph brackets *every* compute list with these (`rendering_device_graph.cpp:1180`), Metal
    overrides both, and this driver overrides neither. **That is §2.28**, and it is not cosmetic.
  - **8 `RenderingContextDriver` memory-reporting methods** (`get_device_total_memory`,
    `get_driver_allocation_count`, `get_tracked_object_type_count`, …) — unimplemented, so the
    editor's memory profiler reads zeros for a web export. A real gap, but a reporting-only one;
    WebGPU exposes no allocation accounting to implement it from.
  - **`get_driver_workarounds()`** — the default is right. Its one flag, `avoid_compute_after_draw`,
    exists for an Adreno 6xx driver bug that Vulkan detects by driver version; a browser exposes
    neither vendor nor driver version, and whether the browser works around it on such a device is a
    §2.9 question.
  - `API_TRAIT_*`: 27 values, 23 with both a base default and a WebGPU case — the same 23, so no trait
    WebGPU relies on is missing its non-WebGPU default. The four without a base default
    (`API_TRAIT_ACCELERATION_STRUCTURE_INSTANCE_SIZE`, `API_TRAIT_SHADER_GROUP_*`) are upstream's
    raytracing traits, reachable only from raytracing paths this driver never enters. No stale cases
    for removed traits.
- **Live sweep** (old Task 10.4): every fixture tier only covers what it was written to cover. The
  deepest bugs in this project's history — SDFGI, FSR1/2, the `threads=yes` crashes, the baking
  crashes — all came from running a real project, not from fixtures. Use
  `webgpu_tests/screenshot_comparison/LIVE_REPRO_METHODOLOGY.md`.

**Live sweep, first project, 2026-10-15.** The user's real `cameraSim` project (Forward+ on web, DOF, 3-point lighting rig,
a scanned character, a UI) in a scratch copy (`LIVE_REPRO_METHODOLOGY.md` step 1; `builds/`, zips, `.godot` left out),
exported with the shader baker (`xvfb-run`, system template, `variant/extensions_support=false` since the dlink template
was not current) and run in system Chrome on the real GPU: **163 fps, `baked 427 / translated 0`, 2.0 s startup stall,
`SceneForwardClusteredShaderRD`, and the only console message is the known §2.13 readback warning.** The screenshot shows
the full scene rendering correctly (depth of field, spotlight cone, shadows, UI). Not yet swept: other real projects, the
dlink export of this one, and a native-Vulkan side-by-side. Note the exporter's "baked cache ... none of it matches"
warning appeared in the *editor's* export log although the export did bake 427 variants; likely about the editor's own
process, not worth chasing unless it recurs with `baked 0`.

**cameraSim re-test, 2026-10-16** (after §2.30's SSIL and read-write-storage fixes, `a79c6f242f`; editor and template rebuilt at
that commit; scratch copy as before). A/B with unbaked exports from a template with this session's driver changes reverse-applied
vs HEAD, plus a baked HEAD export. Uncapped: **~800 fps in all three** (1.21-1.36 ms/frame; CPU-bound, GPU ~50% of the timeline);
GPU time **0.65-0.68 -> 0.58 ms/frame** (-12%, `rwShadowRefreshes` 2 -> 0); baked `427 / translated 0`. Capped at 165 Hz the main
thread is **15% busy (~0.9 ms CPU/frame)**. Screenshots before/after are **byte-identical** (DOF, light rig, character, UI). Per pass
it is at native parity: opaque 0.29 ms vs 0.31 native (`--gpu-profile --resolution 1920x1080`), bokeh DOF 0.145 vs 0.128. Nothing
on this project is worth chasing GPU-side; it has ~6x CPU headroom at the display rate on this machine.

**Deploy side, 2026-10-17 (§4.23):** the containers now ship as a separate `index.shaders.pck` on web, so a
whole-file upload after a code change moves the 14.9 MB game pack and not the 35.6 MB of shaders, and the
browser can keep the shader pack cached across deploys. The download total is unchanged; this does nothing for
the first load, only for re-deploys and re-exports (7.8 s / 4.2 s instead of ~30 s). Pruning variants is still
the open question above.

### 2.28 Compute lists share one encoder: the graph's begin/end hooks are ignored `FIXED 2026-10-14` (end hook; matrix 64/64 on real GPU) *(found by §2.14's audit)*

`RenderingDeviceGraph::_run_render_commands()` brackets every compute list with
`driver->command_begin_compute_pass()` and `command_end_compute_pass()` (`rendering_device_graph.cpp:1180`).
The base implementations are empty, Metal overrides both, and this driver overrides **neither** — it
instead creates a `WGPUComputePassEncoder` lazily inside `command_bind_compute_pipeline()` when none
is active, and never ends one explicitly. The pass therefore survives until something else happens to
end it (a render pass beginning, or the command buffer ending), so *consecutive* compute lists share
one encoder — and with it one WebGPU synchronization scope.

That is the condition `split_compute_pass_if_conflicting()` exists to detect and repair after the
fact: it tracks every dual-capable texture bound in the current pass and, on a conflict, ends the
pass, begins a new one, re-binds the pipeline, the gap groups and all four bind groups. The FSR1/FSR2
EASU-write-then-RCAS-sample ping-pong is the case it was written for. Honoring the two hooks would
make each Godot compute list its own pass, which is a fresh synchronization scope by construction —
preventing that class rather than repairing it, and letting the per-bind texture bookkeeping go.

**Fixed:** the driver now overrides `command_end_compute_pass()` (ends the active compute encoder); the begin stays lazy in `command_bind_compute_pipeline()`. `split_compute_pass_if_conflicting` is kept as a backstop for dispatches within one list. Forward+ matrix 64/64 on a real GPU afterwards. Not yet done: removing the per-bind texture bookkeeping, and checking `current_pipeline` sharing.

Original analysis — worth doing, but not blind: it changes the hottest compute path and the features it affects (FSR1,
FSR2, SDFGI, SSAO/SSIL, volumetric fog) are exactly what the Forward+ matrix covers, so it wants a
real GPU for the before/after. Check while there whether `render_state.current_pipeline` being shared
between render and compute still reads correctly once passes are explicitly bracketed.

### 2.29 Make the baked export a real test tier `DONE 2026-10-15` — Forward+ matrix, scene-smoketest and WebRTC legs landed; deltas re-baselined *(from §2.16 gap 7 and §2.27)*

Every export helper in the suite passes `--headless`, which skips the export-time shader baker, so every
tier runs the unbaked path (`baked: 0, translated: 355`) while the configuration that ships is
`baked: 599, translated: 0` — a different container reader, load profile and failure surface. Baked exports
have been run by hand twice (Forward+ matrix 64/64, §2.27; platformer load profile) and never by CI.

`SMOKETEST_BAKE=1` on `run_scenes.mjs` (2026-10-14) already exports under `xvfb-run` without `--headless`.
**Landed:** `BAKE=1 ./export.sh` (forward_plus) → `export_baked/` via `xvfb-run` (29 s here; SKIP/exit 2 without xvfb-run); `run_forward_plus.mjs --baked` asserts `baked > 0`, `translated <= 80` and no cache-mismatch warning (verified red against an unbaked export: `baked: 0, translated: 200`), 64/64 on real GPU with `baked: 415, translated: 33`; `local_ci.sh --bake` runs both (opt-in). **Smoketest leg landed too:** `SMOKETEST_EXPORTS_DIR` + `run_scenes.mjs --baked` (asserts `baked > 0` and no mismatch warning per scene; gl_compatibility projects are exempt — the baker only bakes the project's own renderer, so the four 2D/GUI demos export with nothing baked). `local_ci.sh --bake` re-exports to `exports_baked/` (6.6 min here) and runs it: 21/21 on the real GPU. Still open: the WebRTC fixture leg, and re-baselining deltas (item 4).

Original plan:

1. `forward_plus/export.sh` (and `export_heavy.sh`, the WebRTC fixture) get the same switch, and
   `local_ci.sh` gains a `--bake` stage that exports baked variants into separate dirs
   (`export_baked/`) so the unbaked tiers keep their fast path.
2. Run, against the baked exports: the Forward+ matrix, the scene smoketest, and assert from
   `godotWebGPUShaderStats` that `translated` stays 0 for scenes with no runtime-built materials (the
   matrix fixture builds materials in GDScript, so it legitimately sees `translated ~33`: assert a
   ceiling, not zero) and that there is no cache-mismatch warning.
3. Say SKIP, not PASS, when `xvfb-run` is absent (macOS, CI runners without it), per the suite's rule.
4. Re-baseline: deltas of the baked matrix against the unbaked one stay within 5% (it was 2.3% max).
5. Watch export time: baking is minutes, not the 90 s the headless path allows.

Done when a regression that exists only on the baked path (a dropped variant, a container-format change,
a mismatched version hash) turns a tier red.

**WebRTC leg landed 2026-10-15.** `BAKE=1 webrtc_parity/export.sh` -> `exports/loopback_baked` (xvfb-run, exit 2 = SKIP without it); `run_tests.mjs` treats it as a fourth variant and asserts `shaderStats baked > 0` (printed by `run_loopback.mjs`); `local_ci.sh --bake` re-exports it. Real GPU, system Chrome: 353/353 assertions across nothreads, threads, dlink and baked. **Finding while doing it:** the WebRTC fixture project never set `rendering/renderer/rendering_method.web`, so every one of its exports was tagged `renderingDriver: opengl3` and the whole WebRTC parity tier had **never run on WebGPU** (the engine never even requested a device). Fixed in `fixtures/loopback_project/project.godot`; the suite passes on the WebGPU path. Two things seen there, not chased: one `device was lost` during `mesh_close` on the baked export in 1 of ~11 runs (0 of 10 reproductions, GPU otherwise idle), and this fixture logs "Platform supports less than 48 textures per stage... fallback to Mobile" where the heavy scene gets Forward+ (its page does not use the engine shell's max-limit request the same way). `run_loopback.mjs` also launched bundled Chromium with `--use-angle=vulkan`; now system Chrome with the three flags. Still open: item 4, the delta re-baseline.

**Delta re-baseline done 2026-10-15 (item 4).** Matrix 64/64 both ways on the real GPU. Of 64 deltas, 61 agree within 3% baked vs unbaked; `taa` (0.46-0.60) and `particle_attractor` (10.1-11.1) differ by more than 5% but vary just as much run to run within the *same* export (3 runs each), so that is noise, not bake. **One real oddity:** `ambient_light` is bimodal on the *unbaked* export (114.16 in 2 of 3 runs, 37.73 in the third) and steady at 37.73 on the baked one (3/3 plus the full run). Both clear `minDelta 12.6`. The `Environment` sky-radiance ambient baseline is plausibly still settling when the "off" frame is captured, so the slower unbaked start changes which state it lands on; not chased. If a matrix run ever fails `ambient_light`, look at the settle window first. §2.29 is otherwise complete.

### 2.15 `generate_lods()` returns zero LODs on web `CLOSED 2026-10-15` — not a LOD bug; it is §2.13's synchronous readback *(old Task 17.1, bug 3)*

Re-tested with a scratch project run natively and in a web export (system Chrome, real GPU). `ImporterMesh.generate_lods()`
on **CPU-built arrays** gives **4 LODs on both** native and web — the function is fine, and meshoptimizer is linked
into the web template. The old "0 on web, 9 native" came from feeding it `SphereMesh.get_mesh_arrays()`, which reads the
vertex buffer back from the GPU: on WebGPU that synchronous readback returns **zeros** (the §2.13 warning "a buffer
readback completed with no mapped data available yet, so it returned zeros"), so every vertex is (0,0,0), the merge
step collapses them to one vertex (`LOD Generation: Triangles 4224, vertices 2210 (merged 1)` in a `--verbose` run) and
no LOD is produced. Diagnosis trail: `dict_unique=1` for a Dictionary keyed by the readback vertices, `v1=(0,0,0)` vs
native `(0,0.5,0)`. **Consequence worth knowing:** any C++/GDScript path that calls `mesh_surface_get_arrays`,
`SurfaceTool.create_from(mesh)`, `MeshDataTool`, `Mesh.create_trimesh_shape()` etc. on a *GPU-resident* mesh silently gets
zeros on web. Meshes made from CPU arrays (importer, `ImporterMesh`, `ArrayMesh.add_surface_from_arrays` followed by
using the same arrays) are unaffected. Making `mesh_surface_get_arrays` work would need a CPU shadow of surface data
(a design decision, not a one-liner); leaving it documented.

### 2.16 Test-suite gaps `OPEN` — gaps 5 and 7 closed, gap 4 partly (perf_guard)

Six named gaps, each roughly a session, in rough value order:

1. **Named regression corpus** — no `regressions/` directory exists. Collect the SPIR-V inputs behind
   the bugs this project already fixed (`webgpu_tests/shader_corpus` is the natural home) with
   expected outcomes, so none can silently come back.
2. **Error-path tests** — nothing covers device-lost and recovery, OOM, invalid API use, context loss.
3. **Async readback tests** — nothing covers viewport capture correctness, GPU→CPU readback callbacks,
   or fence correctness under rapid submit cycles.
4. **Performance benchmarks in CI** — `PARTLY CLOSED 2026-10-15`: `perf_guard/run_perf_guard.mjs` asserts per-frame
   ceilings on WebGPU command encoders, submits, `writeBuffer` calls and bytes for four scenes (found measuring §2.17; the
   numbers are adapter-independent so it runs under swiftshader too, `PERF_GUARD_SCALE=0.1` proves it can go red, wired
   into `local_ci.sh`). Frame time itself, shader-compile throughput and texture upload are still unguarded.
5. **WGSL determinism check** — `CLOSED 2026-10-14`: `wgsl_cache/test_determinism.sh` (in `local_ci.sh`) runs the precompiler twice with different `PYTHONHASHSEED`s and compares the headers byte for byte (1,806,893 B, identical, and identical to the build's own header) and requires 0 glsl/tint failures. Found on the way: `thirdparty/...` includes resolved against the **cwd**, so running `wgsl_precompile.py` from another directory silently dropped the FSR shaders (header 1.53 MB) and still exited 0; includes now resolve against the repo root. Original text: nothing proves `wgsl_precompile.py`'s output is byte-for-byte
   reproducible across runs, which now matters twice over since `tint_convert_cli --batch` is also
   driven from the editor's export-time baker with its own fork/mutex concerns.
6. **Safari CI automation** — needs a macOS runner; see §2.8.
7. **No tier ever loads a baked export.** Every export helper in the suite passes `--headless`
   (`scene_smoketest/run_scenes.mjs:350`, `forward_plus/export.sh`), and `--headless` skips the
   export-time shader baker — confirmed 2026-10-14: the same project exports to a 1.09 MB pck headless
   and a 150.8 MB one under `xvfb-run`. So every tier runs `translated: 355, baked: 0` while the
   configuration that ships is `baked: 599, translated: 0`, which is a different code path through the
   container reader, a different load profile, and where §2.27 lives. **Done once by hand 2026-10-14**
   — a baked export of the Forward+ fixture passes 64/64 with deltas matching the unbaked run (§2.27)
   — so what is left is making it a tier rather than a manual run: `export.sh` should bake when
   `xvfb-run` is available and say so when it is not, which also needs the matrix re-baselined under
   it (it was, by that run).

### 2.17 Forward+ benchmark leg and the README's numbers `DONE 2026-10-15` — measured capped and uncapped; post-fx gap fixed *(old Task 9.16)*

**Superseded 2026-10-26 as the source of the README's numbers** by the load-ramp benchmark (`benchmark/run_ramp.sh`,
`webgpu_tests/README.md` §11, first results at the end of §2.30): it reports how much content each leg carries at 20 fps,
so the display cap that flattened every web column below no longer hides the difference. The README's table is generated
from it (`readme_perf_table.py`). The matrix below is kept as measured; its WebGL column was display-capped throughout.

**Done 2026-10-15.** `benchmark/run_matrix.sh` runs the 7 README scenes on four legs — native Vulkan Forward+,
WebGL (official 4.7.1 editor + 4.7.2 template; a 4.8 pck will not load in a 4.7 template), WebGPU/Forward Mobile and
WebGPU/Forward+ (`BENCH_RENDERER`, confirmed from the console banner) — and writes `benchmark/results/matrix.tsv`
plus per-run logs. `run_benchmark.sh` gained `BENCH_RENDERER`, `BENCH_AUTOQUIT`, `GODOT_WEBGL_BASELINE_EDITOR`, and
launches system Chrome with the standard flags. 28 runs, 22 m 43 s, real GPU (RTX 4080 SUPER, 165 Hz display).

| scene | native Vulkan | WebGL | WebGPU Mobile | WebGPU Forward+ |
|---|---:|---:|---:|---:|
| a sprites | 165 | 164 | 165 | 165 |
| b pbr | 165 | 161 | 165 | 165 |
| c instances | 2520 | 160 | 165 | 165 |
| d particles | 165 | 165 | 165 | 165 |
| e animated | 2809 | 154 | 165 | 165 |
| f postfx | 164 | 165 | **96** | **91** |
| g shadows | 2701 | 165 | 165 | 165 |

**What it says.** Every web leg is capped by the display (rAF), so on this hardware the web numbers do *not* separate
the renderers, and the inherited README "4-5x faster than WebGL" claims (Mac Studio, 2025) are not reproduced here —
the scenes are too light for a 4080. Native Vulkan is uncapped on c/e/g (vsync off in those projects), which is why it
shows 2500+. **The one real signal was `scene_f_postfx` (fixed later the same day, see below): WebGPU was ~45% slower than WebGL and native (96 / 91 vs 165)**,
with `draw_calls` 100 (Mobile) / 263 (Forward+) against 262 for WebGL. That is a genuine WebGPU post-processing cost
(SSAO + bloom + SubViewport), consistent with the 105 fps `benchmark_postfx` of §2.6, and is the next place to look
for a perf win. `cpu_avg` reads 0 on web (render-time measurement is disabled there).

**Post-fx gap closed 2026-10-15: `scene_f_postfx` WebGPU 91 -> 165 fps (display cap).** The main thread was 99% busy under
WebGPU (WebGL 42%), so it was CPU-bound; wrapping every WebGPU call showed 8.2 of ~10 ms/frame inside the API:
(1) **`createCommandEncoder` x80 + `submit` x80 per frame, 3.7 ms** — `command_begin_render_pass` finished, submitted and
re-created the encoder before every pass whose attachment also had TextureBinding usage ("proactive encoder isolation"),
i.e. nearly every offscreen pass. Removed (91 -> 123 fps; the Forward+ matrix, scene smoketest, resource lifecycle,
screenshot comparison, font/fog visual, spec-constant and WebRTC tiers all stay green). (2) **`writeBuffer` x14 of 704 KB
per frame, 4.6 ms** — the scene instance buffer is a dynamic-persistent buffer filled through `map_raw_for_upload` and then
flushed whole, so the full per-frame slice went to the queue though only a few KB of instances were written. New
`RDD::buffer_flush_range()` / `RenderingDevice::buffer_flush_range()` (default = the old full flush, so no other driver
changes) used by `_fill_instance_data` / `_render_shadow_process` in Forward+ and Mobile with the bytes actually filled
(123 -> 165 fps). `dev_mode=yes` (werror) build of template and editor clean. Method, reusable: a `Performance.getMetrics`
busy% to decide CPU- vs GPU-bound, then a prototype-wrapping timer over the `GPU*` classes (per-call counts and ms/frame);
keep it, it found both in minutes. A third whole-slice flush turned up through the new perf guard: the canvas instance buffer (2 MB slice, flushed whole every frame in *every* scene, 3D ones included, because of the FPS label/UI). `_canvas_render_items`' end-of-frame flush now passes the used range: per-frame upload 2.1 MB -> 64-350 KB. **Trap:** `run_scenes.mjs --export-only` silently exports nothing unless `GODOT_EDITOR_BIN` and `GODOT_TEMPLATE_ZIP` are set (its default is the macOS editor); a later run then measures stale exports. The perf guard's numbers are only valid after a fresh export.

**Uncapped run 2026-10-15, corrected** (`BENCH_UNCAPPED=1 ./run_matrix.sh` -> `results_uncapped/matrix.tsv`; opt-in, adds Chrome
`--disable-frame-rate-limit --disable-gpu-vsync` and native `--disable-vsync`, beyond the standard launch flags, so off by
default). **The first two uncapped tables were wrong**: the profiler's fixed 180-frame warm-up ends within a second once
frames are uncapped, so it measured the page's startup phase and the numbers swung 2x between runs (PBR Forward+ read 668,
then 301, then 961 for the same export). `benchmark_profiler.gd` now warms up for >= 180 frames *and* >= 6 s and collects
>= 300 frames *and* >= 4 s; native quits itself when done. Valid table (23 m run):

| scene | native Vulkan | WebGL* | WebGPU Mobile | WebGPU Forward+ |
|---|---:|---:|---:|---:|
| a sprites | 1962 | 165 | 1043 | 1031 |
| b pbr | 3350 | 165 | 996 | 901 |
| c instances | 2873 | 160 | 849 | 825 |
| d particles | 3664 | 160 | 1008 | 981 |
| e animated | 2704 | 165 | 946 | 869 |
| f postfx | 866 | 161 | 583 | 414 |
| g shadows | 3086 | 160 | 883 | 851 |

\* WebGL stays at ~165 even with the flags (Chrome's GL present path is not lifted), so that column is *not* uncapped and
no WebGPU-vs-WebGL ratio can be read from it; the README's inherited "4-5x" claim stays unconfirmed.
WebGPU runs at roughly 25-50% of native Vulkan on these scenes. **Forward+ is within 3-10% of Mobile on six scenes; the
earlier "Forward+ slower on particles/instances/shadows" was noise.** The one real gap is post-fx (414 vs 583).

**Profile of the Forward+ vs Mobile gaps (2026-10-15)** — tools: `perf_guard/api_profile.mjs` (main-thread busy %, calls/frame
and ms/frame inside every GPU* method, `--writes` detail) and `perf_guard/cpu_steady.mjs` (steady-state wasm CPU profile by
function; needs a `debug_symbols=yes` template, 184 MB zip, 4 min build). Both scenes are CPU-bound on the main thread
(99-100% busy). Method note: a call that looks expensive (a 44 KB `writeBuffer` at 300 us) is usually the main thread
*waiting* on a busy GPU process, not copying; judge by the engine-side wasm self time.
 - **Post-fx** (Mobile 1.8 ms -> Forward+ 3.0 ms/frame at the time): 79 render passes in both, but Forward+ adds 154 compute passes
   (910 compute `setBindGroup`; SSAO, glow, tonemap/copy and cluster build are compute in Forward+, raster in Mobile), 413
   draws vs 255, 546 push-constant writes vs 123. The biggest *avoidable* part was **`command_copy_buffer`: +0.55 ms/frame
   (20%)**, one `writeBuffer` into a staging block plus one `copyBufferToBuffer` per small update (165 `writeBuffer` calls vs 87).
   Fixed: staging-block uploads are deferred and merged into one `writeBuffer` per block, issued by `_queue_submit()` before
   every submit. +6% Forward+, +9% Mobile. What remains is inherent Forward+ structure (the extra compute passes); 414 vs 583
   is what is left, and native Vulkan runs this scene at 866, so it is heavy everywhere.
 - **Shadows** (Mobile 1.13 -> Forward+ 1.34 ms/frame): Forward+ made 516 draws, 12 passes and **517 push-constant writes** for 518
   draws, where Mobile (169 draws, 96 writes) already used the `firstInstance` path (`API_TRAIT_FIRST_INSTANCE_INDEX`). The
   Forward+ shader already adds `gl_InstanceIndex` to `draw_call.instance_index`, so the same optimization ports directly:
   `RenderForwardClustered::_render_list_template` now sends a single non-multimesh instance's index as `firstInstance` and
   re-sends the push constant only when something else changes. **Heavy scene: push-constant writes 3608 -> 106 per frame**
   (same 3549 draws), 155 fps; shadows +11%, post-fx +12%. Correctness: the heavy scene renders correctly (per-object colors
   and shadows), the Forward+ matrix is 64/64 with 60 of 64 deltas within 3% of before (the rest are the known noisy ones),
   smoketest 21/21, perf guard green. The gap is now 851 vs 883 (4%), i.e. closed.
 - Two non-gaps found on the way: a 1.8 ms/frame floor of `submit` + `createView` (browser side) in every scene, and
   `getCurrentTexture` ~0.4 ms; neither is ours to fix.

**Unbottlenecking the CPU, options 1 and 2 on the heavy scene (2026-10-15).** Baseline (heavy Forward+ scene, uncapped, symbols
build): 176 fps, 5.69 ms/frame, 100% main-thread busy, 2.2 ms inside WebGPU calls, of which the per-draw calls are
`setVertexBuffer` x3371 (0.57 ms), `drawIndexed` x3547 (0.47), `setIndexBuffer` x2018 (0.35), `setBindGroup` x1161 (0.23); engine wasm self time
is small (culling 0.29, graph 0.15, `draw_list_draw` 0.13, instance fill 0.09). `submit` self time 1.4 ms is mostly waiting on the GPU process.
 - **Option 1 (fewer draws) — landed: 3549 -> 1109 draws/frame, 176 -> ~207 fps (+18%), frame 5.69 -> 4.8 ms.** The shadow lists were
   sorted by the *main* material key (priority, shader, material, then mesh), so two meshes with the same shadow variant but
   different materials never sat next to each other and could not merge. Forward+ now sorts shadow lists by the **shadow variant**
   (shadow shader, shadow material set, mesh surface, LOD, double-sided flag; `SortByShadowVariant`) and uses the same key for the
   instancing "repeat" test (`_fill_instance_data(..., p_shadow = true)`), so consecutive same-variant elements become one
   instanced draw. Verified pixel-identical on the heavy scene (max difference 1/255 over 921,600 px), Forward+ matrix 64/64 (61 of
   64 deltas within 3%), smoketest 21/21, perf guard, screenshot comparison 8/8, `dev_mode=yes` template and editor clean.
   Other scenes are unchanged (they have few shadow draws): shadows 851 -> 862 fps (reported draws 337 -> 169), instances 825 -> 816 (noise).
   **Not done:** the depth pre-pass also draws the shadow variant but shares the main list (sorted by material), so it still draws
   per element (~600 here); batching it needs a separate depth ordering. The colour pass is per material by nature. Remaining
   per-draw cost is `setVertexBuffer` x2098: draws alternate between two vertex arrays in material order, so the redundancy
   filter cannot skip them.
 - **Option 2 (`-O2` for the engine) — no gain, rejected.** `optimize=speed` (global `-O2`) on the same scene: 206-209 fps vs
 209-210 with the default `-Os`, and the wasm grows 42.7 -> 53.8 MB (+26%). The time is in the JS/browser call path, not engine
 wasm, so do not spend download size on it.
 - Still the largest lever for draw-heavy scenes: cutting the remaining per-draw calls (depth pre-pass batching, a mesh-first depth
 order), then the marshaling path (`bigintToI53Checked`, `getJsObject`, `wasm-to-js`, ~0.5 ms/frame here), then threading (§2.5).

**Depth pre-pass batching and the `setVertexBuffer` idea (2026-10-15).**
 - **Depth pre-pass batching — tried, measured, reverted (not worth it).** Premise: the plain depth pre-pass draws each element's *shadow
   variant*, so the shadow-variant sort/repeat that batched the shadow lists (above) should batch it too. Two catches. (1) Only the
   *plain* `PASS_MODE_DEPTH` uses the shadow variant; any scene with SSAO, SSIL, SSR, SDFGI, VoxelGI or a normal-buffer user (the heavy
   scene has SSAO + SSIL + SDFGI) runs `DEPTH_NORMAL_ROUGHNESS`, which draws each material's own shader and cannot be merged by
   variant. (2) The instance index is the element's position in its list, so a reordered depth list needs its own instance data
   (a new `RENDER_LIST_DEPTH` with a copied, sorted element list and a second fill). Prototype on `scene_j_forward_plus_depth` (the
   heavy scene without SSAO/SSIL/SDFGI): draws 1055 -> 546, fps 603 -> 622 (+3%, three runs each, +-1.5% noise). The saved calls are
   largely paid back by the extra copy, sort and instance fill, so the change was **reverted**; the J scene stays as a benchmark.
 - **`setVertexBuffer` — the driver already skips per-slot redundant binds** (and redundant index-buffer binds), so the 2098
   calls/frame were real changes: draws alternate between two meshes because the opaque list is ordered priority -> shader ->
   material -> mesh, and every mesh has its own material. **Fix: order by mesh before material on WebGPU**
   (`RenderList::SortByKeyGeometryFirst`: priority, shader, geometry, surface, material, then the low bits; chosen when
   `API_TRAIT_BATCH_INSTANCE_DRAWS`, i.e. WebGPU only; instancing "repeat" still needs the full key so it is unaffected). A mesh change
   costs three calls on WebGPU (two vertex buffers + an index buffer) against one for a material change, so this binds each mesh once
   per shader. Heavy scene: **`setVertexBuffer` 2098 -> 82 per frame, ~207 -> ~220 fps (+6%)**; J scene 603 -> 667 (+10%); instances
   +4%; shadows and PBR flat. Pixel-identical on the heavy scene (max diff 1/255), matrix 64/64 (60 of 64 deltas within 3%, same
   noisy four), smoketest 21/21, perf guard, screenshot comparison 8/8, `dev_mode=yes` template and editor clean. Caveat: opaque draw
   order changes (priority and shader grouping are kept), so coplanar equal-depth surfaces could resolve differently than before;
   none showed up in any tier.
 - Where the heavy scene stands (176 -> ~220 fps across the three changes today: shadow-variant batching, geometry-first order, plus
   the earlier firstInstance): remaining per-draw cost is `drawIndexed` x~550 plus `setBindGroup` x~1100 (one material set per draw, by
   construction in a scene with unique materials) and the ~0.5 ms marshaling path; the next structural step is threading (§2.5).

### 2.30 Unbottleneck the CPU `OPEN` — started 2026-10-15 *(from §2.17's profiling; feeds §2.5)*

**Why:** every WebGPU export is CPU-bound on the main thread (99-100% busy). Light scenes are API-bound (60-70% of the frame inside
WebGPU calls); the heavy scene (3549 draws originally) is engine-bound (~65% wasm). Tools: `perf_guard/api_profile.mjs`
(busy %, calls/ms per GPU method), `perf_guard/cpu_steady.mjs` (wasm CPU by function, needs `debug_symbols=yes`), `perf_guard/run_perf_guard.mjs`
(budgets). Read §3 "Measurement traps" first (time-based warm-up, waiting-vs-copying, stale exports).

**Done 2026-10-15** (all detail in §2.17 and `drivers/webgpu/README.md`): per-pass encoder split removed (80 -> 2 encoders and submits/frame);
`buffer_flush_range` for persistent instance/canvas buffers (2.1 MB -> 64-350 KB/frame); staging-block uploads coalesced; Forward+
`firstInstance` (push-constant writes 3608 -> 106/frame on the heavy scene); shadow lists batched by shadow variant (3549 -> 1109 draws);
opaque list ordered mesh-before-material on WebGPU (`setVertexBuffer` 2098 -> 82/frame). Heavy scene 176 -> ~220 fps; post-fx 91 -> 165 (capped).
**Measured and rejected:** `-O2` for the engine (0% gain, +26% wasm size); depth pre-pass batching via a separate sorted list (+3%, reverted).

**2026-10-16 — the heavy scene is GPU-bound, not CPU-bound; one shader was 12x native.** Fresh non-`dev_mode` build at `17d5ee6a84`.
 - **The CPU numbers above were partly waiting.** Uncapped, the main thread spends ~1.7 ms/frame inside `getCurrentTexture`
   (0.62), `submit` (0.52-0.72) and `createView` (0.30-0.40); capped at 165 Hz those three vanish and the main thread is **47%
   busy = ~2.85 ms CPU/frame** (0.87 ms of it inside WebGPU calls). GPU timestamps (`perf_guard/gpu_time.mjs`, new) put the
   GPU at **3.2-3.3 ms/frame, 77-80% of the GPU timeline**, against **1.04 ms GPU / 0.33 ms CPU on native Vulkan**
   (`run_benchmark.sh native`, `BENCH_UNCAPPED=1`). The GPU process's command thread is only ~26% busy and the GPU's own
   resolution scaling is flat (shadows/SDFGI/fog dominate), so the gap is GPU work Dawn runs, i.e. what our translation produces.
 - **Per pass** (`gpu_time.mjs --passes`, compute pipelines now carry `cpipe#N:<shader>` labels) against native
   `--gpu-profile`: **SSIL 0.60-0.75 ms vs 0.063 native**, SDFGI integrate 0.40 + 0.08 and SDFGI direct light 0.16 (native
   SDFGI sections ~0.12 + part of the 0.19 "GI + depth pre-pass"), the colour pass 0.21 vs 0.23, shadows 0.18 + 0.08 vs ~0.17.
   Everything except SSIL and SDFGI is at native parity.
 - **SSIL — fixed.** Bisected by substituting hand-edited WGSL (`gpu_time.mjs --replace-wgsl`): not the `last_frame` sample, not
   the views (mips are right); the cost was the per-tap helper re-initializing a function-local `array<vec4<f32>, 32>` sample
   pattern on every call. glslang emits an `indexable` Function variable plus a whole-constant `OpStore` before every runtime
   index into a GLSL `const` array; native drivers fold it, after the round trip through Tint and Dawn NVIDIA does not. The
   same constants as a module-scope `const` or `var<private>` take the pass off the top-8. New SPIR-V pass
   **`hoist_constant_local_arrays`** (before `split_initialized_local_arrays`) moves every Function-scope array that only ever
   holds one non-spec constant to a `Private` variable with that initializer, dedupes identical ones, and leaves anything
   written per element alone. 27 arrays hoisted across the precompiled engine shaders; **0** whole-array re-initializations
   remain there. Heavy scene: **GPU 3.3 -> 2.63 ms/frame (-20%)**, SSIL below 0.05 ms, ~225 -> ~235 fps uncapped (now only
   67% of the GPU timeline is busy, so the frame is mixed-bound). Forward+ matrix **64/64** (ssil 1.74, ssao 1.66 vs native), scene smoketest **21/21** (real GPU, Chrome, fresh exports),
   preprocessing tests 244/0 (new test 23 + fixture `constant_local_arrays.comp`), corpus 14/0, driver unit 370/0,
   `wgsl_precompile` 274/0/0. SSAO had the identical pattern but was already fast (the loop shape let the compiler fold it).
 - **Found on the way:** the precompiled header did not depend on the passes, so the fix first reached only runtime-converted
   shaders (§3 trap 20, fixed in `SCsub`).
 - **Also found:** `forward_plus/export_heavy*/` (a 42 MB wasm each) had been committed since `37a7b4359b`; now gitignored and
   untracked. The unpushed range (`acd6ec9ae4`..`f44c233f2b`) was rewritten on 2026-10-16 so it adds no new versions of them
   (hashes in this file updated; the old ones are gone). The versions in the pushed commits `37a7b4359b`, `af3d57ddd1` and
   `37d974ecab` (tag `Pre-optimisations`) remain; removing those would need a rewrite of pushed history and a force-push.

**2026-10-16 (later) — read-write storage textures were always split: the biggest GPU cost after SSIL.** `gpu_time.mjs --passes`
now times copies too (each bracketed by two empty timestamped passes; `--rounds N` pools N windows, since SDFGI's per-frame work
varies and one ~18-frame window was too noisy to compare). The heavy scene ran **40 `copyTextureToTexture` per frame = ~1.3 ms
GPU**: full-texture refreshes of read-write shadows before every dispatch — SDFGI's 289x272x30 `rgba16sint` probe history 8x
(0.46 ms), a 2312x136x8 `rgba16float` 8x (0.35), the 128^3 light/aniso cascades (0.21 + 0.15 + 0.13). Cause:
`has_rw_storage_textures` asked the *device* for `readonly-and-readwrite-storage-textures`, which is a **WGSL language feature**
(`navigator.gpu.wgslLanguageFeatures`), never a device feature, so it was false in every browser and every `read_write` storage
texture was split into write + shadow read. Probed on this Chrome/RTX 4080 with a read-write BGL per format: core allows
r32float/uint/sint, and with `texture-formats-tier2` (already requested) exactly r8unorm/uint/sint, rgba8unorm/uint/sint,
r16uint/sint/float, rgba16uint/sint/float, rgba32uint/sint/float. Fix: `_wgsl_format_supports_read_write()` decides the split
**per format**; capable formats bind as real `read_write` (native semantics: reads see the dispatch's own writes, where the shadow
showed pre-dispatch values). The old flag is left false on purpose — the read-only -> sampled conversion it gates has always run
and is relied on (comment in the header). Only `rg8unorm` (SDFGI aniso1) still needs a shadow.
Heavy scene: **copies 40 -> 3.7/frame, `rwShadowRefreshes` 40 -> 4, GPU 2.63 -> ~1.5-1.7 ms/frame (native 1.04)**, ~235 -> 264-276
fps uncapped (engine stats 246-260); the GPU is now ~43% of the timeline, so the scene is **CPU-bound again**. Forward+ matrix
64/64 (sdfgi 36.65 vs 36.58 before, voxel_gi 7.743, particles within their usual noise), scene smoketest 21/21 Chrome **and**
21/21 Firefox (real GPU), perf_guard green, `dev_mode` `-Werror` clean. Firefox: same per-format rule, through its own
`wgslLanguageFeatures` and tier2 support.

**Per-pass sweep of the other scenes, 2026-10-16** (HEAD `a79c6f242f`; web = `gpu_time.mjs --uncapped`, GPU ms per frame from
timestamps; native = editor `--gpu-profile --resolution 1280x720`, same renderer; native's sections exclude the final blit/present):

| scene | web fps | web GPU ms | native GPU ms | web GPU % of timeline |
|---|---:|---:|---:|---:|
| benchmark_sprites (Mobile) | 984 | 0.08 | - | 8% |
| benchmark_pbr | 962 | 0.24 | 0.08 | 23% |
| benchmark_instances | 900 | 0.24 | 0.02 | 21% |
| benchmark_particles | 1000 | 0.23 | 0.04 | 23% |
| benchmark_animated | 928 | 0.29 | 0.02 | 27% |
| benchmark_postfx | 610 | 0.47 | 0.80 | 29% |
| benchmark_shadows | 868 | 0.26 | 0.11 | 23% |
| benchmark_batching (60k nodes) | 92 | 0.31 | 0.51 | 3% |
| heavy_i (Forward+) | 241 | 1.68 | 1.06 | 41% |
| depth_j (Forward+) | 647 | 0.55 | 0.45 | 36% |
| demo 3d lights_and_shadows | 870 | 0.28 | 0.32 | 25% |
| demo 3d particles | 873 | 0.22 | 0.14 | 19% |
| demo 3d platformer | 812 | 0.29 | 0.22 | 24% |
| demo compute texture | 903 | 0.21 | 0.05 | 19% |
| demo 2d particles | 844 | 0.26 | 0.10 | 23% |
| volumetric_fog | 942 | 0.21 | 0.09 | 20% |

**No new GPU outlier.** Every scene except heavy_i is CPU-bound with the GPU at 3-36% of the timeline. The small scenes' extra
~0.15 ms is fixed per-frame work, not shader cost (`benchmark_instances --passes`: blit 0.019, an empty pass clearing the 4096^2
shadow atlas 0.032, opaque 0.055 vs 0.024 native). heavy_i's remaining gap is SDFGI (item 0 below). `benchmark_batching` (60,000
MeshInstance3D, ~15 instanced draws) is engine-CPU-bound by design: ~0.8 ms of its ~11 ms frame is in WebGPU calls, and the
native *editor* build runs it at 74 fps (web release template 92). compute_heightmap renders on demand, so it has no frame rate
to measure. **Capped CPU (165 Hz):** heavy_i 47% busy (~2.85 ms, 0.89 in WebGPU calls), 3d platformer 21% (~1.27 ms, 0.29),
benchmark_pbr 7% (~0.42 ms, 0.11) — so 70-77% of the main thread's work is outside WebGPU calls; attribute it with
`cpu_steady.mjs` (needs a `debug_symbols=yes` template) before choosing a CPU fix. The light scenes' uncapped ~1 ms/frame floor
is the browser's rAF/present cadence, not our work (benchmark_pbr needs 0.42 ms).

**CPU attribution (option 1) and the JS-glue question (option 3), 2026-10-16.** `debug_symbols=yes` template (184 MB zip, 4 min),
`cpu_steady.mjs` **capped at 165 Hz** (uncapped profiles count blocking waits as work), self time bucketed by kind:

| | heavy_i | 3d platformer |
|---|---:|---:|
| main thread busy | 1.99 ms/frame | 1.31 ms/frame |
| engine wasm (C++) | 1.20 (60%) | 0.70 (54%) |
| browser WebGPU methods | 0.42 (21%) — `writeBuffer` 0.17, `setBindGroup` 0.11 | 0.16 (12%) |
| Emscripten JS glue | 0.19 (10%) — `getJsObject` 0.07, `wasm-to-js` 0.03, `bigintToI53Checked` 0.015 | 0.10 (8%) |
| V8 `(program)` | 0.14 (7%) | 0.15 (11%) |
| other JS | - | **0.15 (11%): Web Audio `setVolume` + `_syncPlaybackRate`** |

Engine wasm on heavy_i by subsystem: `RendererSceneCull` 0.30 (`_render_scene` self 0.23, i.e. inlined light/shadow culling for 18
shadowed lights), `RenderForwardClustered` 0.21, `RenderingDevice` 0.15, `RenderingDeviceGraph` 0.14, our driver 0.10, sorting 0.06.
Native's whole CPU render time for this scene is 0.33 ms, so engine wasm runs ~3.6x native here. The platformer has no hotspot
(Jolt, animation, GDScript, rendering each 0.02-0.07).
 - **Option 3 (faster glue) — measured, not worth a toolchain patch.** The glue is already one object-table lookup plus the call
   per WebGPU method. Tried in an exported `index.js`: making `WebGPU.Internals.jsObjects` a `Map` instead of the sparse array cut
   `getJsObject` 0.072 -> 0.056 but made `jsObjectInsert` 0.015 -> **0.372** ms/frame (~100 pass/command encoders are created and
   freed per frame), busy 2.05 -> 2.41 ms. Realistic ceiling of any glue patch: 0.02-0.05 ms/frame (1-2%). The only larger form
   is batching a pass's commands into one JS call (ceiling ~0.15 ms, 7%, on heavy_i) — a driver redesign, not done.
 - **Web Audio — fixed.** The engine resends every playing sample's 6 channel gains and its pitch every frame; each assignment
   to an `AudioParam.value` costs. `library_godot_audio.js` now caches the last-written gains (`SampleNodeBus._setGain`) and
   playback rate (`SampleNode._syncPlaybackRate`, reset when `_restart()` creates a new source so the old timing is kept), and
   `setVolumes` passes `subarray` views instead of copies. Platformer: **0.148 -> 0.029 ms/frame** (what is left are real changes,
   3D attenuation as the camera moves). Smoketest platformer / stress / 2d platformer pass on the real GPU. No tier checks audio
   *output*; the change only skips writes of an unchanged value. Upstream-able as is. **Later 2026-10-16:** upstream PR
   godotengine/godot#123996 ("web: Various fixes to the Sample audio driver", unmerged, rewrites `library_godot_audio.js` as a
   closure with native `loop`/`loopStart`/`loopEnd` and a pitch-aware position worklet) is cherry-picked as `16a69c7728`; it
   still wrote every gain and pitch (plus a worklet `setValueAtTime`) each frame, so the skip-unchanged cache is re-applied on
   its structure (`createVolumeMixer`'s `setGain`, `syncedPitchScale`). The `web-audio/skip-unchanged-audioparam-writes`
   branch is superseded by that PR for upstream purposes.
   **2026-10-17:** the PR author asked how the glue was profiled; the method is now a standalone tool,
   **`tests/web_audio_profiling.mjs`** (zero dependencies, Node 22+): it attaches to an already open Chrome tab over the
   remote debugging port, injects a rAF frame counter, takes a `Profiler` sample at 200 µs and prints self/inclusive
   ms/frame per function (`--fn`, default `setVolume,_syncPlaybackRate`; the PR's driver names them `setGain`,
   `setPitchScale`). Usage in its header. Re-measured with it on the pre-PR platformer export: `setVolume` 0.115,
   `_syncPlaybackRate` 0.045 ms/frame self (13% of main-thread busy); with the cache on the PR's driver `setGain` 0.023 +
   `setPitchScale` 0.010. Testing it outside the repo's servers found that **PR #123996 breaks every non-isolated
   `threads=no` export** (`SharedArrayBuffer` referenced unguarded, §3 trap 22) — fixed here in `8bd750b327`, to be
   reported upstream; all eight templates relinked with the guard.

**Engine wasm CPU vs native, 2026-10-16.** Two levers measured with the capped CPU metric (`cpu_steady.mjs` busy ms/frame,
two runs each, ~5% run-to-run noise), heavy_i / 3d platformer:

| template | heavy_i | platformer | wasm |
|---|---:|---:|---:|
| `-Os`, no LTO (what every build here and in CI produces) | 2.08 / 2.20 | 1.01 / 1.10 | 41 MB |
| `-Os` + `lto=thin` (official `production=yes` templates get LTO) | 2.27 / 2.11 | 1.05 / 1.14 | 42 MB |
| `optimize=speed` (`-O2`) + `lto=thin` | 2.05 / 2.12 | 0.99 / 1.07 | 60 MB |

**Neither moves CPU time**; -O2 costs +46% download. (This re-test matters: the earlier "-O2 gave 0%" was read off uncapped fps
while the scene was GPU-bound, so it could not have shown a CPU gain. Now it is confirmed on CPU time.)
**The "3.6x native" was wrong:** the native 0.33 ms is `viewport_get_measured_render_time_cpu()`, which covers only the measured
render section. Whole main-thread CPU natively (editor binary, `/proc/<pid>/task/<pid>/stat` utime+stime over 10 s) is
**0.66 ms/frame** (whole process 0.92) at 909 fps, against web engine wasm ~1.25 ms of a ~2.1 ms main thread. So engine wasm is
~1.9x native's *entire* main thread (which also includes NVIDIA's user-mode driver) — an ordinary wasm-vs-native ratio, not a
pathology. On web the same profiler metric reads 0.00 (it needs driver timestamps), so there is no like-for-like there.
A per-function native comparison needs `perf`, which `kernel.perf_event_paranoid=4` blocks for a normal user
(`sudo sysctl kernel.perf_event_paranoid=1` to allow it). What would still pay on the CPU: algorithmic work in the engine's hot
paths (scene/light culling 0.30, Forward+ list building 0.21, RenderingDevice + graph 0.29 ms/frame on heavy_i — it would help
native too), or overlapping it with game logic via threading (§2.5).

**Function-by-function vs native, and the algorithmic fixes it found, 2026-10-16.** Native: `template_release debug_symbols=yes
disable_path_overrides=no` (release templates refuse `--path` otherwise) run on the project dir at `--max-fps 165`, `perf record -F
4999 -t <main tid>` (`kernel.perf_event_paranoid=1`; the stripped editor binary resolves no symbols). Web: `cpu_steady.mjs` on a
`debug_symbols=yes` template. Matched by function name, ms/frame (scratch `compare.py`).
 - **The outlier was a real engine bug, not wasm codegen.** `RendererSceneCull::_render_scene` self time: web 0.234, native 0.001.
   A temporary counter showed web re-culling and re-rendering **9 of the heavy scene's 18 positional shadows every frame** (native: 0).
   `LightStorage::shadow_atlas_update_light()` returns `should_redraw` without storing the light's version when a light is due for a
   reallocation (wants another quadrant, 500 ms tolerance passed) but no slot is free. If the version changed while the light was in
   that state, it never converges and the light redraws forever. A light is still dirty from creation for the first frames, so any
   long early frame triggers it — every web export has one (shader compilation). **Reproduced natively** with one 1.5 s
   `OS.delay_msec()` on frame 2 or 3 (2700 redraws / 300 frames; frames 1 or 5: none). Present in `upstream/master`. Fix: store the
   version on that path too (`a87c84d4a5`; upstream-ready branch `shadow-atlas/store-version-when-realloc-fails`, applies cleanly to
   upstream). Heavy scene: **CPU 2.08-2.20 -> 1.34 ms/frame (-37%)**, engine wasm 1.29 -> 0.75, render passes 31 -> 13, GPU 1.70 -> 1.52,
   pixel-identical (max diff 1/255); platformer 1.01-1.10 -> 0.90. Forward+ matrix 64/64, smoketest 21/21.
 - **After it:** web engine wasm 0.90 ms vs native's whole main thread 0.77 (Vulkan driver included); engine functions mostly 1-3x.
   The web-only rest was this driver's bind-group path (~0.14 ms) and RenderingDevice's per-set `prepare_for_use` records:
   - New trait `API_TRAIT_UNIFORM_SET_PREPARE_FOR_USE` (base default 1; WebGPU 0). RD recorded a graph instruction plus a virtual no-op
     driver call for every bound set of every draw/dispatch because WebGPU must report `HONORS_PIPELINE_BARRIERS` 0 (D3D12 without
     enhanced barriers really needs it). RD now caches the combined condition at init (`uniform_sets_need_prepare_for_use`), which
     also drops a virtual `api_trait_get` per set per draw. `_run_draw_list_command` 0.043 -> 0.030.
   - `WGUniformSet` keeps a 4-entry linear-search front for `rebind_cache` (cleared with it in `_purge_rebind_caches_for_shader()`):
     the hash lookup was 0.045 ms/frame. A one-entry most-recent slot never hit, because a material set is bound once per pass and
     each pass binds it against a different variant's layout. Now 0.000.
   Together ~0.04 ms/frame (the targeted functions 0.234 -> 0.198); the whole-frame number is inside this machine's current noise.
 - What is left is flat: `draw_list_draw` 0.089 (~80 ns/draw), `_scene_cull` 0.061 (2.4x native), `_fill_instance_data` 0.061
   (2.2x), browser `setBindGroup` 0.10 and `writeBuffer` 0.09, V8 0.15. No single item is worth a change of the size above;
   the next real CPU lever is overlapping this with game logic (§2.5 threading).

**Open, in suggested order (re-ordered 2026-10-16: GPU-side first, it is where the heavy scene's time is):**
0. **SDFGI: mostly done 2026-10-16 (below).** What is left: SDFGI integrate 0.47 ms in 4 small dispatches (111 workgroups each)
   that WebGPU must fence against each other (all four bind the shared, writable `ambient_texture`; natively they sit in one
   compute list without barriers and overlap). Merging them into one dispatch means binding the per-cascade history/average
   textures together (an array texture or atlas) -- an engine change in `gi.cpp` + `sdfgi_integrate.glsl`, worth ~0.3 ms GPU on a
   frame that is now CPU-bound. Also §2.21 item 1 (shareable formats: the light cascades are `rgba16float`, 2x native's packed
   format). The `WEBGPU_DECLARE_TEXTURE3D_ARRAY8` if-chain is **not** a web-only cost: native compiles the same GLSL.
   Then a `gpu_time.mjs --passes` sweep of other scenes for more "fine natively, slow after Tint" shaders.
1. ~~**Marshaling path**~~ measured 2026-10-16 (above): the glue is ~10% of CPU and a faster object table is a net loss; not worth a toolchain patch.
   emdawnwebgpu. Options: fewer calls (below), or a toolchain patch with fast paths for `setBindGroup`/`setVertexBuffer`/`drawIndexed` (same upkeep burden as `misc/emsdk_patches/`).
2. **Remaining per-draw calls on the heavy scene**: ~550 `drawIndexed` and ~1100 `setBindGroup` (one material set per draw in a unique-material scene). Ideas: skip rebinding a
   material/uniform set that did not change (bind-group skips are 0 here — check whether the tracker compares sets, not just slots); batch same-material runs.
3. **Normal/roughness depth pre-pass** (used with SSAO/SSIL/SSR/SDFGI) still draws per element with each material's own shader; not batchable by variant (see §2.17).
4. **Frame-time guards** in `perf_guard` (it only guards call counts and bytes today) and an **uncapped WebGL leg** (needs another harness; Chrome's GL present path stays at ~165). The WebGL half is done 2026-10-26 by the load-ramp benchmark below; the frame-time guards are still open.
5. ~~**Threading** — §2.5, after 1-2 are re-measured.~~ Done 2026-10-16: §2.5 closed, separate render thread is the default.
**Not worth repeating:** engine `-O2`/`optimize=speed`; global INITIAL_MEMORY; depth-list copying; assuming a slow-looking `writeBuffer` is a copy (it is usually the main thread waiting on the GPU process).

**Load-ramp benchmark, 2026-10-26 (work task 20) — the WebGL comparison item 4 asked for.** `benchmark/run_ramp.sh`
grows one kind of content per ramp until the frame rate falls to 20 fps and reports how much it took (N), so WebGL's
display cap stops mattering; `readme_perf_table.py --write` turns a result into the README's performance table.
Method, legs and the fairness rules in `webgpu_tests/README.md` §11; traps 66-67. First measurement
(`results_ramp/2026-10-26/`, RTX 4080 SUPER, Chrome 156, 2 runs, all 60 ramp-runs converged, spread <= 6.5% outside
Firefox): WebGPU Forward+ carries **1.23x WebGL's draw calls, 1.61x its moving nodes, 1.50x its particle emitters, 6.4x
its shadowed PBR spheres, but only 0.67x its full-screen fill**; against native Vulkan Forward+ it is 0.45x (draws),
0.75x (nodes), 0.67x (fill), 0.99x (lit), 0.31x (particles). Open questions, in order:
1. **Fill is 0.67x on WebGPU while WebGL and both native renderers agree at 1,076 layers.** Native Vulkan Forward+ uses
   the same RGBA16F target, so suspect the generated WGSL for the fill shader's loop (SPIRV-Cross output, precision)
   or the pacing; compare `ramp_browser.mjs --no-pace` and `gpu_time.mjs --passes` on the fill export.
2. **Particles 0.31x native** (2,091 vs 6,661 emitters): per-emitter compute dispatch cost on WebGPU? Profile with
   `api_profile.mjs`.
3. **Lit 6.4x WebGL is mostly the Compatibility renderer, not the API**: native Compatibility is also 0.26x native
   Forward+ there (it draws every shadowed light as another pass and does not instance), so quote it as Forward+ vs
   Compatibility. Draws and nodes are the cleanest WebGPU-vs-WebGL API comparison.
4. **Firefox** draws varied 13,967 / 26,153 between runs (61%): needs 3+ runs before quoting.

### 2.18 Texture compression as an export option `DONE 2026-10-20` — per-family texture packs *(old Task 39)*

Desktop is settled: BC formats work in both browsers, BC1 equals uncompressed at RMSE 0.0006. The export
options themselves (`vram_texture_compression/for_desktop` / `for_mobile`) were already upstream; what was
missing is that exporting both put both families in the main pack, so **every player downloaded every texture
twice** to serve desktop and mobile from one export.

**Landed 2026-10-20:** `vram_texture_compression/separate_packs` (default on, shown when both families are
exported). The web exporter's save function diverts imported `*.s3tc|bptc.*` files into
`<name>.textures_bc.pck` and `*.etc2|astc.*` into `<name>.textures_etc2_astc.pck`
(`EditorExportPlatformWeb::_save_pack_file_split_textures()`). The shell's `Engine.pickTexturePack()` picks one
from the WebGPU device's features (WebGL extensions under Compatibility), preferring BC as the engine does (the
importer lists BC first), and preloads it as `<main pack>.textures.pck`, which `ProjectSettings` mounts beside the
main pack. `ResourceFormatImporter` now skips a `path.<feature>` variant whose file is not there, so a device that
supports a family whose pack it did not get (ASTC-only with an ETC2 texture, say) falls through to one it has.
Not with an encrypted pack (same rule as the shader pack).

Verified in headless Chromium with the platformer's 28 3D textures forced to VRAM compression: BC device →
only `textures_bc` fetched, 28 `.s3tc.ctex` loaded; BC stripped from the device → only `textures_etc2_astc`,
`.etc2.ctex` loaded natively; no compression feature at all → `textures_bc`, CPU-decompressed (bcdec), as
before. **Still unmeasured:** Safari/iOS (§2.8) and a real Android device (§2.9) — the formats there are what the
option is for.

### 2.19 Interleaved-build stale object — the real fix `OPEN` — NOT REPRODUCED 2026-10-15 *(old Task 40)*

**2026-10-15:** on `webgpu-4.8` an editor build followed by a non-dlink web release build, then a dlink one, all in one tree, gave clean templates both times (`initialize_betsy_module` count 0, `register_module_types.gen.*.o` regenerated after each header change; `modules/SCsub` ends with `env.Depends(lib, env.Value(env.module_list))`). The workaround stays documented until it recurs; if it does, capture `scons --debug=explain` for the stale object before touching anything.

The workaround is in §1 and is reliable; the underlying SCons dependency gap is not fixed.
`register_module_types.gen`'s object does not get rebuilt when the platform/target changes, so the
template links against a stale one and the export dies in `callMain()` with `TypeError: resolved is
not a function` and no Godot-level message at all. Worth a real dependency fix because the symptom
names nothing and costs an hour every time it is met fresh.

### 2.20 `SUPPORTS_HALF_FLOAT` experiment `OPEN` *(old Task 46 §5)*

`has_feature(SUPPORTS_HALF_FLOAT)` is hardcoded `false` ("not reliably available"), but `shader-f16`
is available in Chrome on most desktop hardware and the driver already demonstrates the right
pattern (`has_rw_storage_textures` queries `device.features.has(...)` at runtime). It would halve
bandwidth and register pressure in shaders that use it and open FSR2's FFX_HALF path. **Not a safe
flip**: it changes which variants the engine generates, the baker's `get_target_feature_overrides()`
has to agree, and §4.16's catalog of benign bake failures needs re-checking. Real upside on mobile.

### 2.21 Deferred emdawnwebgpu feature adoption `OPEN` *(old `emsdk-upgrade.md` Phase 5)*

**Prioritized by the user 2026-10-25** (with §2.37), after an evaluation of emdawnwebgpu's alternatives found none
better: these are capabilities emdawnwebgpu already gives us. Context from the converter swap (§2.35): SPIRV-Cross patch
0006 already emits 16-bit norm *storage* formats where the device has them, and Firefox's naga rejects CanvasSdf's
`r16snorm` storage texture without `STORAGE_TEXTURE_16BIT_NORM_FORMATS` (the driver remaps it, §4.14) — item 2 must keep
that runtime fallback. Firefox fails `sdfgi` parity with both converters (task 11, 2026-10-25); check item 1 against it.

Four items deferred during the Emscripten 4.0.11 → 6.0.9 upgrade (§4.21), each independently
landable — and each to be landed and verified *separately*, because a regression from one must never
be diagnosed alongside another's:

1. **Revisit the SDFGI shareable-format workaround** now that texture views can override `usage`.
   Prototype the narrower-usage view (dropping `StorageBinding` for the read-only reinterpreted
   view) that Phase 9 Round 21 identified; if native shareable formats now work, weigh reverting
   `RD::SUPPORTS_SHAREABLE_TEXTURE_FORMATS` to `true` on WebGPU, which deletes a WebGPU-specific
   branch in `gi.cpp` plus its decoded-storage fallbacks. Needs a live SDFGI regression check.
2. **Native 16-bit norm texture formats** (`R16`/`RG16`/`RGBA16` `Unorm`/`Snorm`) behind the
   `unorm16-texture-formats` device feature, keeping the Float-equivalent fallback as a **runtime**
   capability check. `pixel_formats_webgpu.h`'s "no upgrade path currently exists" comment is now
   stale. **Caveat that already bit once**: a native `R16_SNORM` attempt made things worse, because
   Dawn reports its sample type as `UnfilterableFloat` while the canvas SDF uses a filtering sampler.
3. **Multi-draw-indirect has no number.** The capability-gated fast path
   (`WGPUFeatureName_MultiDrawIndirect`) is functionally verified but was never measured on a
   high-indirect-draw-count scene, which is what its own acceptance criterion asked for.
4. **Subgroup capability reporting stays disabled.** The device feature is used internally by
   built-in shaders; only the user-facing capability query still reports 0. Reporting real values is
   a deliberate later follow-up.

### 2.22 Small perf items `DONE 2026-10-15`

- **`INITIAL_MEMORY` is 32 MB with `ALLOW_MEMORY_GROWTH=1`. Measured 2026-10-15: not worth changing.** Wrapping
  `WebAssembly.Memory.prototype.grow` over a 25 s load shows 9 grows for the cameraSim project (final 169 MB) and 11 for
  the heavy Forward+ scene (final 238 MB), costing **0.1 ms in total** — Chrome grows linear memory without copying. The
  `initial_memory=` SCons option already exists if a deployment still wants it; the default stays.
- **The 45 MB side module's levers are module-level** (`module_*_enabled=no`) and transport
  compression — `-Os` + thin LTO is already production, and `EXPORT_ALL=1` applies to the 1.3 MB
  *main* module, so neither is the cause. Compression is now measured: 45.3 MB → 11.9 MB gzip → 9.5 MB
  brotli, and end to end it is worth 3.5× on the whole load (§4.15). **README now says to serve compressed (2026-10-15); `webgpu_site` still does not, and none of the local servers did until
  `profile_phases.mjs --compress`. That is the cheapest load-time win available to anyone deploying
  this and it costs no engine work.
- Recorded so they are not "found" again: `_check_device_lost()`'s per-frame `EM_ASM_INT` is ~1 µs
  (0.006% of a 60 fps budget); subpass post-processing is off under `WEB_ENABLED` and costs one extra
  render pass, unfixable without input attachments; `uniform_set_create()`'s nested linear scans sit
  behind the engine's own uniform-set cache and every RD backend pays the same shape of cost.

### 2.23 Box3D remainders `OPEN`

`modules/box3d_physics` is the default 3D engine and validated against Jolt and GodotPhysics3D
(§4.6).

**`precision=double` checked 2026-10-14 — it builds and it runs.** `scons platform=linuxbsd
target=editor precision=double` links clean (`BOX3D_DOUBLE_PRECISION` reaching both the module and
the vendored library through `SCsub:78`), all three Box3D objects compile, and the only warnings in
the build are GCC ABI notes from Jolt, not Box3D. Behavior checked too, not just the compile: a
headless scene dropping a sphere onto a static box settles at **y = −0.0001 under both `double` and
single precision** (Jolt, same scene and build, settles at −0.0200), so double precision changes
nothing observable. Fixture kept out of the repo — it is six lines of GDScript, and
`LIVE_REPRO_METHODOLOGY.md` is the pattern for re-making it. The feature gaps are deliberate and reported at runtime
through `Box3DDiagnostics::report_unsupported()` — soft bodies (`SoftBody3D` is inert), generic 6DOF
joints (approximated when the locked axes allow), joint motors/springs beyond hinge motors — and
closing any of them is open-ended work, not a task. Remember the approximations when comparing
against another engine: cylinders are 32-sided prisms, `WorldBoundaryShape3D` is a 2000 m box, height
maps are triangle meshes, and damping uses Box3D's formula rather than Godot's.

### 2.24 Upstream contribution of the driver `OPEN` — human

A PR description was drafted long ago (§5) and the Rust dependency was removed specifically to make
upstreaming possible (§4, naga→Tint). Nothing has been offered upstream. Strategic, human-owned, and
the four-year-old proposal #6646 is still the entry point.

### 2.25 Side project: Node dedicated-server backend — not started

Evaluated 2026-10-12 against `webgpu-4.8`. **Verdict: feasible and worth building, but not as "a
geckos.io server"** — build a Node orchestrator and transport on `node-datachannel` that presents
itself to Godot as a stock WebRTC peer (three negotiated channels, ids 1/2/3, raw payloads), keep
authoritative match simulation in headless Godot and authoritative metagame in Node. The Godot client
is then stock `WebRTCMultiplayerPeer` plus a GDScript signaling addon, so `@rpc` works unchanged on
web and native. Nothing was built or run; every claim in the retired docs was labeled *verified* /
*documented* / *inferred*, and the inferred ones need a Phase 0 spike. The week-by-week schedule and
the measured answer to "how much would a Node-side simulation buy me" are in git history (§5), and
the whole thing depends on §4.20's F3 finding. Human-scoped: weeks, and a product decision first.

### 2.26 Braced GDScript (`.gdb`) — DONE, see §4.33

### 2.31 Forward+ gaps WebGPU itself cannot close `OPEN` — spec-limited; documented, each warns or degrades *(found by §4.29's sweep)*

What is left after §4.29's sweep, every one checked against the driver and the spec rather than the old notes. None is a
driver bug; each needs a WebGPU feature that does not exist yet or a renderer-side redesign. Listed with what happens today:

| gap | today on WebGPU | what would close it |
|---|---|---|
| SDFGI voxelization's image atomics (`imageAtomicOr` on the shared facing grid) | plain read-modify-write; occasional lost facing bits, measured 0.028 mean diff static, 0.025 moving | WGSL texture atomics (proposal only), or a buffer-backed grid in `sdfgi_preprocess.glsl` |
| MSAA 2x / 8x | WebGPU allows sample counts 1 and 4 only; `_clamp_sample_count()` makes every MSAA mode 4x | spec |
| Wireframe / `polygon_mode` line and point | no polygon mode in WebGPU; draws filled | a line-list index rewrite for debug draw, if anyone wants it |
| Variable rate shading, multiview (XR) | not exposed | spec |
| Sampler border colour | clamp-to-edge; the two engine readers that relied on a 0 border (contact shadows, §4.29) emulate it in GLSL | spec; user shaders with `repeat_disable` + border get edge clamp |
| `alpha_to_one` | ignored | spec |
| Separate front/back stencil compare/write masks | front-face masks used for both, `WARN_PRINT_ONCE` (ops and compare are per face) | spec |
| HDR output (`hdr_2d`, HDR swapchain) | 8-bit sRGB canvas | `GPUCanvasToneMapping` `extended` + `rgba16float` canvas, not wired |
| More than 48 sampled textures (or 8 storage textures) in one stage | arrays beyond the per-stage budget stay collapsed to element 0, with a one-time `WARN_PRINT` naming shader, set and binding | adapter limits; only a pathological custom shader hits it |
| 16-bit unorm/snorm sampling (`R16`, `RG16`, `RGBA16` unorm/snorm) | `texture-formats-tier1` makes them unfilterable on Dawn, so they stay unsupported and the engine's fallbacks are used | a filterable tier |
| FSR2 | runs through the `NO_IMAGE_ATOMICS` buffer path; edge-only diff 0.55 vs Vulkan (fp16 vs fp32 resolve) | `SUPPORTS_HALF_FLOAT` work, §2.20 |

Follow-ups from the same sweep, none of them a known breakage:

1. **Re-measure omni shadow cost.** `API_TRAIT_FORCE_OMNI_DUAL_PARABOLOID` is 0 now, so omni lights default to cube shadows
   (6 passes per light instead of 2). The 133 → 76 ms win in `webgpu_site/PERFORMANCE_AND_OPTIMIZATION.md` was measured with
   the override on; the shadow-stress scene (32 omni lights) has not been re-run. `perf_guard` and the smoketest stayed green.
   If it regresses badly, the answer is a per-project opt-in, not forcing it back for everyone.
2. **Report the `POINT_COORD` bug upstream** (human). A material that reads `POINT_COORD` without writing `POINT_SIZE` draws
   nothing on any driver without `SUPPORTS_POINT_SIZE` (D3D12 too); the fix is the three-line `#define` in both scene
   `_inc.glsl` files.
3. **Name the 4x4 depth texture in a float slot.** A `--verbose` run of the fixture logs "a 4x4 depth texture is bound where the
   shader declares a float texture" a few times per frame. 4x4 is the size of Godot's default depth placeholder, so it is
   probably harmless, but it was not traced; the message now names shader, set and binding, so one `--verbose` run answers it.
4. **No dedicated test for the lightmap and VoxelGI arrays.** `texture_array` exercises the expansion through a material; a
   scene with several lightmaps or VoxelGI probes would cover the engine's own arrays.

### 2.32 Sample playback's remaining gaps: bus effects and streaming `OPEN` — deferred 2026-10-18 *(from §4.30)*

Web sounds default to Sample playback (`audio/general/default_playback_type.web`), which Web Audio plays outside
`AudioServer`. After §4.30 it matches `AudioServer` on everything it does, but two things it cannot do at all:
**bus effects** (reverb, EQ, compressor, and the analysis ones — capture, spectrum, record — get no data) and
**streaming** (a sample is decoded whole on its first `play()`: a hitch, and the track in memory as 32-bit floats).
`playback_type = Stream` per player is the native mixer and has neither gap. Decided 2026-10-18 to leave the defaults
alone for now and make the gaps visible instead, so they are not reported as bugs:

- export: the web export logs a warning naming every bus with enabled effects while `default_playback_type.web` is Sample;
- runtime: `AudioServer::_warn_sample_bus_effects()` warns once per bus when a Sample sound starts on a bus whose send
  chain has effects, and `AudioDriverWeb::register_sample()` warns about streams over 30 s (`LONG_SAMPLE_SECONDS`).
  On web both go to `console.warn` (WebLogger); natively they never fire. `run_e2e.mjs` requires each exactly once.

Options when this is picked up:

| approach | pros | cons |
|---|---|---|
| **Auto-route `Default`**: threaded exports resolve it to Stream everywhere; single-threaded ones to Stream only for players whose bus chain has effects or whose stream is long, with a bigger mix buffer on that path | exact native parity wherever it applies (every effect, capture/spectrum, streaming, generators); small change (one decision point, plus `AudioStreamPlaybackPolyphonic::play_stream`); explicit `playback_type` still wins | on single-threaded exports Stream mixes on the main thread: a long frame (shader compile, GC) underruns the 512-frame ring buffer and crackles, hence the bigger buffer, which adds latency (~85 ms at 4096 frames) to those sounds; a bus that gains effects after a sound started does not move it; a behavior change for every web game |
| **Web Audio effect equivalents** on the sample bus graph | never touches the main thread; keeps Sample's latency | approximate: EQ, filters, delay, amplify, panner, distortion can come close (`BiquadFilterNode`, `DelayNode`, `WaveShaperNode`); reverb (`ConvolverNode` with a generated impulse vs Godot's Freeverb), chorus, phaser, compressor, limiter would sound different; capture/spectrum/record impossible (no data path back); a large job, and each effect needs its own parity test like `run_tests.mjs` |
| **Godot's own effect DSP in a Wasm audio worklet** (`-sAUDIO_WORKLET`) | exact effects with Sample's off-main-thread mixing | threads-only (shared memory); the effect instances would run outside `AudioServer`'s locking; the biggest job of the three |
| **Both** auto-route and Web Audio effects | Stream parity where it is safe, approximate effects elsewhere | the cost of both |

`webgpu_tests/web_audio/` is where any of these would be tested: `run_tests.mjs` for DSP against native ports,
`run_e2e.mjs` and the `audio_parity` smoketest scene for the engine.

### 2.33 MetaHuman importer: DNA rig, clothing, hair `DONE 2026-10-20` — rig, clothing, strands, shell, simulation; open: an M1 run, wrinkle masks

Import an Epic MetaHuman DCC-style export as a rigged Godot scene — body and face rig, clothing, hair — with hair either
dynamic or baked to a mesh. **Target: 90 fps or better on an M1 MacBook and a mid-range laptop, in a WebGPU export.**
The only sample is `~/Downloads/Ava.zip`, given to the user as an example; who or what exported it is unknown, so the
importer must not depend on anything only that exporter writes.

**What an export holds** (measured on Ava, 2026-10-20):

| part | format | numbers |
|---|---|---|
| head rig + meshes | `head.dna` (DNA 2.5, `MH.6`) | 8 LODs, 870 joints (840 at LOD 0, 84 at LOD 4), **0 blend shapes** — the face is joints only; up to 12 influences per vertex; 174 GUI → 263 raw controls, 545 PSDs, 82 animated-map channels, 1 RBF solver (neck correctives); 50 meshes (head, teeth, saliva, eyes, eye shell, lashes, lacrimal edge, cartilage per LOD) |
| body rig + meshes | `body.dna` (DNA 2.8, `MHB.1`) | 4 LODs, 342 joints, 176 raw controls, 72 RBF solvers + 8 twist / 8 swing setups (correctives), body LOD 0 30,455 vertices |
| clothing | `Clothing/outfits.fbx` | one mesh skinned to the same 342-joint body skeleton, 2 used materials |
| grooms | 4 Alembic (Ogawa) files | hair 100,498 strands / 3.86 M points from 1,280 guides; fuzz 44,550 strands; eyebrows 1,915; eyelashes 787. Guide interpolation (`groom_sim_curve_a/b`, weights, per-vertex lerp) is already in the files |
| assembly | `CharacterAssemblyManifest.json`, `ExportPlan.json`, `Geometry/*.json` | components, materials (hair melanin/redness, fabric colors), groom physics parameters, and `visible_triangles` per mesh: `[face, p0, p1, p2]` in position indices; it drops ~13 k body triangles hidden under the clothes |
| maps | 38 PNGs | `Head_Normal` and `Head_SRMF` are 8192², ~256 MB of VRAM each uncompressed. SRMF = specular, roughness, metallic (0), unknown A. Wrinkle color/normal deltas (CM1-3, WM1-3) are exported but **the region masks are not**, so the 82 animated-map channels cannot be applied yet |

**DNA facts that cost time to establish:**
- DNA space is Y-up, +Z front, centimeters (`coord_sys` right/up/front): the same orientation as Godot, so only ×0.01.
- In quaternion mode (`rl4::Configuration::rotationType`), joint pose = translation `neutral + delta`, rotation
  `neutral * delta`, scale `neutral + delta`; `RigLogic::getJointVariableAttributeIndices(lod)` lists only the
  attributes that move at that LOD.
- Correctives (body RBF/twist/swing, the face's neck RBF) take the driver joint's rotation **relative to its neutral**
  as raw controls `joint.qx/qy/qz/qw` (every solver's `default` pose is the identity quaternion).
- The head DNA's root, `spine_04`, is stored in **world** space; its other 26 joints shared with the body match the
  body's local transforms. Merge by name and take shared joints from the body.
- `Geometry/*.json` `faces` are the DNA faces in position indices. Body UVs sit in UDIM tile 1002 (u ≈ 1.5) and wrap.
- Truncating to Godot's 8 influences loses on average 0.9% of a head vertex's weight (8,385 vertices are over 8),
  17.6% at worst; at 4 influences 11% on average — unusable.

**RigLogic is open source:** [OpenRigLogic](https://github.com/EpicGames/openriglogic) (MIT) is the DNA reader and
evaluator Unreal uses. Built with Emscripten 6.0.9 and timed in Node: head LOD 0 **86 µs** with SIMD (`RL_BUILD_WITH_SSE`
+ `-msse4.2`, which emulates SSE over SIMD128 — `wasm_simd` alone does not turn on its vector paths), 327 µs scalar,
58 µs native; body 19 µs. The rig evaluation is ~0.1 ms of a character's frame.

**Licensing is not settled:** June 2025 coverage of MetaHuman 5.6 says characters may ship in other engines under the
Unreal Engine EULA (free below $1 M revenue, a seat license above); a 2026 blog disputes it. Read Epic's EULA text before shipping.

**Plan** (effort at Opus 5, medium):

1. **Rig, meshes, materials, clothing** (1-2 weeks) — `modules/metahuman/`, `thirdparty/openriglogic/` (5.8 branch).
   - `.dna` scene importer: `head.dna` builds the whole character from its folder (body DNA, manifest, clothing,
     maps), `body.dna` the body alone. Without a manifest it still imports the DNA rigs and meshes.
   - One merged `Skeleton3D` with one shared `Skin`, so all LODs drive one render-server skeleton.
   - Authored LODs as visibility ranges; the rig follows them through `RigInstance::setLOD`.
   - 8-influence weights; hidden body triangles culled; the hidden slots (saliva, cartilage) dropped.
   - Custom skin, eye, teeth and cloth shaders.
   - Map import settings: size limit 2048 by default, VRAM compressed, normal maps flagged. They are applied through
     `reimport_append` with a generator marker, so a user's own settings are never overwritten.
   - Runtime: `MetaHumanRig`, a `SkeletonModifier3D`. Each frame it feeds driver rotations to the body correctives
     and face GUI controls to the face rig, then writes only the variable joints. Controls are properties, so
     animation tracks can key them.
   - The DNA is stored stripped of geometry, which is already baked into the meshes.
2. **Groom reader + baked hair** (~1 week) — a minimal Ogawa reader (the scratch probe is ~200 lines), Z-up→Y-up,
   strand decimation, and root binding to the head surface. Eyebrows, lashes and fuzz then skin like any mesh.
   Solid-shell bake: voxelize, mesh, simplify with meshoptimizer, bake a flow map for an anisotropic highlight.
3. **Hair cards** (~1 week) — cluster strands into cards and bake an atlas (albedo/alpha/depth/root-tip).
   Alpha to coverage is already wired in the driver.
4. **Dynamic strands** (1-2 weeks) — simulate only the guides (~20 k particles, using the manifest's physics
   parameters), and render decimated strands as camera-facing ribbons interpolated from the guides in the vertex
   shader. Either mode can also sway through generated chain bones and `SpringBoneSimulator3D`.

Gaps to close or decide: wrinkle masks (not exported); a refit of the >8-influence face weights against sampled rig
poses if truncation shows; ARKit / Live Link control mapping (not in DNA).

**Phase 1 — done 2026-10-20.** `modules/metahuman/` with OpenRigLogic vendored (`thirdparty/openriglogic/`, 5.8 branch,
one patch: `<type_traits>` for Emscripten's libc++).
- `.dna` scene importer (`editor/editor_scene_importer_metahuman.cpp`):
  - one `Skeleton3D` (1,185 bones for Ava) with one shared `Skin`;
  - every DNA LOD as visibility ranges (face 1.5/3/5/8/12/18/26 m, body 3/8/16 m, `metahuman/lods/distance_scale`);
  - top-8 renormalized weights; hidden-under-clothing triangles culled (body LOD 0 30,455 → 25,328 vertices);
  - clothing rebound from FBXDocument (the Unreal FBX skeleton is Z-up under a -90° X parent; `Root` vs DNA `root`);
  - shaders for skin, eye, wet shell, teeth (mouth darkening from teeth mask 002 R), blended procedural eyelashes and
    cloth (`editor/metahuman_shaders.h`);
  - map import settings applied once (VRAM compressed, 2048 limit, normal maps flagged and Y-inverted): the 8K head
    normal and SRMF become 5.6 MB each.
- `MetaHumanRig` (`SkeletonModifier3D`, `metahuman_rig.cpp`):
  - body correctives from driver rotations, face from `expressions/*` or `face_board/*` properties;
  - writes only the LOD's variable joints;
  - skips RigLogic when its inputs are unchanged;
  - LOD by camera distance from `head`/`pelvis`, where the meshes' visibility ranges measure from.
- `MetaHumanDNA` keeps the stripped DNA.
- Tests: `webgpu_tests/metahuman/run.sh <export.zip> [--shots] [--export]`, nine checks: structure, eye meshes on their
  joints (2.6 mm), jaw opens, an elbow bend drives the correctives.

Measured:
- **8-influence truncation**: rest pose exact; under a strong pose (jaw open + smile + blink at 1.0) mean 0.05 mm,
  max 2.7 mm, 110 of 24,049 head vertices over 1 mm. Not worth a refit yet.
- **WebGPU, baked** (1,030 shaders baked, 0 translated), Ava framed head-and-torso so every rig and mesh is at LOD 0,
  40 expressions and both arms animated, RTX 4080 SUPER, 1920x1080: **~1.1-1.4 ms/frame uncapped (700-880 fps)**.
  **GPU 0.20 ms/frame** (`gpu_time.mjs`), 32 draws, 11 passes, so the frame is main-thread CPU. Both rigs together
  add ~0.2 ms of process time (capped run, rig on 0.51 vs off 0.33 ms).
- **Download**: `index.pck` 73.6 MB (textures 2048, head scene 10.5 MB, all LODs) and `index.shaders.pck` 54 MB
  (advanced group baked for SSS).
- **Not measured: an M1 or a mid-range laptop.** By these numbers, one character has a large margin at 90 fps.

Engine fixes found on the way, all in §3:
- trap 44: `rendering_method.web` now follows the project's renderer;
- trap 45: the baker scans `.godot/imported`;
- trap 46: the export cache keys on the import's `.md5`;
- trap 47: the scene importer keeps a format importer's mesh-instance settings.

**Phases 2-4 — done 2026-10-20.**

Grooms:
- `editor/metahuman_groom.cpp` reads Unreal's groom Alembic (Ogawa only, one curves object). Godot = (x, z, y)/100
  of the file: the fit against the DNA's eyelash mesh decided it (0.46 mm against 0.59 mm mirrored).
- Every curve takes the skin weights of the head vertex nearest its root (4 kept). That binds the scalp to the
  `FACIAL_*_Hair*` joints, so the hairline moves with the brows.

Representations (`editor/metahuman_hair.cpp`, `editor/metahuman_shaders.h`):
- **Strands:** camera-facing ribbons at least 2 px wide, coverage-scaled.
  - Brows and lashes are blended; the groom lashes replace the DNA cards.
  - Scalp hair is opaque: 20% of the curves at 10 points, with root darkening and an optional hashed coverage for
    projects with TAA.
  - `ALPHA` clamps `v_coverage` before `pow()` (2026-10-26, §4.41): grooms that taper to a width of exactly 0 (Ava's
    eyelashes: 1574 tip vertices) interpolate coverage to about -1e-9 beside the tips, and `pow()` of that is NaN on
    NVIDIA and lavapipe. The opaque and hashed strand variants edit the shader through `HAIR_STRANDS_ALPHA_LINE`.
    Checked by `webgpu_tests/metahuman/nan_check.gd` (in `run.sh`).
- **Skin pores, 2026-10-26 (work task 25, merged `fd53d6d657`):** the tiling pore normal was tiled at a fixed 16 repeats
  per UV unit, so pores were ~2x too large on the face and ~7x on the body, natively too (WebGPU = Vulkan to 0.0008
  levels).
  - The map: `Head_DetailNormal` = `Body_DetailNormal`, a generic 1024² micro-skin tile. The export names Unreal's
    `Micro Skin Tiling` but carries no value. Ava's body UVs are 3.4x coarser than the face's (median 1.34 against
    0.395 m of skin per UV unit).
  - The fix: the importer scales the tiling from each skin mesh's median UV density so one repeat covers 1.2 cm of skin
    (`metahuman/materials/skin_detail_size`; the map's pore marks then land at ~900 per cm², and its detail below the
    ~1 mm the 8K normal resolves). Ava: head 32.9, body 111.7. The detail fades out over the LOD 0 range (head
    0.75–1.5 m), as Unreal turns it off from face LOD 1. Also fixed: the skin shader read the RG-compressed normals'
    blue (0) as z (§3 trap 72). Cheek at 0.25 m: pore features 9.6 → 5.5 px; arm at 0.5 m: detail rms 1.71 → 0.70.
  - Ruled out by measurement: the 2048 size limit (keeps 92% of the 8K normal's mm-scale slope energy), web filtering,
    mips, `SamplerLodBias`.
  - Open: the lips get pores (Unreal's `Micro MaskA-D` region masks are not exported); the cloth shader has the same
    z pattern; the user's look on size and strength (hand-offs page `look-skin-pores`). Tracked as §2.46, with the
    WebGPU after-capture that hung.
- **Shell:** voxelized (3 mm), closed, stray pieces dropped, smoothed, surface nets, simplified to 8,000 triangles,
  flow tangents smoothed over the mesh for the anisotropic highlight.
  - It cross-fades in beyond 3 m.
  - A shadows-only copy casts the hair's shadow, so the strands cast none: the shadow pass went from 0.165 to
    0.021 ms.

Phase 3 (hair cards) was superseded: decimated real strands at that budget look better than cards would, and need
no texture-baking pipeline.

Dynamics (`metahuman_hair_simulator.cpp`):
- The simulator steps the 1,280 guides at 16 points each (Verlet, segment lengths, a shape constraint from root to
  tip, head/neck/spine/shoulder spheres) at a fixed 120 Hz.
- Each frame it uploads the offset of every guide point from where skinning puts it (RGBA16F, 32 × guides). The
  strand and shell vertex shaders follow their two guides from the groom's own interpolation (`groom_sim_*`).
- Idle hair keeps exactly its groomed shape: offsets are measured from the settled rest pose (Unreal's "gravity
  preloading" problem).
- `verify.gd` checks idle, head-turn and settle.

Measured (RTX 4080 SUPER, Chrome, 1920x1080 page, Ava at LOD 0, face, arms and head animated, all 1,142 shaders baked):
- Frame: 1.52 ms uncapped. GPU: 0.43 ms with scalp strands, 0.23 ms with the shell alone.
- Pack: `index.pck` 95 MB, `index.shaders.pck` 62 MB.
- The offset upload still lands as 20 `copyBufferToTexture` calls per frame (0.066 ms). The driver's staging split
  was not chased.
- Not measured on an M1. Scaled by compute and bandwidth (~12x), the GPU half is about 5 ms.

New §3 entries: 49 (shader define order), 50 (a projection with negative [1][1]), 51 (camera-facing geometry in a
shadow pass), 52 (a step-size-dependent rest pose).

### 2.34 Ray and path tracing on WebGPU Forward+ `MEASURED 2026-10-23` — software-only by necessity; desktop passes the gate 3–4×, the phone is 100× slower: desktop-only feature, Tier A next

**Design document: `webgpu_site/RAY_TRACING_DESIGN.md`** — the full design (acceleration-structure layer, traversal include, shading at a hit without bindless, lights, frame integration, denoising, platform gating, the tiers with costs and acceptance tests) and **the full path tracer, designed and saved for later consideration** (its §7: wavefront scheduler, BSDFs, NEE, Movie Maker integration, projected cost). This entry keeps the research trail, the measurements and the status.

**The constraint that shapes everything: WebGPU has no hardware ray tracing, and none is coming soon.** The spec issue
([gpuweb #535](https://github.com/gpuweb/gpuweb/issues/535)) sits at "Milestone 4+" with no date; the Chrome lead's position
(Oct 2025) is that Dawn has nothing, that it cannot come through browser extensions, and that bindless resources are a
prerequisite that is itself years out. WGSL has no `rayQuery`, no acceleration-structure binding type, no RT pipeline
stages. So on this target "ray tracing" can only mean **compute-shader traversal of an acceleration structure we build
ourselves, in storage buffers** — the same thing Unity's TrueTrace, WebRTX and every WebGPU path-tracer demo do. That is
not only a limitation: the same code runs on Metal and D3D12 (where upstream has no RT implementation either) and on
GPUs without RT cores, so a software path is the only one that makes an RT feature available on every Godot backend.

**Upstream state (the 4.8 base of this branch).** `RenderingDevice` has a full hardware RT API since PR #99119 (merged
2026-01-27, fixes in #122051 / #123093 through Oct 2026): `blas_create` / `tlas_create` /
`acceleration_structure_instance_write`, `command_build_blas` / `command_build_tlas`, RT pipelines with raygen / miss /
closest-hit / any-hit / intersection stages, shader binding tables, `raytracing_list_*`, two feature bits
(`SUPPORTS_RAYTRACING_PIPELINE`, `SUPPORTS_RAY_QUERY`). **Only Vulkan implements it, and no Godot renderer uses it**: a
grep of `servers/`, `modules/` and `scene/` finds the API's only client to be `ShaderRD`'s stage plumbing. Open upstream
items: BLAS refit/compaction missing, the ray-query flag cannot be enabled at device creation, push constants arrive as
zeros in raygen on NVIDIA. Around it: NVIDIA's `nvidia-pt-dlss` fork (GDC 2026) replaces rasterization with full path
tracing plus DLSS Ray Reconstruction, NVIDIA Vulkan only; a student project did hybrid RT reflections through an RT
pipeline from a Forward+ depth/normal reconstruction; Calinou's assessment is that without bindless, RT shadows and AO are
feasible in Godot and reflections/GI are not yet. **This driver** returns `false` for both feature bits and every RT entry
point is an `ERR_FAIL` stub (`rendering_device_driver_webgpu.cpp:3676`), which `RenderingDevice` guards against anyway.

**What the engine already has that is software ray tracing**, i.e. prior art inside this tree:

| what | where | shape | on web? |
|---|---|---|---|
| lightmapper | `modules/lightmapper_rd/lm_compute.glsl` `trace_ray()` | uniform grid (`grid` texture → cluster list → clusters of `CLUSTER_SIZE` triangles with cluster AABBs), CPU-built in `_create_acceleration_structures()`; closest-hit and any-hit; hit albedo/emission from a UV2 atlas `texture2DArray` — its answer to "materials at the hit point without bindless" | **no**: editor-only module (`config.py`: `env.editor_build`), so not in the template, not in the precompile list, not in the corpus |
| SDFGI | `environment/gi.cpp`, `sdfgi_*.glsl` | ray-marching a signed distance field in cascades | yes, at parity (§4.29) |
| VoxelGI | `voxel_gi*.glsl` | cone tracing a voxel octree | yes, at parity |
| contact shadows | `_process_sscs` → `sscs_buffer` (set 1 binding 37), `min()`ed into the directional shadow term in `scene_forward_clustered.glsl:2655` | screen-space depth ray march | yes |

So GI by ray marching exists; what does not exist anywhere in the runtime is **triangle-accurate ray tracing**.

**What is feasible, in order of value per effort.** Every tier shares one acceleration-structure layer (below).

- **Tier A — RT shadows and RT AO (geometry only).** Needs no material at the hit, except alpha-tested surfaces
  (treat as opaque first; an alpha-mask atlas later). Integration is small: a compute pass in `_pre_opaque_render` after
  the depth prepass, which already produces depth and `normal_roughness` for SSAO/SSIL/SSR. **RT AO writes the SSAO
  texture** (`RB_SCOPE_SSAO`/`RB_FINAL`, sampled as `ao_buffer` set 1 binding 27) — zero change to the scene shader.
  **RT directional shadows write a screen-space mask** consumed exactly where `sscs_buffer` is today (one more
  `min()` into `shadow`, or a replacement for the shadow-map lookup when the mask covers the pixel); omni/spot RT shadows
  are the same mask with a layer per light, like `sscs_index`. Soft shadows: N rays toward the light's disc with a
  temporal blue-noise sequence and the TAA/temporal reprojection the renderer already has. Value: pixel-exact contact
  without PCF/bias artifacts, no shadow-atlas passes for the lights that use it (6 passes per omni light today, §2.31
  follow-up 1).
- **Tier B — RT reflections replacing SSR** (post-opaque, writes `RB_SCOPE_SSR`, sampled as `ssr_buffer` binding 35).
  Needs shading at the hit: the realistic answer is a hybrid — reproject the hit into the current frame's color where it is
  on screen (what SSR does), and otherwise a coarse surface cache (lightmap/UV2 atlas as the lightmapper does, or
  reflection-probe/SDFGI radiance at the hit) rather than evaluating materials. Scope: large; the cache is most of it.
- **Tier C — path-traced GI** (1 spp ReSTIR-GI style, denoised, as an alternative to SDFGI). Research-grade: the denoiser
  (SVGF or a temporal/spatial pair) is more work than the tracing. The probe decides whether even 1 spp fits.
- **Tier D — a progressive reference path tracer** (Movie Maker / stills, no frame budget). A *generic* Godot path tracer
  is a bindless problem — every material is its own shader, which a compute kernel cannot evaluate at a hit. The feasible
  subset is StandardMaterial3D-like materials with their textures in an atlas or in texture arrays within the 48-per-stage
  budget (§2.31). Honest scope: a reference mode for standard materials, not for arbitrary shader code.

**The acceleration-structure layer (shared):**

- **BLAS per mesh surface**, a binned-SAH BVH2 (BVH4 / compressed wide BVH later if the probe shows traversal-bound)
  built on the CPU: `MeshStorage` keeps the CPU `SurfaceData` (`mesh_get_surface()`), so positions are there without a
  readback (the readback would be §3 trap 54 all over again). Build cost: the JS probe builds 190 k triangles in 200 ms;
  C++ is several times faster and it can run on a worker in threaded builds; better still, **bake at export** next to the
  WGSL (`ShaderBakerExportPlugin` pattern, §4.23) and ship the trees in the pack.
- **TLAS per frame** over the culled `RenderGeometryInstance` list (transform, BLAS id, flags/mask, material id), CPU-built
  (thousands of instances, well under a millisecond) and uploaded as a storage buffer; two-level traversal in GLSL
  (instance → object-space ray → BLAS). MultiMesh/particles are instances; their transform buffers are already storage.
- **Skinned / blend-shaped meshes:** `skeleton.glsl` already writes skinned vertices to a storage-usage vertex buffer per
  frame; a compute refit (leaf AABBs from the skinned vertices, then bottom-up by level) keeps the BLAS valid. Rebuild
  small ones outright.
- **Where it lives:** a driver-agnostic `RendererRD` effect (next to `environment/gi.*` and `effects/ss_effects.*`) with a
  shared `rt_trace_inc.glsl`, so it also runs natively (and so the Forward+ parity harness can compare web against
  native — RT features have no native Vulkan *reference* otherwise; their truth is a many-spp brute-force render of the
  same fixture). **Rejected for now:** implementing upstream's acceleration-structure API inside the WebGPU driver and
  lowering GLSL `rayQueryEXT` to a traversal in a SPIR-V pass. It would let future upstream ray-query shaders run
  unchanged, but Tint has no ray-query ops, so it is a DXR-fallback-layer-sized project — and upstream has no such
  shaders yet. Revisit if they appear.
- **WebGPU-specific constraints, all survivable:** no bindless → one node buffer and one triangle buffer for the whole
  scene (Chrome exposes the adapter's `maxStorageBufferBindingSize`, which `engine.js` already requests; swiftshader
  reports 1 GiB, the default is 128 MiB); 10 storage buffers per stage on Chrome; no 64-bit atomics (no LBVH Morton
  sorts with 64-bit keys — use 32-bit or the CPU); subgroups are available (Chrome 134+) for compaction later; a
  `array<u32, 48>` function-scope traversal stack and the data-dependent loop compile and run through Tint (probe); the
  output textures must be storage-capable formats, which the SSAO path already handles for `R8_UNORM`.

**Budget arithmetic** (to fill in with the real-GPU probe): 1080p is 2.07 M pixels, so a kernel at *N* Mrays/s costs
2074 / *N* ms. A 60 fps frame has 16.7 ms and a hybrid effect can take 3–5 ms of it, so a full-resolution one-ray-per-pixel
effect needs **400–700 Mrays/s** of its ray type, a half-resolution one with temporal accumulation a quarter of that. Rough
expectation for a well-written BVH2 in WGSL on the 4080 SUPER: hundreds of Mrays/s to low Grays/s coherent, a few hundred
Mrays/s incoherent (software traversal is typically 4–8× behind the RT cores). Mobile (Razr 60, Adreno) will be one to two
orders of magnitude lower, which is why any RT feature is a per-project opt-in with a quality ladder, never a default.

**The probe — `webgpu_tests/raytracing_probe/` (`node run_probe.mjs`, README there).** Standalone page: deterministic
procedural scene (ground + tessellated spheres and boxes to a requested triangle count), binned-SAH BVH2 in JavaScript,
one WGSL compute shader with `primary` (coherent, closest hit), `shadow` (to a point light, any hit) and `bounce`
(cosine hemisphere from each hit, closest hit) entry points, each timed over 20 dispatches with GPU timestamp queries;
prints Mrays/s, hit fractions, traversal steps, BVH build time and depth, the adapter's features and the limits that bound
a software tracer, and draws the result so a broken traversal is visible. **It is also a correctness test**: a grid of 66
primary rays is re-traced on the CPU by brute force over every triangle in double precision and must agree with the GPU
traversal (it does, to 1.4e-5), the hit, occlusion and bounce-hit fractions must be the ones the fixed camera gives, and
the BVH depth must fit the 48-entry stack; the runner exits 0/1 on the checks and 2 when a real-GPU run landed on the
fallback adapter. It launches the system Chrome headed with the real-GPU flags (§3 "Measurement traps"); `--software`
runs swiftshader at 320×180, which proves the kernels and nothing about hardware; `--url-only --port 8099` serves it for
a browser you already have up or for the phone (`adb reverse tcp:8099 tcp:8099`, then the printed `http://localhost:8099/...`
URL in Chrome on the Razr 60; the result is on the page itself).

| run | adapter | triangles | BVH build | primary | shadow | bounce | 1 spp frame |
|---|---|---|---|---|---|---|---|
| 2026-10-23, `--software`, 320×180, 12/12 checks | swiftshader (fallback) | 18 k | 37 ms | 6.0 Mrays/s, 11.7 steps | 3.8 Mrays/s, 26 % occluded | 2.9 Mrays/s, 23 % hit | 31 ms |
| 2026-10-23, `--software`, 640×360, 66/66 rays = brute force | swiftshader | 190 k, depth 21 | 200 ms | 3.9 Mrays/s, 15.4 steps | 3.0 Mrays/s | 2.1 Mrays/s, 21 steps | 172 ms |
| **2026-10-23, real GPU, 1920×1080, 20 dispatches, 12/12 checks** | **RTX 4080 SUPER** (Chrome/Dawn/Vulkan; subgroups 32, `shader-f16`, 16 storage buffers/stage, 2 GiB binding) | 93 k, depth 20, 6 MB | 101 ms (JS) | **0.39 ms, 5.3 Grays/s**, 14.4 steps | **0.24 ms, 5.4 Grays/s** (1.27 M rays), 25 % occluded | **0.54 ms, 2.3 Grays/s**, 30 % hit, 19.4 steps | **1.17 ms** |
| same | same | 945 k, depth 23, 60 MB | 749 ms (JS) | **0.55 ms, 3.7 Grays/s**, 16.9 steps | **0.33 ms, 3.8 Grays/s** | **0.67 ms, 1.9 Grays/s**, 35 % hit, 23.8 steps | **1.55 ms** |
| **2026-10-23, Razr 60, 1280×720, 10 dispatches, 6/6 checks** | **Mali-G615 MC2** (Chrome 155, `arm / valhall`; subgroups 16, `shader-f16`, 16 storage buffers/stage), screen on, tab in front | 93 k, depth 20 | 177 ms (JS) | **20.7 ms, 45 Mrays/s**, 14.4 steps | **14.3 ms, 40 Mrays/s** (0.57 M rays), 25 % occluded | **29.6 ms, 19 Mrays/s**, 30 % hit | **64.6 ms** |

The software rows prove the kernels and the picture (correct spheres, boxes, blocked shadow rays, sky), nothing about
hardware. **The desktop rows are the decision**: GPU timestamps and wall clock agree to within the 50–100 µs submit
overhead, the traversal matched brute force on every sampled ray, and the GPU was idle (4 %) during the run. The gate below
asked for 300 Mrays/s incoherent for half-resolution Tier A and 600 for full resolution; the 4080 SUPER delivers **1.9–2.3
Grays/s incoherent** and **3.7–5.3 Grays/s coherent**, so one full-resolution primary + shadow + bounce sample per pixel
costs 1.2–1.6 ms of a 16.7 ms frame. **The phone is a different world: 19 Mrays/s incoherent, 40–45 coherent, a hundred
times less** — one 720p bounce ray per pixel is 30 ms on its own, and even quarter-resolution AO (230 k rays) would be
12 ms, so no RT effect fits a mobile frame on this class of GPU. The traversal was correct on both (every sampled ray
matched brute force), so the gap is hardware, not a translation problem. Read the desktop rows with three discounts: the scene is geometry only (no material fetch, no alpha
test, no TLAS instance transform — expect ~1.5× for a two-level traversal); it is cache-friendly (60 MB of BVH and triangles
sits in the AD103's 64 MB L2, a game scene with textures in flight will miss more — expect 2–3× in a real frame); and the
guess above ("a few hundred Mrays/s incoherent") was 5–10× too pessimistic, which says the WGSL→SPIR-V→NVIDIA path adds
no measurable penalty to a plain stack traversal. Even with every discount, **Tier A fits at full resolution on this class
of GPU with room for several rays per pixel, and Tier C's *tracing* at 1 spp fits (the denoiser is then the cost).**
The phone row settles the defaults: **RT is a desktop-class feature — a per-project opt-in that the engine turns off (or
falls back to SSAO/shadow maps) when the adapter is mobile-class**, which the adapter info the shell already reads
(`arm`/`qualcomm` vendors) or a first-frame throughput check can decide at startup.

**Next steps, in order:**
1. **Measure.** **Done 2026-10-23 on both.** Desktop: the gate (≥ 300 Mrays/s incoherent for half-resolution Tier A,
   ≥ 600 for full) is passed 3–4× over. Phone (`node run_probe_phone.mjs`, which drives the device's Chrome over adb +
   CDP): 19 Mrays/s incoherent, far under the ~100 that would have allowed a mobile mode — RT stays desktop-only.
2. **If viable, prototype Tier A engine-side**, behind a project setting (`rendering/raytracing/*`, default off): RT AO
   first (writes the SSAO texture, no scene-shader change, so the existing SSAO fixture in the Forward+ matrix exercises
   it), then directional RT shadows through the `sscs_buffer` path. BLAS at load from the CPU surface data, TLAS per frame,
   skinned meshes excluded until refit exists. Truth for tests: a many-spp render of the same fixture, not native Vulkan.
3. **BVH infrastructure proper**: export-time bake, GPU refit, BVH4 if traversal-bound.
4. Tiers B–D only on the numbers from step 1.

Effort: Tier A prototype 2–3 weeks, of which the acceleration-structure layer is about one; Tier B another 2–3; Tier C
open-ended; Tier D 2 weeks for the standard-material subset once the layer exists.

### 2.35 SPIRV-Cross's WGSL backend instead of Tint `DONE 2026-10-25` → §4.36 — swapped; Tint deleted

**Closed 2026-10-25.** The swap ran as tasks 00–19 on a work board (`webgpu_notes/work/`, retired with it) and ended
with Tint deleted; results, numbers and what each task changed are in §4.36, follow-ups in §2.38–2.44. The
evaluation below is kept as written, since it is what the decision was made on.


**What it is.** [Ludicon/spirv-cross-wgsl](https://github.com/Ludicon/spirv-cross-wgsl) (Ignacio Castaño) is
KhronosGroup/SPIRV-Cross plus one backend, `spirv_wgsl.{hpp,cpp}` (4,640 lines, `CompilerWGSL : CompilerGLSL`), on a
`wgsl` branch that merges upstream automatically. First commit 2026-10-02; one author; 165 reference shaders under
`shaders-wgsl/`, its output checked with Tint in its CI; Apache-2.0 OR MIT. It emits spec constants as `@id(N) override`,
splits combined image samplers (sampler at `binding + 16`, or Tint's layout with `--wgsl-resolve-binding-conflicts`),
turns push-constant blocks into a uniform buffer at a chosen group/binding, adds `diagnostic(off, derivative_uniformity)`
when implicit derivatives are used, and has a warning channel for lossy translations (non-finite constants, depth compare
at non-zero LOD, bias outside fragment, weak compare-exchange, ignored builtins such as `gl_PointSize`). It throws on what
WGSL cannot express: arrays of textures/samplers/buffers, 16-bit integers, geometry/tessellation, image atomics, etc.
**We already vendor SPIRV-Cross** (`thirdparty/spirv-cross`, for Metal), so adopting it is the WGSL files plus a version
bump, not a new dependency.

**Measured 2026-10-25 on the 276 variants `wgsl_precompile.py` builds** (81 shader files; `webgpu_tests/spirv_cross_eval/`,
`./run.sh <out> <spirv-cross>` reproduces all of it; Chrome on the 4080 SUPER, every optional feature enabled):

| | Tint (current) | spirv-cross, raw glslang SPIR-V | spirv-cross after `run_pipeline()` |
|---|---|---|---|
| converted | 276/276 | 195/276 — 79 "Arrays of textures and samplers are not supported", 1 subgroup op, 1 storage format | **276/276** |
| valid in Chrome | 276/276 | 107/195 — 86 duplicate bindings (combined-sampler split at the default offset) | **275/276** |
| bindings + kinds + override ids equal to Tint's | — | — | **276/276** |
| WGSL bytes, all variants | 6,287,157 | — | 6,686,228 (+6%) |
| biggest variant (Forward+ color uber, lightmap+motion+sepspec frag, 288 KB SPIR-V) | 0.43 s | — | 0.035 s |
| whole corpus, one process per module | 8.8 s | — | 1.7 s |

- **It replaces Tint, not our passes.** Raw glslang output fails on exactly what `spirv_preprocess` exists for
  (texture arrays → `expand_binding_arrays`); after the passes it accepts everything, and the interface comparison is
  what makes it a drop-in: the driver's bind-group layouts and pipeline constants come out the same.
- **The one invalid shader** is `effects/fsr2/fsr2_compute_luminance_pyramid_pass.glsl` (`no_image_atomics`, comp):
  "`workgroupBarrier` must only be called from uniform control flow", reached through `SpdDownsampleNextFour` from
  `SpdDownsample`. Barrier uniformity has no diagnostic switch; Tint's structurization of the same SPIR-V passes, so the
  difference is in how SPIRV-Cross emits that control flow.
- **The Tint times include running our passes a second time** on already-preprocessed input; `perf` is not permitted
  on this machine (`perf_event_paranoid=4`), so the Tint-only share was not separated. The ~10× is an upper bound.
  It matters less than it looks: baked exports translate nothing at runtime (`baked 747 / translated 0`, §4.23), and the
  cold export's whole Tint stage is 1.8 s of 7.8 s.
- **Size is estimated, not measured.** Tint is 11.5 MB of 202.5 MB of web-template objects (5.6%); natively its objects
  are 18.5 MB against ~4.2 MB for SPIRV-Cross core + GLSL + WGSL. The web wasm has no name section, so the saving in
  `godot.wasm` is unknown until a template is built without Tint. Tint is also 384 translation units in every one of the
  eight web variants `build-linux.sh` builds.
- **Validation is not parity.** Nothing above renders anything. Tint's WGSL is what `run_forward_plus.mjs --parity`
  (71/71, §4.29) has been judged on.

**Value, in order.** (1) A fallback when Tint aborts (`TINT_UNIMPLEMENTED` on `sewer-shift`, §4.27) — cheap, the library is
already here. (2) A second converter in `shader_corpus`: the interface comparison catches Tint and pass regressions.
(3) A full replacement: smaller wasm, shorter web builds, no Tint patches to carry — only behind Forward+ `--parity`
and a GPU-time comparison.

**Decision 2026-10-25 (user): do the full swap**, patching SPIRV-Cross locally as needed (never upstreamed), until
Forward+ is back at parity with Vulkan. The tree before it is tagged `pre-SPIR-V-Cross`.

### 2.36 Web export asks for performance, a screen that stays on, and its orientation `DONE 2026-10-25` → §4.37

**Closed 2026-10-25** (work task 19, merged `6d8a3e6dc0`): Wake Lock, `html/orientation`, fullscreen on the first tap,
high-performance on every adapter request. What was built and how it was checked: §4.37. The request and the survey
below are as written before the work.


**Ask (user, 2026-10-25):** the web export template should signal that it wants performance mode, that the screen should
stay awake, and that it prefers the orientation set in the export panel, adding a portrait/landscape export option if
there is none.

**What exists today (checked 2026-10-25):**
- *Performance:* the WebGPU adapter is already requested with `powerPreference: 'high-performance'`
  (`platform/web/js/engine/engine.js`, and the render thread's own adapter in `display_server_web.cpp`). Nothing else
  is signaled. The web has no general "performance mode" API: `powerPreference` is a hint the browser may ignore (it
  matters on dual-GPU laptops and some phones), and the Compute Pressure API only reports, it cannot request.
- *Screen awake:* nothing. `DisplayServerWeb::screen_set_keep_on()` is an empty override and `FEATURE_KEEP_SCREEN_ON`
  is commented out in `has_feature()`, so the project setting `display/window/energy_saving/keep_screen_on` (default
  true) does nothing on the web. The Screen Wake Lock API (`navigator.wakeLock.request('screen')`, secure context,
  Chrome/Edge/Safari 16.4+/Firefox 126+) does it; the lock is released whenever the page is hidden and must be
  requested again on `visibilitychange`.
- *Orientation:* the export option `progressive_web_app/orientation` (Any / Landscape / Portrait) exists, but it is
  written only into the PWA manifest, so it applies only to an installed PWA. `FEATURE_ORIENTATION` is commented out
  and `screen_set_orientation()` is not implemented, so `display/window/handheld/orientation` is ignored on the web.
  `screen.orientation.lock()` works on Chrome for Android only in fullscreen (or an installed PWA) and rejects
  elsewhere; desktop browsers reject it. Side bug in the same code: `CLAMP(orientation, 0, 3)` indexes a 3-entry array
  (and `display` is clamped to 4 for a 4-entry one), so an out-of-range preset value reads past the end.

**Plan:**
1. Keep-awake: implement `screen_set_keep_on()` with the Wake Lock API in `library_godot_display.js`, re-acquire on
   `visibilitychange`, apply the project setting at startup, report `FEATURE_KEEP_SCREEN_ON`. Fail quietly
   (`print_verbose`) where the API is missing.
2. Orientation: a non-PWA export option (e.g. `html/orientation`: Project Setting / Any / Landscape / Portrait,
   default follows `display/window/handheld/orientation`), passed in `GODOT_CONFIG`. The shell calls
   `screen.orientation.lock()` when it enters fullscreen (the only time Chrome for Android allows it) and, while the
   device is held the other way and the lock is unavailable, may show a "rotate your device" hint. Implement
   `screen_set_orientation()` / `FEATURE_ORIENTATION` on top of the same code. Keep the PWA option, or make it default
   to the new one, so there is one place to set it; fix the clamp bug.
3. Performance: keep `powerPreference: 'high-performance'`, make sure every adapter request uses it (the shells in
   `misc/dist/html/`, the render thread, the editor), and document that the web offers no stronger signal. Check on the
   Razr 60 whether Chrome's behavior differs (§2.9 recipe).

**Test:** the Razr 60 over adb (keep-awake: screen does not dim past the system timeout while running; orientation:
fullscreen locks to the export setting), desktop Chrome and Firefox (no errors where the APIs are missing or reject).

### 2.37 Record each render pass in wasm memory, replay it in one JS call `OPEN` — prioritized by the user 2026-10-25

**Why.** Every WebGPU call from the driver is a wasm→JS crossing into emdawnwebgpu's glue (one `getJsObject` table
lookup, then the browser method). Measured 2026-10-16 (§2.30, `cpu_steady.mjs` capped, heavy_i / 3D platformer): main
thread 1.99 / 1.31 ms per frame, of which engine wasm 60% / 54%, browser WebGPU methods 21% / 12%, **Emscripten glue
0.19 / 0.10 ms (10% / 8%)**. A faster glue table was tried and lost (§2.30 option 3), and replacing emdawnwebgpu with
another binding (juj's wasm_webgpu, evaluated 2026-10-25) keeps the same per-call shape while giving up the `webgpu.h`
API native Dawn shares, so the remaining lever at this layer is fewer crossings: record a render pass's commands
(`setPipeline`, `setBindGroup`, `setVertexBuffer`, `setIndexBuffer`, `draw*`, `setViewport`, `setScissorRect`, ...) into
a flat buffer in wasm memory and replay the whole pass from one JS function with direct references to the GPU objects.
Ceiling on desktop heavy_i: ~0.15 ms/frame (~7% of main-thread CPU); on phones each crossing costs several times more
(the May 2026 IPC arc measured crossings at 20–45% of mobile frame time before the per-call optimizations), so the Razr
60 is where it should pay most and where it must be measured.

**Prior design.** `webgpu_notes/COMMAND_STREAM_RECORDER_PERF_OPTIM.md` (read it with
`git show bb79e0122f^:webgpu_notes/COMMAND_STREAM_RECORDER_PERF_OPTIM.md`), Approach 2 "Godot-level JS batch replay",
designed 2026-05-04 and not built because the per-call work of §4.21 closed the gap first. The same doc weighs render
bundles, indirect draws and custom bindings; the decision then was command buffering only.

**Plan.**
1. Measure first on the current tree: `cpu_steady.mjs` on heavy_i and the platformer (desktop, capped) and the Razr 60
   (§2.9 recipe), glue and WebGPU-method self time per frame, so the result has a baseline.
2. Recorder in `rendering_device_driver_webgpu.cpp`: render-pass commands append fixed-size records to a per-pass wasm
   buffer; `command_end_render_pass()` (and any command that must not be reordered against it) flushes with one
   `EM_JS` call. The JS side resolves handles once per object through emdawnwebgpu's table (or a parallel array the
   driver fills at object creation), and calls the `GPURenderPassEncoder` methods in a tight loop. Compute passes
   second, only if the measurement says they matter.
3. Keep the direct path behind a switch for A/B and as a fallback; the recorder must be bit-identical in output
   (Forward+ `--parity`, the scene smoketest and `perf_guard` budgets unchanged) and no slower anywhere.
4. The render thread (§2.5) has its own device and encoders: the replay runs on whichever thread owns the pass.

**Done when** the Razr 60 and desktop numbers show the main-thread saving, parity is unchanged, and `perf_guard` passes.
Effort: ~2–4 sessions.

### 2.38 Converter smoke check and stored references out of the work board `OPEN` *(from §4.36)*

The one check that the converter still produces what the driver expects without rendering anything is
`webgpu_notes/work/smoke.sh`: it builds `wgsl_convert_cli`, converts every engine variant `wgsl_precompile.py` builds,
compares bindings, kinds and override ids with `webgpu_tests/spirv_cross_eval/interface_reference.json`, runs
`webgpu_tests/wgsl_text_scan/run_tests.sh` against `scan_reference.txt`, and compiles every module in Chrome. It lives in
the swap's work board, which is being retired, and `wgsl_text_scan/run_tests.sh` takes a smoke run directory as input,
so the text-scan tier has no producer outside the board. Nothing in `local_ci.sh` or CI runs either.

**Do:** move `smoke.sh` and `smoke_expected_invalid.txt` to `webgpu_tests/spirv_cross_eval/` (or a `converter_smoke/`
tier), drop its board-only parts (leases), point `compare_interface.py`'s and `run_tests.sh`'s comments and the
CLAUDE.md Testing block at it, and add it to `local_ci.sh --quick` (no GPU: Chrome compiles WGSL on any adapter). Also
watch the first GitHub run after the swap: work/9 changed matrix keys (`wgsl-cli`), the artifact name
(`wgsl-convert-cli`) and the cache keys of every workflow without a CI run (§4.36). Effort ~0.5 session.

### 2.39 The baked Forward+ fixture still converts ~97 stages at load `OPEN` — pre-existing, every converter *(from §4.36)*

`run_forward_plus.mjs --baked` ends in RESULT FAIL on its bake gate (`translated <= 80`) with every converter, Tint
before the swap included: 8 Forward+ scene-shader groups (×12 stages) and one particles stage are converted at runtime,
because the baker never sees those versions. Parity itself is 87/87 (71/71 when this was written), so this is load time, not correctness, but the
gate being red means the tier cannot catch a real baking regression. Start from the fixture's `--verbose` "Shader cache
miss for ..." lines against the export's "Shader baker: baking ..." fingerprints (§3 traps 40, 45, 49 are the three
earlier causes of the same symptom). Effort ~1 session, needs the GPU.

**2026-10-26:** the matrix grew to 82 arms (work/21), whose new materials and shaders are built in GDScript at run
time and so can never be baked; translated rose to ~117 and the gate's ceiling was raised to 130 to keep it green. That
makes the gate looser, not this item fixed: the ~97 engine stages above are still inside the 117. Fixing this item
should bring the ceiling back down to the GDScript-built count plus a margin.

*2026-10-26:* the bake gate's ceiling is 150 (`run_forward_plus.mjs` `TRANSLATED_CEILING`): 117 with the work/21 arms, 133 once work task 26's `dof_nan` arm added a GDScript-built NaN material, which the bake cannot see. A broken bake translates ~440, so the gate still catches it.

### 2.40 SDFGI differs from native in Firefox only `OPEN` *(from §4.36)*

In Firefox desktop (Linux, wgpu on Vulkan) `sdfgi` and `sdfgi_camera_move` are 0.277 from native Vulkan (tolerance
0.08), with Tint-converted and SPIRV-Cross-converted exports alike; Chrome passes the same exports. The difference is
subtle by eye. Frames: `webgpu_notes/work/results/11-firefox/sdfgi_frames/` (in git history once the board is gone).
Some SDFGI pass behaves differently on Firefox/wgpu — bisect by swapping single modules' WGSL
(`gpu_time.mjs --replace-wgsl`, if it runs under `--browser firefox`) or by disabling SDFGI's passes one at a time, and compare with naga's
own translation of the module. Effort ~1 session, needs the GPU and Firefox (`scene_smoketest/firefox_prefs.mjs`).

### 2.41 Subsurface scattering on the Razr 60 is 11.1 from desktop Vulkan `OPEN` — pre-existing *(from §4.36, §2.9)*

fp_check's `subsurface_scattering_1` on the phone (Mali-G615, Chrome) is 11.1 mean abs diff from desktop Vulkan with
both Tint and SPIRV-Cross, while desktop WebGPU passes; every other look matches. Not the swap. Compare with an Android
Vulkan render if one can be made, then bisect on device by swapping WGSL modules (`phone_bench.mjs --swap-wgsl`, the
method that found patch 0019's zero-trip loop, §3 trap 58). Effort ~1 session, needs the phone lease.

### 2.42 FSR2 accumulate and scene set 1 binding 24 get the blank depth fallback `DONE 2026-10-28` → §4.74 *(from §4.36)*

With `--verbose`, every frame logs `'Fsr2AccumulatePassShaderRD:1' set 1 binding 5: a 320x180 depth texture is bound
where the shader declares a float texture`: FSR2's accumulate pass reads `r_input_depth` as a float texture, so the
driver substitutes the blank fallback (§3 trap 29). It is read only to derive motion vectors where there are none
(`GODOT_DERIVE_INVALID_MOTION_VECTORS`, i.e. the sky), and a fallback reading 0 is the sky's reversed-Z depth, so the
fixture shows nothing. `SceneForwardClusteredShaderRD` set 1 binding 24 logs the same (a 4x4 depth texture; it is §2.31's
"trace the 4x4 depth placeholder message"). Check FSR2 with the camera moving and the sky in view; if it differs, make
the binding a depth texture by name (`*_depth*`) or teach the reclassifier its use. Effort ~0.5 session, needs the GPU.

### 2.43 Audit swizzled texture views `DONE 2026-10-28` → §4.73 *(from §4.36, §3 trap 55)*

WebGPU texture views have no component swizzle, and `RenderingDeviceDriverWebGPU` stores a view's swizzle without
applying it, with no warning. FSR2's reactive mask was such a view and read the wrong channel for months (amd-fsr2 patch
0005). Find every other non-identity swizzle that reaches the driver: `texture_storage.cpp` builds views from
`ret_format.swizzle_*` for image formats such as L8/LA8 (some are already converted to RGBA up front, §3 trap 17), and
`RenderSceneBuffersRD`'s `alpha_only_view` exists for MetalFX. For each, either convert the data, do the swizzle in the
shader, or have the driver warn once when it drops one. Effort ~0.5–1 session.

### 2.44 `run_spec_ops.sh`'s `op_FConvert_f32` probe is invalid SPIR-V `DONE 2026-10-28` → §4.72 *(from §4.36)*

`webgpu_tests/spirv_cross_eval/spec_op_probes.py` builds `op_FConvert_f32` as an FConvert to the same width, which the
validator rejects ("Expected component type of Value to be different"). Since `run_pipeline()` validates its input
(§4.36), the probe's legacy (frozen) path is rejected, so `run_spec_ops.sh` should report it as a new failure. Run it
once (needs the GPU), then drop the probe or list it as expected. Effort <0.5 session.

### 2.45 Occlusion culling on the web: the raycast module (Embree) in web templates `DONE 2026-10-26` → §4.38

**Closed 2026-10-26** (work task 22, merged `b13c7bee24`; default decided in `508f4ed3b2`). Moved whole to §4.38.

### 2.46 MetaHuman skin follow-ups after the pore fix `OPEN` *(from §2.33, work task 25)*

Task 25 (merged `fd53d6d657`) fixed the pore scale (§2.33's 2026-10-26 bullet). Left open:

- **The cloth shader reads blue as z** (§3 trap 72): `modules/metahuman/editor/metahuman_shaders.h`'s cloth fragment
  takes `texture(normal_map, UV).rgb * 2.0 - 1.0` and the same for `micro_normal_map`, so z is -1 on RG-imported
  normal maps, as the skin shader's was. Rebuild z from xy as the skin shader now does. Natively visible too; check
  with `webgpu_tests/metahuman/run.sh --shots` after a fresh import (§3 trap 71).
- **The lips get pores:** Unreal masks the micro detail by region (`Micro MaskA`-`D`), which the export does not carry.
  The SRMF alpha (black on lips and eyelids) looks like a fuzz mask and was deliberately not used. Needs a mask source.
- **The WebGPU "after" capture never ran:** `webgpu_notes/work/results/25-metahuman-skin-coarse/capture_web.mjs` on
  worker 25's `web_after` export hung before its first SHOT twice, even with 10 minutes. The "before" pair matched
  Vulkan (WebGPU = Vulkan to 0.0008 levels) and the fix touches neither the renderer nor the converter, so this is a
  missing confirmation, not a known difference. Find why the capture hangs (headed system Chrome, real adapter).
- **The user's look** on size and strength: hand-offs page `look-skin-pores` (frames in the task's `looks/`). The
  user's camera-sim needs a **Reimport** of `scenes/Ava/head.dna` to get the fix.

Effort ~0.5 session for the cloth fix plus a GPU run; the lip masks need data the export lacks.

### 2.47 Editor update check: the blind spot and two pending user questions `OPEN` *(from §4.45, work task 30)*

The check against this fork's releases is done (§4.45, merged `4b4bd00e62`). Open:

- **Two user decisions** on the hand-offs page, each implemented with its default: `update-newer-rule` (newer = the
  release's `main-<sha>` commit is not the build's and was committed later; the alternative is a fork version number in
  `version.py`, the tag and the title) and `update-repo` (default `Shane-Gadsby/godotwebgpu`). A different answer is a
  small change in `editor/project_manager/engine_update_label.cpp` (and, for a version number, the release workflow).
- **The blind spot of the commit-date rule:** a build from a branch committed after a release but not containing it
  (`webgpu-4.8` before a fix that is only on `main`) is not told about that release.
- **Coupling to the release workflow:** the check reads the commit from the release's `main-<sha>` tag and the engine
  version from the `godot-webgpu-*-<version>` asset names; `.github/workflows/runner.yml`'s `draft-release` job must
  keep both.

Effort <0.5 session once the user has answered.

### 2.48 Survey: post-processing effects Godot lacks `SURVEYED 2026-10-28` *(user request; work task 41)*

**Done (work task 41):** `webgpu_notes/work/results/41-postprocess-survey/SURVEY.md` ranks nine gaps with evidence. Recommended to build: color grading on Environment (baked into one internal 3D LUT; all renderers, no new binding), screen-space light shafts, Panini projection (in the lens resample), color vision deficiency simulation/correction, then local exposure (Forward+ and web). Later: custom tonemap curve, GTAO (with §2.34's RT AO), web HDR output, micro shadows. Not gaps: contact shadows (upstream, Forward+), sharpening (§4.50), lens dirt (`glow_map`), desktop HDR output. The user's picks are the `pp-*` questions on the hand-offs page; each chosen effect becomes its own task. **Built so far:** micro shadows §4.59 (task 49), light shafts §4.64 (task 46), color grading §4.65 (task 45), Panini §4.66 (task 47), GTAO §4.68 (task 48), custom tonemap §4.70 (task 56), local exposure §4.71 (task 57). **All `pp-*` "Build it now" picks are built**; color vision (§2.48 entry 8) and web HDR output (entry 7) are "Later". custom tonemap curve and local exposure next.

Investigate which post-processing effects Godot's renderers (Forward+, Mobile, Compatibility; web included) do not
have, the way the camera survey of 2026-10-28 found the camera attributes that became work tasks 33–38. **Exclude what
that batch covers** (lens optics, motion blur and rolling shutter, bokeh/blades/tilt-shift/starburst, halation, lens
flare, veiling glare, diffusion filters, metering, auto white balance, in-camera NR and sharpening, monitoring overlays,
grain §4.47, camera LUTs §4.48).

- **Method:** compare Godot's post stack (Environment: tonemap, glow, adjustments + color correction, SSR, SSAO, SSIL,
  SDFGI, fog/volumetric fog; Viewport: TAA, FSR, SMAA, FXAA, debanding; Compositor effects) with Unreal's post-process
  volume, Unity HDRP/URP's volume overrides and Blender's compositor/Eevee, and check this fork's tree for each gap
  before listing it (grep, as the camera survey did).
- **Candidates to check, not conclusions:** grading controls beyond BCS (lift/gamma/gain, shadows-midtones-highlights,
  channel mixer, curves, split toning, hue-vs curves); local exposure / local tonemapping; screen-space contact shadows;
  screen-space light shafts (god rays); panini projection; outlines / edge detection; screen-space distortion (heat haze,
  underwater); exposure fusion / HDR display grading controls; GTAO/HBAO-quality AO; sharpening outside FSR.
- **Output:** a ranked list (realism or usefulness, effort, per-renderer feasibility, WebGPU cost: passes, bindings,
  bandwidth, whether the Forward+ tonemap push constant or task 33's CameraEffects UBO is involved), with where each
  belongs (Environment, CameraAttributes, Compositor, project settings), for the user to choose from. Put choices on the
  hand-offs page.

Effort ~0.5 session for the survey; each chosen effect becomes its own task.

### 2.49 Web loading screen: progress bar colors `DONE 2026-10-28` → §4.56 *(user request; work task 42)*

When the loading bar is shown, let users pick its **border**, **background** and **fill** colors in the Web export
options, next to the existing **Loading Screen** group (`loading_screen/<stage>/text`, `show_text`, `show_spinner`;
`web_loading_screen_stages[]` in `platform/web/export/export_plugin.cpp`, wiki "Web Export Options → Loading screen").

- The bar is the `<progress id="status-progress">` element in the default shells (`misc/dist/html/full-size.html`,
  `webgpu-full-size.html`), styled in their CSS. Add three Color export options (e.g. `loading_screen/progress_bar/
  border_color`, `background_color`, `fill_color`), defaulting to today's look so existing exports are unchanged, and
  apply them in both shells. `<progress>` needs vendor pseudo-elements for its parts (`::-webkit-progress-bar`,
  `::-webkit-progress-value`, `::-moz-progress-bar`) or a styled div; check Chrome, Firefox and Safari.
- Pass them to custom shells too: add them to the `$GODOT_LOADING_SCREEN` JSON (or a sibling placeholder), and keep
  custom shells that don't use them working.
- **Add a toggle** (user, 2026-10-28): a `loading_screen/progress_bar/enabled` export option in the Web preset
  (default on, today's behavior) that shows or hides the bar in the default shells and is passed to custom shells; the
  three color options are visible in the export dialog only while it is ticked (`get_export_option_visibility()`).
- Docs: the class/export docs, and the wiki's Web Export Options page (the wiki can be pushed).

Effort <0.5 session.


### 2.50 Build scripts: the no-threads, no-dlink web templates first `DONE 2026-10-28` → §4.60 *(user request; work task 50)*

The user exports with the **no-threads, no-dlink** web templates (debug and release) far more than any other, and wants
to start exporting as soon as those two exist instead of waiting for the whole set. Today `build-linux.sh` builds the
editor, then the native templates, then the 8-variant web matrix with `dlink` and `threads` outer loops starting at
`yes`, so `threads=no dlink_enabled=no` comes **last**; `build.sh`, `build-macos.sh` and `build-windows.ps1` only build the
`dlink_enabled=yes threads=no` pair and not the non-dlink one at all.

- Every `build-*` helper: right after the editor (and the converter/baker prerequisites the templates need), build
  `template_debug` and `template_release` with `threads=no dlink_enabled=no` **and install them immediately** into the
  places the rest of the run installs templates (`builds/<version>/`, the editor's export templates folder), so the user
  can export while the rest builds; then the remaining variants and native templates as today. Print a clear line when
  the pair is installed ("ready to export").
- Keep the editor/template hash match (§3 trap on committing mid-build): same tree, same run, nothing changes.
- The narrowed-matrix flags (`--web-targets/--web-threads/--web-dlink`) keep working; the pair is just ordered first.
- Check the per-step timing table and the final summary still read correctly. `build.sh`/`build-macos.sh`/
  `build-windows.ps1`: add the non-dlink no-threads pair (they lack it) and put it first.

Effort <0.5 session (scripts only; one full `build-linux.sh` run to confirm the order and the early install).

### 2.51 Web export: a Start button and "preload before run" `DONE 2026-10-28` → §4.61, §4.63 *(user request, 2026-10-28)*

The user expected these in the Web export options: a camera-sim session wrote full specs for them on 2026-10-28 that never
reached this board; they are now `webgpu_notes/work/tasks/51-web-start-button.md` (with the user's extra styling) and
`53-web-preload-before-start.md` (a proper `Engine.prototype.prefetch()` instead of camera-sim's `window.fetch` wrapper),
originals in `webgpu_notes/work/inputs/camera-sim-2026-10-28/`. **Those task files supersede the outline below.**

- **Preload before run** (`loading_screen/preload_before_run` or similar, default off): the page downloads everything
  the game needs (wasm, pck, the shader packs `<name>.shaders.pck` / `.shaders_extra.bin`, the texture pack the device
  samples, §2.18/§2.27) and does as much start-up as the engine allows before the user starts it, so starting is instant.
- **Start button** (default off, or on whenever preload is on; decide and document): a button on the loading screen that
  starts the game (and gives the page the user gesture audio/fullscreen need). Options: text, and its look: background
  color, border color, border width, border radius, font family, font size, font color, padding; `cursor: pointer` by
  default; slightly darker on hover and slightly darker again while pressed (derived from the background color, with
  overrides). Shown only while enabled (`get_export_option_visibility()`), passed to custom shells through a placeholder
  like `$GODOT_PROGRESS_BAR` (§4.56), applied in both default shells.
- Defaults leave existing exports unchanged; accessibility (keyboard focus ring, Enter/Space, aria label); Chrome and
  Firefox checked with the launch rule (§3 trap 79); wiki Web Export Options updated.

Effort ~0.5-1 session.
---

## 3. Traps and lessons — read before debugging

- **Profiling a phone over USB (2026-10-17):** `adb reverse` + a localhost server gives WebGPU a secure
  context without HTTPS; `adb forward` to `localabstract:chrome_devtools_remote` dies with every
  `am force-stop` and must be re-bound; Chrome restores old tabs on relaunch, so close every other page
  target first or two engine instances share the GPU (this produced a bogus first number); a foldable's
  `screencap -p` emits a text warning instead of a PNG unless `-d <display id>` is given; and an export
  rendering on the separate render thread shows **zero** WebGPU calls on the page with ~12% main-thread
  busy — that is not "idle", it is the worker. See §2.9 and `webgpu_tests/perf_guard/android/README.md`.

Each of these cost real time at least once. They are the reason this file exists.

Numbers here are **not** stable the way §2's are — these are grouped by theme, so a new trap gets
inserted into its group and the rest shift. If you insert one, re-check the references: `git grep -n
"trap [0-9]"` across this file and `CLAUDE.md`.

**Measurement and environment**

1. **A software adapter is not this fork's renderer.** Under headless swiftshader every pixel-based
   assertion fails identically whatever the engine drew, because nothing presents to a capturable
   canvas: `test_font_visual.mjs` reports "glyph modulate is being dropped" — specific, plausible and
   wrong — while a scene that *passes* the smoketest screenshots as 100% `rgb(255,255,255)`.
   **Screenshot a known-passing scene before believing any pixel result.**
2. **CI builds with `dev_mode=yes` (`warnings=extra werror=yes`); no local command above does.** A
   warning that is fatal in CI is invisible locally — one unused variable kept CI red for two days
   while every local tier was green. `./webgpu_tests/local_ci.sh --dev-mode` before assuming agreement.
3. **Another process can be holding the VRAM, and the failure looks exactly like a regression.**
   The real-GPU tiers need a few GB for SDFGI/VoxelGI 3D textures. On 2026-10-14 LM Studio held
   **13.1 GB of this machine's 16 GB**, and the Forward+ matrix failed *every* feature with hundreds
   of errors cascading from one `GPUOutOfMemoryError: vkAllocateMemory failed with
   VK_ERROR_OUT_OF_DEVICE_MEMORY` on a 768×768×64 texture. It reads as a driver regression in
   whatever was last changed; it is not. Run
   `nvidia-smi --query-compute-apps=pid,used_memory --format=csv` before believing a sudden wall of
   validation errors. Fall back to `CI=1` (swiftshader) only for adapter-independent work — trap 1
   says why that is not a substitute.

   **Corollary (2026-10-14):** `local_ci.sh` on this machine needs `WEBGPU_REAL_GPU=1`, or the Chrome font
   test fails exactly as trap 1 says. And `test_font_visual.mjs` used to screenshot the instant the scene
   printed READY, which Firefox had not presented yet (black canvas, same false message); it now retries for
   10 s. `WEBGPU_REAL_GPU=1 ./webgpu_tests/local_ci.sh --dev-mode` plus the stages after the Firefox font test
   (fog, matrix, WebRTC, lifecycle, screenshots) were all green on `4d407aacc4`.

3a. **A baked re-export needs the editor rebuilt along with the template.** The version hash is
   per build: change the driver, rebuild only the web template, export, and the run reports
   `baked: 0, translated: 355` with no error — correct output, completely different code path. Check
   `godotWebGPUShaderStats.baked` before reading any baked timing. Likewise `local_ci.sh`'s
   `ensure_playwright` will `npm install` the *latest* Playwright into a directory lacking `node_modules`
   (it did so in `webrtc_parity`, 1.64 vs the 1.63 browsers installed), which makes every browser in that
   tier SKIP; delete the new gitignored `node_modules` rather than trusting the SKIP.

4. **An identical number across genuinely different builds indicts the measurement, not the fix.**
   `360/16` repeated *exactly* through three unrelated fixes because a stale client-side cache sat
   between artifact and observation. "Identical, not merely similar" is the tell.
5. **Build, then commit, then rebuild.** A binary built before its commit carries the *previous*
   `GODOT_VERSION_HASH`, which is how an editor/template mismatch arises — invisible until the
   version banners are compared side by side.
6. **One sample of a nondeterministic quantity is how a finding gets written down wrong.** The WebRTC
   phase did this once; every congestion number afterwards was resampled across 3-4 runs on purpose.
7. **Before believing a recorded "this never works", re-run it.** `demo_compute_heightmap`'s
   "readback never completes" was false; the committed export under `capture_errors.mjs --all` showed
   a different line entirely. Thirty seconds, no rebuild.

**Shaders and the conversion pipeline**

8. **Trace against the *preprocessed* SPIR-V, not glslang's output.** They are different modules;
   designing against the wrong one cost a full implementation. `WGSL_DEBUG_DUMP_PREPROCESSED` on `wgsl_convert_cli`.
9. **WGSL captured from the engine is not the converter's output** — it has already been through the driver's
   own text passes. `wgsl_convert_cli` on the same SPIR-V is (it was Tint's when this was found). Diffing the two is a two-minute check
   and it is what finally attributed the depth-promotion bug correctly.
10. **`wgsl_convert_cli --batch` hides its own crash diagnostics** — it forks per shader and redirects
   the child's output to `/dev/null` so one converter abort cannot kill the batch. Run it single-file,
   un-forked, to see the real abort.
11. **When a new shader fails conversion**, the fix is almost always a new/extended SPIR-V
    preprocessing pass or a SPIRV-Cross patch (`thirdparty/spirv-cross-wgsl/patches/`, never upstreamed) — not a
    WGSL text rewrite. It was a Tint patch before §4.36.

**Merges and upstream**

12. **On a large merge, a clean conflict list proves nothing.** Five breaks in fork-touched shared
    code produced *no conflict at all*, because the fork's lines and 4.8's change sat in different
    places in the same file: a removed helper (`get_compressed_image_format_pixel_rshift`), a moved
    header, a deleted static member, a dropped function parameter, a duplicated local declaration.
    Each was found by a build or a grep, never by git. **After the next sync, grep for every symbol
    upstream deleted.**

**Reading code vs measuring it**

13. **`library_godot_webrtc.js`'s JS fallback values are unreachable dead code**, because `get_prop`
    falls back only on `undefined` while browsers report unset channel properties as `null`, which
    crosses into C++ as `0`. Reading that file predicts behavior badly; measure instead. Three WebRTC
    findings were written down wrong from exactly this.
14. **A partial fingerprint is worse than none.** A summary of *some* fields matched byte-for-byte on
    both sides while the hashes differed, which reads as proof of sameness. When identity is the
    question, decompose every field the hash covers, or print the thing itself.
15. **When the producer of a thing cannot be enumerated reliably, enumerate the things.** Five fixes
    targeted *where materials come from*; four changed nothing. Baking every version the engine had
    already built closed it immediately.

**Verification that looks sufficient and is not**

16. **"Text appears" cannot catch a colour bug** — white text is indistinguishable from text whose
    modulate was dropped. Use a non-white fill or a contrasting outline. A change shipped on exactly
    this evidence and made all text white.
17. **A log line that misdescribes a deliberate, unconditional conversion as a hardware shortfall
    will cost someone an investigation** — it did twice, for `LumAlpha8` and then `RGB8`, from the
    same function. If the driver always does something, the message must say why.
18. **Compile-check the disabled configuration.** `text_server_fb` is off by default here, so a change
    to it ships uncompiled without `module_text_server_fb_enabled=yes`.
19. **Emscripten-only driver files can be compile-checked in ~2 s** by naming the object file as the
    scons target — the name must carry the full variant suffix (e.g.
    `….wasm32.nothreads.o`). No full web build needed.
20. **`wgsl_precompiled.gen.h` used to depend only on `wgsl_precompile.py`** (fixed 2026-10-16). A change to a
    preprocessing pass or an engine `.glsl` rebuilt the converter tool and the runtime path, but the precompiled engine
    shaders kept the old WGSL — the header was a day old after a pass change, with nothing saying so. `drivers/webgpu/SCsub`
    now lists the pass sources and `servers/rendering/renderer_rd/shaders/**/*.glsl`, and since 2026-10-17 (§4.22) a
    fingerprint `Value` over every vendored converter/SPIRV-Tools/CLI source (today SPIRV-Cross and `wgsl_cli`),
    `bin/wgsl_convert_cli` itself and glslang's version, so a converter patch re-bakes on its own (since 2026-10-25
    the FSR 1/2 shader sources under `thirdparty/amd-fsr{,2}` too: an amd-fsr2 patch alone left the table stale); `./build-linux.sh --force-shader-rebake` deletes the header
    when you want to be certain. Check with `ls -la drivers/webgpu/wgsl_precompiled.gen.h` and the
    `[WGSL Precompile] Results:` line in the build log.
21. **The browser main thread cannot wait, and a worker must not re-schedule itself with a timer** (§2.5, 2026-10-16). A
    blocking wait on the web main thread (`CommandQueueMT::sync()`, any `push_and_sync`) is an Emscripten busy-spin that
    counts as main-thread time — the first render-thread build was *slower* (47% busy, 1.8 ms/frame in
    `_emscripten_get_now`). Wait with `Atomics.waitAsync` and return to the event loop instead. And on the worker,
    `emscripten_async_call(f, 0)` is `setTimeout(0)`, which Chrome clamps to 4 ms once timers nest five deep — the render
    thread's wake latency rose to 4 ms per frame; a `MessageChannel` message yields without the clamp. Diagnose a "hang" with
    `Debugger.pause` over raw CDP on every target (page + workers): a page stuck for seconds was the main thread spinning
    in `sync()` while the render thread translated shaders, not a deadlock — the debugger stack showed it.
22. **Every test server in this repo sends COOP/COEP, so a non-isolated page is never tested** (2026-10-16). The scene
    smoketest, the perf tools and the scratch profilers all serve with `Cross-Origin-Opener-Policy`/`Cross-Origin-Embedder-Policy`
    set, so `SharedArrayBuffer` always exists. A plain `python3 -m http.server` does not send them, which is how a `threads=no`
    export is usually hosted, and there `SharedArrayBuffer` is **undefined**, not just unavailable. PR #123996 (cherry-picked
    `16a69c7728`) did `HEAPF32.buffer instanceof SharedArrayBuffer` in `godot_audio_sample_register_stream`, which throws a
    `ReferenceError` on the first sample stream, the engine never leaves "Rendering first frame..." and the main thread sits
    idle — 21/21 smoketest scenes passed around it. Guarded with `typeof SharedArrayBuffer !== 'undefined'`; when testing a
    `threads=no` export, serve it at least once without the headers.
23. **A WebGPU-only workaround written unconditionally into shared RD code breaks every other driver** (2026-10-16).
    `blit.glsl` ended in a bare `color.a = 1.0;`, added by `f1f47af795` because Chrome composites a canvas with alpha < 1 as
    transparent even under `CompositeAlphaMode_Opaque`. That shader has four pipeline variants and only
    `BLIT_MODE_NORMAL_ALPHA` blends, so forcing alpha to 1 destroyed the blend *weight*: `main/splash.png` is 800x600 with a
    *transparent black* surround (452129 of 480000 px at `rgba(0,0,0,0)`), which then composited as an opaque black rectangle
    over the gray clear colour on the **native editor** boot splash — for seven months, in a path no WebGPU test covers.
    Fixed by gating the line on a `USE_ALPHA_BLEND` define given to that one variant, and keeping the swapchain opaque with
    `src_alpha = ZERO, dst_alpha = ONE` on the splash pipeline instead. `drivers/webgpu/` is compiled out of native builds,
    but `servers/rendering/` and its `*.glsl` are not: a driver quirk handled there needs a define, a variant or an
    `api_trait_get()`, never a bare statement. To check a boot splash, note the window is unpainted in an
    `import -window root` capture — grab it by id (`xdotool search --name '^Godot$'`) and sample pixels inside vs. outside
    the centered 800x600 rect; `application/boot_splash/minimum_display_time` holds it up long enough.
24. **Two SCons runs in one tree poison each other, and the damage outlives both** (2026-10-16). The shared
    `.sconsign5.dblite` is the known reason; the quieter one is generated headers whose content depends on the build
    options — `modules/modules_enabled.gen.h` differs by `debug_features` (`objectdb_profiler` is debug-only). A
    release-variant timing probe interleaved with a `template_debug` build compiled
    `register_module_types.gen.web.template_release.*.o` against the debug header while sconsign recorded the release
    one; the release link then failed on `initialize_objectdb_profiler_module` and the object was "up to date" on every
    later run. Same mechanism as Task 9.5 Round 15. `pgrep -ax scons` before any build, including a no-op one. To
    recover, delete every object whose source includes the header and was compiled in the overlap window (95 across 7
    variants that night, see §4.22); touching the header does not help, because sconsign already holds the right
    signature for it. **Half of this is fixed since §4.25**: the option-dependent generated sources now live under
    `gen/<variant>/`, one copy per build, so no run can see another's. The shared `.sconsign5.dblite` remains, and an
    overlap can still lose a run's recorded signatures (wasted recompiles, not wrong objects) — so still check
    `pgrep -ax scons` first.
25. **The compiler's path is an implicit dependency of every object, so a build-speed knob can cost a full rebuild**
    (2026-10-17). SCons records the resolved executable (`/usr/bin/g++` vs a ccache masquerade directory's `g++`) in
    each object's signature, and `c_compiler_launcher` changes the command string, which is signed too. Turning ccache
    on recompiles all 11 `build-linux.sh` variants once, and turning it off again does too. Since §4.26 ccache is on by
    default and `--no-ccache` keeps the masquerade on `PATH` (setting `CCACHE_DISABLE=1` instead of removing it), so
    the signed path no longer depends on the flag and toggling is free; only the first default-on run pays. **Measured 2026-10-17**: 2791 of 3146 objects in one web variant, which reads as "something corrupted the
    tree" and sends you hunting generated headers (it is not them — alternating platforms costs 30 objects before
    §4.25 and 0 after). `ccache -s` showing a full variant's worth of calls at a 0% hit rate is the tell. Masquerading `emcc` breaks `platform/web/detect.py` (`WhereIs("emcc")` + realpath locate the sysroot and
    JS libraries), so only `em++` and the native compilers are shadowed. Related measurement trap: a 4–8 minute editor
    "link" was never the linker (ld.bfd ~6 s, mold ~2 s with `debug_symbols=no`) but a recompile contending with
    another build; time a pure relink (move the binary away, `scons` again) before blaming ld.
26. **"Tint is slow" was never true of the export bake: the cost was one serialized `fork()` per stage** (2026-10-17).
    `bake_wgsl_via_subprocess()` spawned `tint_convert_cli` once per shader stage behind a process-wide mutex that
    existed for fork-safety (`OS::execute()` runs engine code in the child before `exec`), so 1,447 spawns ran one
    after another on a 24-core machine — 24 of a 27 s export at a mean of 17 ms each, while the conversion itself
    was a few ms. Measure the subprocess chain (wrap the tool in a timing script, count calls) before touching the
    converter. The fix was structural (§4.23): batch the stages, spawn with `posix_spawn()`, which runs none of this
    process's code in the child and needs no lock. Related: a template that predates the separate shader pack loads
    the game with **no** shader cache at all — no mismatch warning, just `baked: 0` and a slow start — and the
    exporter now warns about it; the same-commit rule in §1 covers it.
27. **A red line in a web export's console says nothing about severity, and not every line is ours** (2026-10-17).
    Until `d937802c50` the web platform had no warning channel at all: `OS` wrote warnings and errors alike to
    stderr, Emscripten routed stderr through `Module.printErr`, and every shell logged that with `console.error()`,
    wasm backtrace attached. So a one-time `WARN_PRINT` about an asynchronous readback -- the documented, correct
    behavior of this backend -- looked exactly like a crash, and a user reasonably reported it as one. Warnings now
    go to `console.warn` through `WebLogger` (§4.28); if you are reading a console from an older build, **the colour
    is not evidence** -- match the text against the source before concluding anything. Two further lines in that
    class: an uncaught `WrongDocumentError` from `requestPointerLock()`, which was a missing `.catch()` and not a
    capture failure, and `WebSocket connection to 'ws://127.0.0.1:6007/' failed`, which is **Chrome's own**
    network-stack log for an unreachable one-click debug host -- not suppressible, not re-levelable, and absent from
    a plain export. To see an export's verbose output, add `--verbose` to `GODOT_CONFIG['args']` in the exported HTML.
28. **A "feature works" delta says nothing about parity; four broken features passed the matrix for weeks** (2026-10-17).
    The Forward+ matrix asked only "does the frame change when the feature is toggled". SDFGI (25× off native while
    the camera moved), volumetric fog's shadows, omni cube shadows (forced to dual-paraboloid by a trait) and every
    `DEPTH_TEXTURE` read all changed the frame, so all passed. Compare against native: `run_forward_plus.mjs --parity`
    (§4.29). And a native reference is only a reference if both sides render the same frame — native with a 4-frame
    settle had not converged its radiance, web captures landed a frame or two late, one arm's resource changes leaked
    into the next, and `Texture2D.get_image()` returns an empty image at runtime on web. The fixture now runs both
    sides at a fixed 60 fps, settles 240 frames, freezes the captured frame (particles paused, render loop stopped
    after `frame_post_draw`), snapshots and restores every scene resource between arms, and builds its mip textures
    at load. When the fixture changes, re-derive every `minDelta` from the native deltas: the old ones carried the
    leaks (`glow_map` was calibrated at 20.8; its real delta, native and web alike, is 2.12).
29. **A depth texture in a float-sampled slot reads 0, silently** (2026-10-17). WGSL splits what GLSL calls
    `texture2D` into `texture_2d<f32>` and `texture_depth_2d`; a depth-format view bound to the former is a
    validation error, so the driver binds a blank fallback instead. Whether a GLSL texture becomes a depth texture
    is decided by `_reclassify_single_component_depth_textures()`, a **name and usage heuristic**, so any reader the
    heuristic misses sees an empty texture with no error. That was `DEPTH_TEXTURE`, refraction and proximity fade
    (`copy.glsl`'s depth copy), volumetric fog's shadow atlases, and `cube_to_dp`'s source. A depth binding now has a
    depth-ish name in the shader (`*_depth*`) and the driver `print_verbose`s every depth view it has to replace;
    run with `--verbose` when anything reads as "too bright/too dark only on web".
30. **Chrome's per-stage limits are exactly what Forward+ needs, so texture arrays cannot all expand** (2026-10-17).
    On the RTX 4080, Chrome reports `maxSampledTexturesPerShaderStage` 48 and storage textures 8, and 48 is the
    Forward+ threshold itself (CLAUDE.md). `expand_binding_arrays()` therefore gives each stage a budget (16 sampled
    by default, the whole 48 only for a module that already declares more than 16; storage 4/8) and leaves the
    remainder collapsed, with a one-time warning naming the binding. Do not "fix" a collapsed array by raising the
    budget without counting the stage's other textures: going past the adapter limit fails pipeline creation outright.
31. **`texture-formats-tier1` makes 16-bit unorm/snorm sampleable but not filterable** (2026-10-17). Enabling
    `R16_UNORM`/`RG16_UNORM`/... looks free and lets FSR2 drop its snorm→half conversion, then every linear sampler on
    them is a validation error (`unfilterable-float` vs a filtering sampler). Reverted; the formats stay unsupported
    and the engine's fallbacks apply. FSR2's conversion now runs only when the device lacks `R16_SNORM`.
32. **Sample playback is a second mixer, and nothing listened to it** (2026-10-18). On web every
    `AudioStreamPlayer` defaults to Sample playback (`default_playback_type.web`), which skips `AudioServer`'s mixer
    and hands the sound to Web Audio — so every rule `AudioServer::_mix_step()` applies has to be re-implemented in
    `library_godot_audio.js`, and none of it was tested: surround gains were one channel off, ping-pong and backward
    loops played forward, a send to an earlier bus did not fall back to Master, `move_bus` used the wrong index,
    solo honored mute, volumes stepped (clicks) instead of ramping, and `set_bus_layout()` resolved sends against
    freed buses. `webgpu_tests/web_audio/run_tests.mjs` now renders the driver in an `OfflineAudioContext` against
    JS ports of the native code (§4.30). Two follow-ons: **headless native is no timing reference** — the dummy
    driver's playback positions wander by ~100 ms run to run, so `run_e2e.mjs` checks numbers against what native
    playback does by definition; and **a per-frame "unchanged" cache belongs on the C++ side** — caching in JS still
    paid the string marshaling and the call (0.029 ms/frame) that a C++ compare avoids (0.002).
33. **A headless editor cannot be driven through its UI** (2026-10-18). `--headless` gives the editor's root viewport
    a 2x2 size, so every pushed mouse event lands outside it: a drag-and-drop test drops nothing and every check
    "fails" — including drops that have always worked, which is the tell. Run UI-driving editor tests windowed under
    Xvfb (`xvfb-run -a -s "-screen 0 1920x1080x24" ... --editor --rendering-driver opengl3`), as `tests/editor_drop`
    does. Two more from the same test: a modal dialog left open (Project Settings) swallows the main window's input,
    and after an edit the inspector rebuilds, so a control found before it may already be queued for deletion.
34. **A resource setter that does not emit `changed` hides the edit from anything that caches the resource**
    (2026-10-18). The web sample driver renders a stream once, on its first play; `AudioStreamOggVorbis::set_loop()`
    and most WAV/MP3 setters emitted nothing, so turning `loop` off at runtime left the cached sample looping forever
    and `finished` never fired. Natively nothing caches, so nothing showed it. The setters now emit `changed` and the
    driver re-renders on the next start (§4.30's follow-up).
35. **A module's test headers do not see the module's defines** (2026-10-18). `tests/test_main.cpp` includes
    `modules/*/tests/test_*.h` without the `CPPDEFINES` the module's own `SCsub` adds, so `#ifdef
    GDSCRIPT_BRACES_ENABLED` in a GDScript test header is always false there: a whole test case compiled to nothing and
    reported 0 test cases, looking like a filter typo. Check at run time instead
    (`GDScriptFormatter::get_braces_grammar_version()`), and declare members a test touches unconditionally.
36. **The GDScript tokenizer reports positions past the end for a token that ends the file without a line break**
    (2026-10-18). The last token's end column is past the text, and the synthetic `NEWLINE` after it starts on a line
    that does not exist. Code that turned (line, column) into offsets clamped the line and got offset 0 back, which
    sent the ternary rewriter into an endless loop (a crash) and made the formatter reject such files. Any new
    position-to-offset helper must map a line past the last to the end of the text; the fuzzer
    (`test_gdscript_braces_fuzz.h`) found both.
37. **A cheap WebGPU call can still be an expensive one -- on the other process** (2026-10-20). Every startup tool
    measured the page's main thread, where `createShaderModule` and `create*Pipeline` cost microseconds, and so §2.4 sat
    "effectively done" with ~1 s unexplained. Chrome does the actual work on the GPU process's main thread, which also
    produces the page's frames, and the startup steps wait for a frame: 5.9 s of compute pipeline compilation hid there
    as page-side idle time. Look at `CrGpuMain` (`startup_phases/gpu_process_trace.mjs`) before calling a stall CPU-bound.
38. **A commit between building the editor and the template unbakes every A/B** (2026-10-20). Shader cache keys mix in
    `GODOT_VERSION_HASH`, which is the git HEAD at build time, so committing between two builds makes the second one miss
    the whole baked cache (`baked: 0`) and an A/B "measures" Tint. For a before/after of a driver change, build the editor
    and both templates at one HEAD (apply/revert the change as a working-tree diff), export once, and swap only
    `index.wasm`/`index.js` between runs. The converter A/B of §4.36 hit the same thing from the other side: a
    template built from another commit than the editor that bakes for it loads `baked: 0`, so build every A/B
    template from a detached worktree at the editor's exact commit.
39. **A software adapter runs Forward Mobile, so a Forward+ startup cannot be measured there as-is** (2026-10-20).
    swiftshader reports 16 sampled textures per stage. To exercise Forward+'s shader loading and pipeline creation in a
    GPU-less container, build a scratch template with the `textures_per_stage < 48` fallback in
    `RendererCompositorRD::initialize()` lowered to 16 (never commit it): shader loading, caching and timing are real,
    but draws whose pipelines exceed the adapter's limit fail, so it says nothing about the image.
40. **A shader whose variants are chosen from a capability at construction is invisible to the baker** (2026-10-20).
    The baker bakes the versions that exist in the editor, flavored by the target's `has_feature()` answers only where
    a subsystem recomputes its defines through `ShaderRD::add_general_defines_refresh_callback()`. A constructor that
    reads a capability or a limit once -- `FSR::FSR()`'s half-float variant text, `CopyEffects` creating raster octmap
    shaders only below six storage images -- produces the editor's flavor or nothing, and the export misses it at
    load (`translated` > 0, "Shader cache miss" in `--verbose`). Register a refresh callback that recomputes the text,
    or creates the target's versions, whenever a new effect makes such a choice.
41. **`actions/upload-artifact` drops Unix permissions, so a released editor uploaded as loose files cannot start**
    (2026-10-20). Every Linux and macOS editor release up to v1.5 shipped `Godot`/`godot.linuxbsd.editor.x86_64` and
    `tint_convert_cli` as `-rw-r--r--` (`zipinfo` on the release zip shows it): macOS will not open such a `.app`, and
    the `chmod +x` in linux_builds.yml happened before the upload that discarded it. The binaries themselves were fine
    (universal, ad-hoc signed with the hardened runtime). Editors are now zipped on the build runner
    (`.github/actions/package-editor`, `ditto` on macOS) and the release copies that zip instead of re-zipping the
    downloaded files; the action also unpacks the zip and starts the editor from it, so the next loss fails the build.
42. **`local_ci.sh` stopped at the first test that did not pass, with no summary** (2026-10-20). Under
    `set -euo pipefail`, `run_test()`'s `output=$(cd "$dir" && "${cmd[@]}" 2>&1)` ended the script on any non-zero
    exit, before `status=$?` was read, so its SKIP (2) and FAIL (1) branches never ran: a run without
    `WEBGPU_REAL_GPU=1` died silently at the fog-smoothness SKIP, and a real failure would have looked the same. It
    now captures the status with `|| status=$?`. If a run ends without its results table, that is what happened.
43. **A Playwright Chrome launch without `headless: false` runs headless, which on this machine cannot present WebGPU**
    (2026-10-20). Playwright defaults to headless; there Chrome gets only the swiftshader fallback adapter (16 sampled
    textures, so the Mobile renderer) and loses the device at the first canvas present (GPU process: "Could not find
    SharedImageBackingFactory ... WebgpuSwapChainTexture"). A test then fails only if its first frame lands inside its
    window, so any change to startup time moves the failure in or out -- `webrtc_parity` bisected cleanly to an
    innocent commit that made startup faster. Set `headless` explicitly in every launch, and log the adapter
    (`adapter.info`, `isFallbackAdapter`) before trusting a "real GPU" run.
44. **A project without a `rendering_method.web` override exported for WebGL, whatever its renderer** (2026-10-20,
    fixed). The override defaulted to `gl_compatibility`, so a Forward+ project that never set it ran Compatibility on
    the web; with a WebGPU-only template (`opengl3=no`) that is a black canvas with no renderer line in the console,
    while the main loop still ticks at 60 Hz -- it read as "57 fps, rig costs nothing". The default now follows
    `rendering/renderer/rendering_method` (`main/main.cpp`): Forward+ and Mobile projects get WebGPU, Compatibility
    projects keep WebGL. A template built before the change still defaults to Compatibility, so rebuild templates.
45. **The baker's variant pruning never saw imported scenes** (2026-10-20, fixed). Its scan for features that need
    Forward+'s advanced group (`SSS_STRENGTH`, `subsurf_scatter_enabled`, ...) skips hidden folders, and every
    importer's output lives in `.godot/imported`. A glTF/FBX/MetaHuman material with subsurface scattering therefore
    got its advanced variants pruned, and the browser compiled them with Tint: 360 scene shaders, the page frozen for
    over 45 s. `_compute_unmet_group_requirements` now also scans the imported files path.
46. **The export cache kept shipping a reimported file's previous output** (2026-10-20, fixed). `.godot/exported` is
    keyed by the source file's mtime, else its md5 plus its `.import` file's; a reimport with unchanged options (an
    importer improved by an engine update, a deleted import cache) changes neither, so the export reused the old
    customized scene and its stale baked shaders (46 translated in the browser). The key now includes the import's
    own `.md5` file (`_export_cache_md5()` in `editor_export_platform.cpp`).
47. **The scene importer overwrote what a format importer set on `ImporterMeshInstance3D`** (2026-10-20, fixed). It
    fills every per-node option with its default and then applies them, so the MetaHuman importer's LOD visibility
    ranges (and any cast-shadow or layer choice) came out as 0..0. Those defaults now come from the node itself
    (`_post_fix_node` in `resource_importer_scene.cpp`); a user's per-node setting still wins.
48. **A MetaHuman manifest's per-mesh material list is wrong from LOD 3 down** (2026-10-20). `dna_geometry[].meshes`
    repeats LOD 0's nine material slots for every LOD, but the DNA drops saliva and cartilage from LOD 3, so from there
    every mesh gets its neighbor's material and `eyeLeft_lod3` was hidden as saliva. Match materials by the DNA mesh
    name instead (`eyeLeft_lod3_mesh` -> slot `eyeLeft_shader_shader`); the geometry JSON's own names are right.
49. **A material's usage defines came out in a different order in the editor and the template** (2026-10-20, fixed).
    `ShaderCompiler` appends `UV_USED`, `UV2_USED`, ... in the order it first meets each built-in, and the two
    disagreed on that order for the groom strand shader. The shader cache key hashes the list, so the browser asked
    for a version the export never wrote: 40 stages translated by Tint at load, `translatedShaders` naming only
    `SceneForwardClusteredShaderRD` groups. The tell is in a `--verbose` run (`GODOT_CONFIG['args']`):
    "Shader cache miss for ..." with a `^ version is:` fingerprint. Compare its define list with the export's
    `--verbose` "Shader baker: baking ..." fingerprints. The defines are now sorted at the end of compilation.
50. **`PROJECTION_MATRIX[1][1]` can be negative** (2026-10-20). Godot's projection may flip Y, so pixel-size math
    must use `abs()`. Without it the strands' "at least 2 px" clamp was silently off: binary stipple brows at
    portrait distance.
51. **Camera-facing geometry in a shadow pass faces the light's camera** (2026-10-20). With a pixel-width clamp
    computed from that camera (far, huge shadow atlas), strands became giant shadow casters that darkened the whole
    lower face. Branch on `IN_SHADOW_PASS`: face `INV_VIEW_MATRIX[2]`, use the true width.
52. **A Verlet hair solver's rest sag depends on its step size** (2026-10-20). Gravity enters as step², while the
    shape constraint acts per step. A rest pose settled at 1/120 s drifted 1 cm when the frames came at another
    rate. Simulate at a fixed step with an accumulator.
53. **On GitHub's runners the Firefox smoketest never ran the engine** (2026-10-21, fixed). Headless Firefox with no
    display cannot read the GPU, so it blocklists WebGPU ("WebGPU is disabled by blocklist"). `gfx.webgpu.force-enabled`
    and `gfx.webgpu.ignore-blocklist` do not override that, so `requestAdapter()` returned null and no scene started.
    The 20 scenes without required logs "passed" with zero errors; only `demo_compute_heightmap`, `audio_parity` and
    `braced_gdscript` failed, and only for want of their pass line, which kept 🧪 WebGPU Tests red on PR #19.
    The fix: the Firefox steps run under `xvfb-run` (lavapipe then gives a fallback adapter), and `run_scenes.mjs` now
    fails any scene whose engine never prints its banner. Reproduce locally with
    `env -u DISPLAY VK_ICD_FILENAMES=.../lvp_icd.json`. Separately, `click_to_start` clicked once, which could land
    before Godot listened for it (`audio_parity`'s intermittent Chrome failures): it now clicks every 2 s until the
    checks are in.
54. **`PrimitiveMesh::surface_get_arrays()` read the mesh back off the GPU, so on web it returned zeros**
    (2026-10-21, fixed). A `CapsuleMesh` handed to a GDExtension collider (TressFX-Godot's `TressFXCollisionMesh`)
    failed in a web export with `mesh has no extent` — its own `verts.is_empty()` check had passed, so the arrays came
    back the right *size* with every vertex at the origin. `PrimitiveMesh` threw away the arrays it generated in
    `_update()` and re-read them through `RenderingServer::mesh_surface_get_arrays()`; on WebGPU a readback is
    asynchronous, and a first one returns the zero-filled shadow buffer (`RenderingDeviceDriverWebGPU::buffer_map()`,
    documented in §2.13 — it is `print_verbose`, so nothing in the console connects the zeros to the readback unless
    the export runs with `--verbose`). The fix is `_build_mesh_arrays()`, shared by `_update()` and
    `surface_get_arrays()`: the arrays are rebuilt on the CPU from `_create_mesh_array()`, which is correct on every
    backend, drops a pointless GPU round trip, and no longer loses the quantization the vertex format applies. The
    general lesson: **any driver-agnostic engine code that reads a GPU resource back synchronously is wrong on
    WebGPU** — if the data was on the CPU a moment earlier, recompute it instead. Worth sweeping for other callers.

55. **A WebGPU texture view cannot swizzle, and the driver drops a swizzle silently** (2026-10-25). `RD::texture_create_shared()`
    with a `TextureView` whose `swizzle_*` is not identity creates a plain view: WebGPU has no component swizzle, and
    `RenderingDeviceDriverWebGPU` stores the swizzle without applying it ("WebGPU has no texture component swizzle").
    No error, no warning. FSR2's reactive mask was such a view (`get_internal_texture_reactive()`, alpha broadcast to
    RGBA), so FSR2 read the scene's red channel as reactivity, every lit pixel was reactive, and the output was soft:
    0.55 from native, Laplacian sharpness 0.83× native — which had been accepted as "FSR2 is a little softer on web".
    amd-fsr2 patch 0005 reads `.a` itself; sharpness is now 1.000× native. Anything that relies on a swizzled view needs
    the swizzle in the shader (§2.43 audits the rest). Metal honors swizzles, so `get_internal_texture_reactive()` stays
    for MetalFX.
56. **A parity capture must land on a fixed temporal-jitter phase, and FSR2 at scale 0.5 has 32 of them** (2026-10-25).
    Volumetric fog (Halton, `frame % 16`), TAA and FSR2 jitter by frame number, and a settled temporal effect still
    differs by phase: native Vulkan measures 0.045–0.082 against itself one or two phases apart. The Forward+ fixture's
    capture phase depended on whether the harness's first hash change landed in boot frame 1 or 2; baked exports boot
    fast enough to straddle it, so fog "flipped between two exact states" (0.046 / 0.082) and was reported as a race in
    the baked path. `fp_scene.gd` now aligns every capture to phase 0 (`--fp-phase=N` to triage, `[FP] FRAME n (phase p)`
    logged). The period is `JITTER_PERIOD` 32, not 16: FSR2's phase count is `8 / scale²`, so aligning mod 16 left the
    FSR2 arm on one of two phases at random. Lesson: when a result takes exactly two values, print the frame number first.
    Native `--reference` directories saved before this are invalid.
57. **A stale `export_baked/` gives false parity FAILs** (2026-10-25). `run_forward_plus.mjs --baked` reads
    `forward_plus/export_baked/`, which only `BAKE=1 ./export.sh` refreshes; a plain `./export.sh` after a rebuild leaves
    it hours old. A lead run failed FSR2 (0.578) and volumetric fog (0.071) that way, on a tree that passes. The harness
    now refuses an export older than its template, the editor or `fp_scene.gd` (`--allow-stale-export` overrides).
58. **Mali-G615 / Chrome 155 ran a WGSL `for (var i = -3; i <= 3; i++)` zero times** (2026-10-25, cost ~2 h).
    Volumetric fog's filter pass lost its density on the Razr 60 only (12.6 from native); the same loop written as
    `loop`/`while`, or counted from 0, is correct, and desktop shows nothing. SPIRV-Cross emitted `for` where Tint never
    had; patch 0019 makes it emit only `loop { if (!c) { break; } ... }`. A phone-only look bug is bisected on device by
    swapping single modules' WGSL (`phone_bench.mjs --swap-wgsl`), not by reading the WGSL.
59. **A `fetch(..., { keepalive: true })` body is capped at 64 KB** (2026-10-25). Above it the fetch rejects — silently
    if the rejection is caught — so a page script that reported its frames that way lost every frame. `phone_bench.mjs`
    sets `keepalive` only below 60 KB.
60. **Firefox hides `GPUAdapterInfo`** (2026-10-25). vendor and architecture are empty, so a Firefox number cannot prove
    from inside the page that it ran on the real GPU (trap 1, trap 43). `perf_guard/frame_time.mjs` logs the browser's
    row in `nvidia-smi`'s process table instead (C+G, ~1.6 GB). Firefox does expose `timestamp-query`, so
    `gpu_time.mjs --browser firefox` works. Linux Firefox also needs wgpu pinned to Vulkan
    (`scene_smoketest/firefox_prefs.mjs`, `dom.webgpu.wgpu-backend`).
61. **An editor built before the converter rename cannot bake** (2026-10-25). The editor runs `wgsl_convert_cli` from its
    own directory (`wgsl_bake_subprocess.cpp`); an editor from before work/9 looks for `tint_convert_cli`, and a new
    editor beside only a stale `tint_convert_cli` finds nothing either. The export warns once ("not found next to the
    editor executable") and ships every shader unbaked: it works, and loads slowly (`baked: 0`). Rebuild editor, tool
    and templates together, and update anything that copies the tool by name (CI artifacts, older scripts).
62. **`git -C <repo> worktree add <relative path>` resolves the path against `<repo>`, not the shell's directory**
    (2026-10-25). A worker briefly created its worktree *inside* the lead's checkout that way. Always pass an absolute
    path.
63. **Chrome refuses fullscreen requested on a touch press** (2026-10-25). `requestFullscreen()` needs user activation,
    and a touch gives it on `touchend`, not `touchstart`. Godot delivers `InputEventScreenTouch` with `pressed = true`
    from touchstart, so `window_set_mode(WINDOW_MODE_FULLSCREEN)` on a press fails silently on the first tap and "works"
    on the second (the first tap's activation is still live). Request it on the release; the export's own
    `html/fullscreen_on_first_tap` does (§4.37).
64. **A scan of generated WGSL that finds nothing does not fail** (2026-10-25). The driver reads binding facts out of the
    converter's text; when the text's shape changes, the scan just misses and the binding silently gets a default.
    SPIRV-Cross keeps a sampled texture in `let _27 = textureLoad(depth_tex, ...)` and swizzles at each use, where Tint
    swizzled the call, so the depth reclassifier would have left SSR's `source_depth` a float texture with the blank
    fallback (trap 29) — the kind of bug only the parity matrix finds by eye. Every such read now goes through
    `wgsl_text_scan.{h,cpp}` (structural, with cases in `webgpu_tests/wgsl_text_scan/cases/`), checked against
    `scan_reference.txt` for every engine variant.
65. **SPIRV-Tools' optimizer asserts on invalid SPIR-V, and a structural pre-check is not enough** (2026-10-25, cost about
    an hour). Ids, function references and block terminators look like all the IR loader needs, and a scan for them
    stopped the first three fuzzer asserts; the fuzzer then found AggressiveDCE asserting on an `OpLoad` from a
    non-pointer, and `expand_binding_arrays` getting result id 0 once the id bound neared IRContext's 0x3FFFFF limit
    (`TakeNextId()` returns 0, unchecked). Only the validator rules out type errors: `run_pipeline()` validates untrusted
    input; `check_ir_preconditions()` is only a guard against our own passes' bugs. The fuzzers had been blind too —
    linked against uninstrumented pass objects, ~200 coverage edges instead of ~5,000 — so a clean fuzz run said nothing
    until §4.36.
66. **With the frame-rate limit lifted, Chrome lets a WebGPU page queue GPU work faster than the GPU finishes it**
    (2026-10-26, cost a full benchmark run). rAF keeps firing while the GPU falls behind, so a window's frame time reads
    too fast as the queue grows (load-ramp fill "carried" 2x native Vulkan's layers), and the backlog then slows every
    later window to ~150 ms whatever the load is, into the next scene on the same page (it looked like a 100x "cliff at
    32 spheres"). A GPU-bound WebGPU measurement with `--disable-frame-rate-limit` must bound frames in flight:
    `benchmark/ramp_browser.mjs` gates each rAF on the `onSubmittedWorkDone()` of the frame before last (2 in flight, as
    native Vulkan). Paced, fill read 0.67x native. WebGL is throttled by the browser and needs nothing. Also: one scene
    per page load, and treat a lost device as the end of the run (a lost device "renders" at 0.2 ms a frame).
67. **`OS.get_static_memory_usage()` is 0 in release web templates** (2026-10-26): memory tracking is a debug-build
    feature, so a script budgeting against the 2 GB web heap sees nothing until `alloc_static` fails ("Parameter
    \"mem\" is null") and the page dies. Read the heap size from the page (`WebAssembly.Memory.buffer.byteLength`;
    `ramp_browser.mjs` captures the instance's memory export and `ramp.gd` reads it through `JavaScriptBridge`).

68. **A shader cache left in the parity fixture by another engine build changes the native reference** (2026-10-26, cost
about an hour). After a rebuild, `--parity` failed SDFGI by 7.05 in Chrome, unbaked and baked, while the web frame
was within 0.03 of what native had rendered the day before. The native editor was reusing
`fp_check/.godot/shader_cache` from an earlier build: five pipelines failed ("Mismatch fragment shader output mask (1)
and framebuffer color output mask (0)") and native SDFGI picked up extra bounce light. Moving the cache aside gave 0 errors
and SDFGI 0.027 again. `run_forward_plus.mjs` now deletes that cache before rendering the reference. Lesson: when a parity
arm fails, compare the web frame with an older *native* frame first; the reference can be the side that moved.

69. **In a `threads=no` build `Thread::start()` does nothing, silently** (2026-10-26). `core/os/thread.h`'s no-threads
`Thread` is a stub: `start()` returns at once and `is_started()` stays false, so code that hands work to a `Thread`
and polls for it never runs that work. The raycast module's occlusion culler committed its Embree scene that way,
so a nothreads web template with the module raycast every frame against nothing and culled nothing, with no error
(§2.45). Any `Thread` in code that can run in a nothreads template needs a synchronous `#ifndef THREADS_ENABLED` path;
`WorkerThreadPool` is fine (it runs tasks inline).

70. **One NaN pixel becomes a black bokeh shape (NaN × 0 = NaN)** (2026-10-26, §4.41). The bokeh DOF weights every tap
with `mix(center, sample, m)`; with `m == 0` a NaN sample still poisons the sum, so a single non-finite pixel blanks
everything within the blur radius in the bokeh's shape (hexagon by default; 32 px with physical transitions). When a
black shape appears, look for one NaN pixel first, and find it with a **read-back**, not a shader probe: a probe
rewrites the expression that made the NaN, and a blended marker is buried by the layers drawn after it (92 lash layers
at the pixel here). What worked: a `CompositorEffect` at `POST_TRANSPARENT` that scans the color image for exponent
bits 0x7f800000 (alpha NaN too ⇒ the material's `ALPHA` was NaN, since blending can only put NaN in alpha from the
source alpha), then the material swapped for one with the same vertex stage and an **additive** fragment that counts
suspect values. Test NaN on the bits, never `isnan()`: compilers may fold it (WGSL explicitly allows that). And
`pow(x, y)` of an interpolated value that is 0 at some vertices needs a clamp: interpolation lands at -1e-9.
Mobile hides the whole class unless the viewport is HDR (its RGB10A2 buffer stores NaN as 0).
Follow-up (§4.44): glow, SSR and auto exposure spread it too, and not always as black. On NVIDIA, `min(NaN, cap)`
returns the cap, so glow turned one NaN pixel into a **bright** blob, and `notEqual(x, x)` is false for a NaN, so
upstream's own SSR sanitize let it through; auto exposure blanked the whole frame. Measure "pixels changed", not only
"pixels black", and test NaN on the exponent bits (`finite_inc.glsl`), which is the only test that held on every path.
A NaN in SSR's input also persists for frames after its source is gone (seen as black bars in a NaN-free reference).

71. **An importer or importer-shader change does not reach a scene that is already imported** (2026-10-26). Godot
reimports a source only when it or its `.import` settings change, and a MetaHuman's `head.dna` never does: the
shaders are baked into the imported scene, so after a fix in `metahuman_shaders.h` or the importer, an existing project
(the user's camera-sim, or `webgpu_tests/metahuman/work/`) keeps rendering with the old shader. `metahuman/run.sh`
failed the new NaN check on a 10-20 import with a fixed editor (7032 NaN pixels) and passed after a fresh one. `run.sh`
now drops `.godot/imported` whenever the editor binary is newer than its last import; a user project needs
**Reimport** on `head.dna` (or `.godot/imported` removed) after such a change.

72. **An RG-compressed normal map samples blue as 0** (2026-10-26, §2.33). Godot imports normal maps as RG (BC5 / RG8)
with swizzle b = ZERO, so a custom shader that reads `.rgb * 2.0 - 1.0` gets z = -1, not the normal's z. Rebuild z from
xy (`sqrt(1 - dot(xy, xy))`), or pass xy to `NORMAL_MAP`, which Godot completes itself. The MetaHuman skin shader had
it (task 25); the cloth shader still does.

73. **The shader baker's capability override reached the editor's own frames** (2026-10-26, user report, §4.43). A
WebGPU export makes `RenderingDevice::has_feature()` answer as WebGPU (no image atomics, no FP16, ...) so the baked
shaders get WebGPU's defines. The override was global and lasted the whole export, and the editor draws frames between
the export's steps (every progress step does). So with FSR 2 on in the project, FSR 2's per-dispatch
`has_feature(SUPPORTS_IMAGE_ATOMIC_32_BIT)` said "no" and bound storage buffers to the editor's image-atomics shaders:
`Shader 'Fsr2ComputeLuminancePyramidPassShaderRD:0' Mismatch uniform type for binding (1), set (1). Expected 'Image',
supplied: 'StorageBuffer'` (and the depth clip, reconstruct and lock passes), every editor frame of the export. **The
log reads like a web runtime failure and is not one**: it sits between the export's "Shader baker:" lines, and the `:N`
after the shader name is the variant, which proves the device: `:1` on the depth clip pass is the FFX_HALF variant,
which only exists on an FP16 device (the Vulkan editor), and `:0` on the luminance pyramid is the image-atomics
variant, which WebGPU never declares. `--export-release` never draws the 3D viewport, which is why no tier caught it.
Fix: the override applies only on a thread inside `RD::ShaderBakeFeatureOverrideScope`, opened by the baker's own
callbacks. Every other per-frame `has_feature()` (fog, GI, VRS, FP16, depth resolve) had the same exposure silently.
Same root, found on the way: **FSR 2 built its variant list from `has_feature()` once, at construction**, so the bake
baked the editor's flavor under cache keys the WebGPU runtime never looks up and dropped 7 stages as unconvertible (a
baked FSR 2 export missed 7 stages in the cache: measured 7 → 0 after the fix). A capability-dependent variant must be
a shader group with `set_group_bake_requirements()` (fog.cpp, fsr2.cpp) or refreshed by
`add_general_defines_refresh_callback()` (fsr.cpp). `webgpu_tests/bake_isolation/run.sh` catches both.

74. **An SCU build compiles only the test folders `scu_builders.py` names** (2026-10-26, §4.46). `tests/SCsub` globs
every `tests/**/*.cpp` into `force_link.gen.h`, but with `scu_build=yes` it compiles only `tests/.scu/*.cpp`, which
`scu_builders.py` generates from a hand-written folder list. A test in a new folder (`tests/editor/project_manager/`,
§4.45) is then referenced and never built: `ld.lld: error: undefined hidden symbol:
ForceLink::force_link_test_engine_update_label()`. Only CI's GCC sanitizer editor builds `scu_build=yes`, an hour into
the run, so every local build and every other CI job passed. A new folder under `tests/` goes into that list
(`"/editor/project_manager"` beside `"/core/variant"`).

75. **Main's static checks run on the merge's whole diff, not only on code** (2026-10-26, §4.46). `static_checks.yml`
runs `prek --from-ref <the previous main>` over every file the merge brings, so a work board's notes, result JSON and
helper scripts (`webgpu_notes/work/`) are checked like engine code: shebang scripts must be executable, ruff targets
Python 3.9 (no backslash inside an f-string expression, which 3.12 allows), codespell wants US spellings, file-format
wants final newlines and no trailing space. #20 failed this in 1m44s and none of its builds ran. Check before a PR
with `pre-commit run --from-ref origin/main --to-ref HEAD`, **with the fixes staged**: pre-commit stashes unstaged
changes and checks the committed text, so an unstaged fix looks as if it did nothing.

76. **An `EM_ASM` body must not name anything of Emscripten's** (2026-10-27, user report). Closure (CI's
`use_closure_compiler=yes`, so every release template) renames the runtime and library objects (`$WebGPU`,
`UTF8ToString`, `stringToNewUTF8`, `_free`, `wasmTable`, `GodotConfig`) and their unquoted properties. A regular
template failed `RenderingContextDriverWebGPU::initialize()` with `M.importJsDevice is not a function`: the EM_ASM got the
renamed object but read `WebGPU["importJsDevice"]` by its old property name. A dlink template's EM_ASM bodies live in
the side module and are `eval`'d at load time, outside the renamed glue: `WebGPU is not defined` without threads,
`stringToNewUTF8 is not defined` (render thread prestart) with them. Local builds skip closure, and `local_ci.sh` only
compiles the dlink template, so no tier caught any of it. Such JavaScript goes in a JS library function, whose `__deps`
are renamed with it (`platform/web/js/libs/library_godot_webgpu.js`: device import, shader stats, the render thread's
device and yield; `godot_js_os_startup_mark()`). An EM_ASM may use `Module['...']`, `globalThis`, browser APIs and its
`$N` arguments. Only the opt-in `WEBGPU_VERBOSE` diagnostics still break the rule.

77. **`CanvasItemMaterial` and `ParticleProcessMaterial` build their shader on an idle frame too** (2026-10-27, user
report). Task 32 made the shader baker call `BaseMaterial3D::flush_changes()` before reading a material's shader, but
the other two classes queue the same way (`register_scene_types.cpp` registers all three as idle callbacks) and were
never flushed. No frame ticks during an export, so a `CanvasItemMaterial` the export loads fresh (a `GPUParticles2D`'s
particle-animation material, say) has no shader yet and nothing is baked for it. The tell is one whole
`CanvasShaderRD` version in `translatedShaders`: `CanvasShaderRD:0`, `:1`, `:2` and `:4` once each, with `:3` and
`:5` as `cached` hits (the `USE_POINT_SIZE` variants compile to the same SPIR-V as 2 and 4). The baker now flushes all
three (`ShaderBakerExportPlugin::_flush_material_changes()`), before its embedded-material snapshot and before each
material lookup. A new class that defers its shader the same way must be added there.

78. **`--fixed-fps N` does not give a constant delta, and a `-s` script's `await process_frame` runs before nodes
process** (2026-10-28, work task 38). With N different from the physics ticks per second the process delta alternates
(144 fps gives ~1/120 and ~1/150). A convergence or timing test must integrate the deltas the node actually used (record
them from a node with a higher `process_priority`), never assume k/N.

79. **A browser test without WebGPU flags or prefs fails in ways that look like engine bugs** (2026-10-28, user report,
work task 42). Chrome needs `--enable-unsafe-webgpu --enable-features=Vulkan --use-vulkan --ignore-gpu-blocklist`
(system Chrome, headed, `--enable-unsafe-swiftshader` ignored); Firefox on Linux has no WebGPU at all without
`webgpu_tests/scene_smoketest/firefox_prefs.mjs`'s `FIREFOX_WEBGPU_PREFS`. A delegated worker launched both without
them. Every launch now goes through those settings and asserts the adapter first (Chrome nvidia/lovelace, not
fallback, 48 sampled textures; Firefox an adapter at all), and the rule is in `webgpu_notes/work/README.md`, which every
worker reads; a worker's prompt repeats it.

80. **An 8-bit capture of a glare source measures the clip, not the glare** (2026-10-28, work task 40). Task 35 measured
halation at radius 1 as 6.82 px against a 3.36 px model from 8-bit sRGB renders: the source disc saturates, so its
apparent width enters the halo fit. HDR captures (RGBA16F 2D, Linear tonemapper) fitted with the model convolved with the
effect-off frame read 3.95 px for the same chain. Measure glare from HDR captures
(`results/40-camera-answers-wave2/tools/analyze_halo.py`).

81. **A build helper that picks "the newest `bin/godot.web.template_debug*.zip`" installs whichever variant was built
last, under whatever name it was given** (2026-10-28, work task 50): `build.sh`, `build-macos.sh` and
`build-windows.ps1` built only the dlink pair and installed it as `web_nothreads_*.zip`, so exports from those templates
used a GDExtension template whether or not the preset asked for one. Spell the scons output name out
(`.wasm32[.nothreads][.dlink].zip`).

82. **A web JS function that both stores and applies a size starves the per-frame "did it change?" check** (2026-10-28,
work task 52). `godot_js_display_desired_size_set()` resized the canvas itself, so `process_events()` saw no change and
nothing that hangs off that check ran: no `size_changed`, the viewport and the WebGPU surface kept the old size. Return
whether it changed and run the same path a browser resize does. Under the render thread the transferred `<canvas>`
element's `width` is stale; read `DisplayServer.window_get_size()` or `GodotDisplayScreen.transferred_size`.

83. **A headless editor export has no shader pack** (2026-10-28, work task 53). `--headless` has no RenderingDevice,
so the export-time WGSL bake is skipped silently: no `<name>.shaders.pck` / `.shaders_extra.bin`, and `shaderPack` /
`shaderExtras` are missing from `GODOT_CONFIG`. A test that needs them must export under `xvfb-run -a <editor>
--rendering-driver vulkan` (as `webgpu_tests/forward_plus/export.sh BAKE=1` does).

84. **The luminance multiplier points opposite ways in RD and GLES3** (2026-10-28, work task 46). In RD, scene light =
stored x multiplier (Mobile stores half, `RenderSceneBuffersRD::get_luminance_multiplier()` returns 2); in
Compatibility, stored = encoded x multiplier and `post.glsl` divides. An effect that reads the frame multiplies in RD
and divides in GLES3; backwards, Mobile's light shafts were silently zero. Compatibility is reverse-Z too
(`GL_GEQUAL`, sky at depth 0).

85. **The GLES3 shader builder reads a `uniform` line to its end**, so a trailing `// comment, with, commas` turns into
bogus enum entries (`SCALE.,`) in the generated header (2026-10-28, work task 46). Put the comment above the uniform.

86. **Godot's Environment color correction LUT is sampled at texel centers ((i + 0.5) / N), not lattice points**
(2026-10-28, work task 45), unlike the camera LUT. A LUT of lattice points through that slot lifts black to the first
texel's value (1.5% at 33³). The color grading LUT carries a flag (Forward+ push-constant bit 24, Mobile
`color_correction_lattice`) that remaps to lattice points; anything else reusing the slot must do the same.

87. **A `SceneTree` `-s` script's `root.size` in `_initialize()` is not the window's yet** (2026-10-28, work task 47):
64x64 headless, square under xvfb. A camera FOV computed from it is wrong and the run still "works". Pass the size in,
or compute it in the first frame.

88. **Playwright's `keyboard.press('Escape')` does not leave fullscreen in Chrome** (2026-10-28, work task 54; it does
in Firefox). The key goes to the renderer, not the browser UI that owns the exit, so a test relying on it stays in
fullscreen and every following step fails as if the engine were wrong. Leave with `document.exitFullscreen()` in the
fullscreen document. Also seen the same day: Firefox now and then ignores a parent page's own `iframe.requestFullscreen()`
in a long fullscreen sequence (lead's first run of `embed_test.mjs`: 6 checks failed with the game never reached, a
rerun passed 59/59); a single fullscreen failure there is not an engine regression until it reproduces.

89. **WGSL `clamp()` keeps NaN** (2026-10-28, work task 48). A non-finite value that a Vulkan driver's
`clamp()`/`min()`/`max()` happens to scrub stays NaN in WGSL on Chrome, and an R8/RG8 store of NaN reads back as 0:
GTAO's sky came out black on the web only (infinite background depth gave NaN denoiser weights). Keep linear depths
finite (clamp to the storage format's range) and test background pixels with `!(x < limit)`, which is also true for NaN.

90. **`textureLod` at a fractional lod is not a linear blend of two mips on every GPU** (2026-10-28, work task 55). On
the RTX 4080 SUPER (Vulkan) with an anisotropic sampler, lod L + t weighs mip L+1 by exactly min(2t, 1). A blur that
picks its size with a fractional lod is then wrong between lods, by up to 2x, and differently per GPU and browser. Read
the two integer lods and `mix()` them yourself when the blend weight matters.

91. **Glow's raster downsample does not keep levels at their UVs on odd sizes** (2026-10-28, work task 55): texel j
reads coordinate 2j+1 of the level above, so level L's texel x holds lod 0's 2^L x. Glow's own upsample undoes it, but
anything sampling the raster mip chain by UV (Mobile's glare chain) must read level L at uv * size(0) / (2^L size(L)),
or coarse levels drift (a halo 61 px off its light at 2160p). Also: a blend of two blur levels is not a Gaussian, so a
Gaussian fit against a source as wide as the blur reads several percent wide; test PSF-sized effects with sources well
below the blur.

92. **The gles3 effects shaders did not depend on the parent folder's includes** (2026-10-28, work task 56; fixed in
`drivers/gles3/shaders/effects/SCsub`, which now adds `../*_inc.glsl`). `post.glsl` includes `../tonemap_inc.glsl`, so
after editing `tonemap_inc.glsl` the scene and sky shaders rebuilt but `post.glsl.gen.h` kept the old text, and the
Custom tonemapper rendered pure white whenever Compatibility ran its post pass. A build that "compiled fine" but
renders an old shader: check the `.gen.h` dates.

93. **An HDR 2D viewport's `get_image()` returns null on the web** (WebGPU, template_debug, no error), and natively
it is RGBH (3 channels, format 14), not RGBAH (2026-10-28, work task 57). A web test cannot read HDR values back that
way; compare WebGPU with native in 8 bits and let native carry the HDR comparison. Also: look at one frame of a new
test scene before trusting numbers from it (task 57's camera first sat behind its room's back wall, and the "match"
was a flat wall).

94. **Check hand-built SPIR-V probes with the converter, not only the distro's `spirv-val`** (2026-10-28, work task 58):
the system `spirv-val` (v2025.1) accepted an f32-to-f32 FConvert that the vendored SPIRV-Tools validator in
`run_pipeline()` rejects, so `run_spec_ops.sh`'s probe looked valid by hand and failed in the pipeline.

---

### Measurement traps (found 2026-10-15)

- **A fixed frame-count warm-up is a startup measurement once frames are uncapped.** 180 frames at 900 fps is 0.2 s; the
  same export read 289, 668 and 961 fps in three runs. Warm up by wall-clock too (`benchmark_profiler.gd` now does).
- **A slow-looking WebGPU call is usually the main thread waiting on the GPU process**, not copying: a 44 KB `writeBuffer`
  took 300 us, a 133 KB one 29 us. Attribute with engine-side wasm self time (`perf_guard/cpu_steady.mjs`), not call time alone.
- **Judge CPU- vs GPU-bound first — and uncapped "100% busy" does not mean CPU-bound** (corrected 2026-10-16). Uncapped, the
  main thread blocks inside `getCurrentTexture()`, `createView()` and `submit()` when the GPU is behind, and that wait counts as
  busy: the heavy scene read 100% busy with 1.7 ms/frame in those three calls while its real CPU cost was 2.85 ms of a 4.4 ms frame.
  Two checks that do separate them: **capped** busy % (`api_profile.mjs` without `--uncapped`: 47% x 6.06 ms = CPU ms/frame, and
  the three calls drop out of the list) and **GPU time** (`perf_guard/gpu_time.mjs`: timestamp queries per command buffer, or
  per pass with `--passes`). Shrinking the canvas (`api_profile.mjs --size 480x270`) is *not* a check here: shadows, SDFGI and
  fog do not scale with it. Native per-section GPU time for comparison: run the project with `--gpu-profile`.
- **`api_profile.mjs` inflates the frame** (it wraps every GPU method with two `performance.now()` calls); read its fps as a
  lower bound and take fps from `gpu_time.mjs` or the engine's `godotWebGPUFrameStats` instead.
- **Sub-0.06 ms passes move ±0.01 ms between launches of the same export, and about 1 launch in 10 runs in a slow GPU
  state (half the fps)** (2026-10-25, §4.36's GPU A/B). Use at least 3 launches with the order rotated, read medians, and
  confirm a per-pass gap by swapping the WGSL (`gpu_time.mjs --replace-wgsl`) before blaming a converter or a change.
- **Stale exports**: `scene_smoketest/run_scenes.mjs --export-only` needs `GODOT_EDITOR_BIN` and `GODOT_TEMPLATE_ZIP` or it exports
  nothing (default is the macOS editor); an editor without `webgpu=yes` bakes nothing; a commit after the build mismatches the
  baked cache. Check `baked > 0` and the template/editor version before trusting a number.

## 4. Archive — completed work

One entry per phase or task arc, kept for cross-reference only. Round-by-round investigation trails,
exact diffs and every superseded hypothesis are in git history (`git log`, and §5's retired docs).
**All of this is `DONE` unless the entry says otherwise.**

**Legacy numbers**: source comments and test scripts still cite the old `Phase N` / `Task N.M`
identifiers. Each entry below names the phases and tasks it absorbed, so grepping this file for
`Task 12` or `Phase 15` still lands in the right place.

### 4.1 Phases 0-6 — the original sprint (March 2026)

`webgpu` SCons option, driver registration, the four driver classes, Emscripten bootstrapping,
buffers/textures/samplers/bind groups/pipelines, 2D then 3D rendering, compute, timestamp queries,
push-constant emulation, export preset + HTML shell, build/browser/benchmark testing, then the
Mobile-renderer feature gaps (GPU skinning, SubViewport+SSAO+Bloom, `texture_get_data()` async
readback, reflection probes, omni shadow cubemaps). End state: ~6,800 lines across 9 files in
`drivers/webgpu/`, 278 shaders converting, 0 Tint failures, 0 Dawn validation errors. The
architectural decisions this produced — push constants at group(3)/binding(120), subpass flattening,
barriers as no-ops, shadow-copy buffer mapping, no 3-component formats, 256-byte copy alignment, max
4 bind groups, device import via `WebGPU.importJsDevice()` — are documented in `CLAUDE.md` and
`drivers/webgpu/README.md`, which are the live copies.

### 4.2 Phase 7 — audit and hardening (April 2026)

22 audited items: fence signaling via GPU callback, `buffer_unmap` flush, dynamic buffer offsets,
format-aware bytes-per-pixel, 16-bit Unorm/Snorm remapping (a real bit-pattern bug, not just an
interpretation change), swap-chain format from the device's preference, sampler filter validation,
depth-fallback substitution (user-visible via Bokeh DOF), the `textureGather` hole in the
reclassification heuristic, sync-scope splitting, push-constant ring overflow, debug labels. MSAA
resolve, indirect-draw count buffers, swap-chain `LoadOp`, `command_render_clear_attachments` were
each investigated and closed as inactive/intentional/dead. Three entries survive as §2.13.

#### The last three TODOs — closed 2026-10-14 (old §2.13)

- **7.15, `WGUniformSet::temp_views`** — no leak. `uniform_set_free()` releases every entry, along
  with the `rw_shadow_*` views and textures, every cached rebind group and `handle`. Growth is bounded
  per set rather than per frame: the only place that appends during rendering is
  `_get_compatible_bind_group()`, reached once per (uniform set, target layout) pair because
  `rebind_cache` short-circuits every later call.
- **7.17, `WGPipelineWrapper::specialized_modules`** — no leak. There is one `pipeline_free()` for
  both pipeline kinds and it releases `specialized_modules[0..5]` after the handles.
- **7.8, `buffer_map()` returning a shadow copy** — inherent, and now documented where it bites.
  The *upload* direction is correct by design and already described on
  `API_TRAIT_BUFFER_MAP_RETURNS_SHADOW_COPY`. The *readback* direction has two mechanisms, and
  neither can block: `buffer_get_data_direct()` keeps a persistent staging buffer per buffer, hands
  back the previous frame's bytes and returns **false** on a first call (so it never passes off zeros
  as data); `buffer_map()`'s readback branch pumps `wgpuInstanceProcessEvents()`, then checks
  `wgpuBufferGetMapState()` directly and copies `getConstMappedRange()` if the JS side mapped even
  though the C callback had not fired. Only when both fail does it return the zero-filled shadow —
  which cannot be avoided, since completing a `wgpuBufferMapAsync()` needs a turn of the JS event loop
  that a synchronous `RenderingDevice` call in a single-threaded build cannot yield. **The contract is
  therefore: a readback is at best one frame old, and a first readback can come back empty.** That
  branch now says so once (`WARN_PRINT_ONCE`) instead of letting a caller chase the zeros.
  No per-frame render path depends on it: the engine's readback callers are all script- or
  editor-facing — `mesh_storage.cpp`'s surface and multimesh reads, `gi.cpp`'s VoxelGI octree save,
  `particles_storage.cpp`'s AABB. Those *are* observable to a game that calls
  `ArrayMesh.surface_get_arrays()` or `MultiMesh.get_buffer()` on the first frame, which is the
  honest limitation to document rather than fix.

### 4.3 Phase 8 — Godot 4.7.2 upstream sync (September 2026)

Merge, not rebase (`14d2857f66` carries the conflict-resolution writeup). Driver adapted to the
interface changes. The sync's one real regression was `scene_forward_mobile.glsl`'s Tint crash — a
genuine Tint bug in `ConvertUserCall`'s texture-parameter propagation, triggered by 4.7's new LTC
area lights, worked around with a 13th SPIR-V pass rather than patching vendored Tint. Also fixed
here: `writeonly` storage buffers failing conversion, `inline_opaque_functions` silently no-op'ing on
real engine SPIR-V, pipeline-layout creation failing for `SceneForwardMobileShaderRD` (all 3D draws
silently dropped), depth textures bound with a `Filtering` sampler (both the layout and the runtime
bind), and a `DirectionalLights` buffer-size mismatch root-caused to `flatten_binding_arrays()` after
a spec-constant red herring.

### 4.4 Phase 9 — Forward+ (Clustered) support (September 2026)

The largest arc in the project: 46 rounds against real hardware and the user's own project. Fixed,
among dozens: `cluster_render.glsl`'s two conversion crashes, subgroup support for the precompiler,
Sint/Uint/Float sample-type mismatches across SDFGI/VoxelGI/GI shaders, cluster-building and
framebuffer-attachment bugs, SSAO/SSIL via a pre-DCE SPIR-V capture pass, volumetric fog, the SDFGI
packed-format reinterpretation trick, read_write-storage splitting for the legacy spec-constant path,
cross-variant bind-group reuse, SDF voxelization pipelines with no attachment on either side, FSR1
and FSR2 end to end (formatless storage images, an FP16 Tint crash, a compute-pass intra-pass
sync-scope violation, a second unaudited `imageAtomicMin/Max` site, a `dilatedDepth` naming false
positive, and the render target's deliberate lack of `TEXTURE_USAGE_STORAGE_BIT`).

**The headline finding — the SDFGI brightness runaway (Rounds 29-46)**: `rgba16sint`/`rgba32sint`
lightprobe textures need `readonly-and-readwrite-storage-textures`, absent on the test adapter, so the
driver splits them into a write-only storage side plus a shadow texture refreshed by an explicit
per-bind GPU copy. **That copy's extent used `depth` (always 1 for a 2D-array texture) instead of
`layers`**, so only layer 0 of 30 was ever refreshed and every other layer read stale/zero data
forever. Confirmed by a synthetic repro reproducing the exact growth shape, then by a 300 s real-GPU
capture of the scene that had run away to white in every prior round: zero brightness change, zero
errors. Rounds 30-45 are a catalog of disproved hypotheses (clamping, quantization damping, cache
staleness, texture identity, Dawn hazard races, push-constant delivery, parallelism) — do not
re-litigate them. Also closed here: Task 9.15 moved vertex-stage read_write storage demotion to a
Tint IR transform, made the format-remap text passes memcpy-safe and removed a dead WGSL-text pass.

### 4.5 Phase 10 — sweep, and the two toolchain tasks (September-October 2026)

- **Task 11 — the non-dlink export abort**: root-caused to a 4-byte heap overflow inside Emscripten
  4.0.11's vendored `emdawnwebgpu` (`new WGPUInstanceImpl(desc)`: 32 bytes allocated, 36
  zero-initialized), found with a clean ASan report after eight rounds of it masquerading as a Godot
  bug — the overflow lands on whatever allocation sits next, so it surfaced as four different
  symptoms depending on binary layout, and `variant/extensions_support` was never the real variable.
  One real independent bug was found and kept along the way: the device was imported via
  `importJsDevice()` with no `parentPtr`, so its async events were tracked under the wrong instance.
  **Closed 2026-10-14 on Emscripten 6.0.9 — the abort is gone**, on two kinds of evidence, because
  a clean load alone would not have been enough (the overflow was heap-layout dependent, so "it
  works today" could just mean the stray bytes landed harmlessly again):

  1. **The mechanism no longer exists in the vendored port.** 6.0.9's `webgpu.cpp` has
     `wgpuCreateInstance()` call a static factory — `WGPUInstanceImpl::Create(descriptor)` →
     `AcquireRef(new WGPUInstanceImpl())` — whose constructor takes **no descriptor**, has an
     explicit member-init list (`EventSource(GetNextInstanceId())`) and a real body; the
     descriptor's state is *assigned* into the already-constructed object afterwards. The
     descriptor-taking constructor whose compiler-generated whole-object zero-init wrote 36 bytes
     into a 32-byte allocation is gone, and `grep "new WGPUInstanceImpl"` finds only that one site.
  2. **Two differently laid out non-dlink binaries load clean**, which is the axis that used to
     decide whether the stray write hurt: `webgpu_tests/test_project` exported with
     `variant/extensions_support=false` as **release** *and* as **debug** (the debug template is
     where a different stale-layout crash had reappeared before). Both print the banner,
     `WebGPU 1.0 - Forward+`, `Emscripten 6.0.9, single-threaded, no GDExtension support`, build the
     full shader-coverage scene and render. Zero aborts, zero `unreachable`, zero dlmalloc or
     `EventManager` assertions, zero WebGPU errors — the only `ERROR:` in either log is the
     fixture's own `look_at` misuse, which the dlink control shows too. The dlink control
     (`extensions_support=true`, with its 47 MB `index.side.wasm`) is identical but for the
     `GDExtension support` line, so the old "works with extensions, aborts without" correlation is
     simply absent rather than inverted.

  **Not repeated**: the ASan build that originally produced the diagnosis. An ASan web template
  relink costs far more than this result is worth now that the upstream code shape is gone, and ASan
  changes the very heap layout the bug depended on. The Round 6 `parentPtr` fix stays in the tree
  (`rendering_context_driver_webgpu.cpp` passes `instance` to `importJsDevice()`), still correct and
  still unrelated to this crash.
- **Task 12 — threads**: `threads=yes dlink_enabled=no` (the common case) root-caused and fixed
  (`6893725f61`). `threads=yes dlink_enabled=yes` was then made to boot clean via the two Emscripten
  toolchain patches in `misc/emsdk_patches/` — `0001` an `ASM_CONSTS` initialization race in the
  dylink+pthread glue (a hard hang for as long as the configuration existed), `0002`
  `-sEXPORT_ALL` re-exporting the `HEAP*` views eagerly (~11 `growMemViews` TypeErrors per load,
  silently dropping later `Module` exports on worker threads). Both are dlink-only and both need a
  pthread worker to fire. **Bug #1's structural consequence is permanent and worth knowing: WebGPU
  handles live in a per-thread JS table, so handle creation stays on the thread that imported the
  device.** Whether threading is worth shipping is §2.5.
- Tasks 10.1-10.4 (the sweep itself) were overtaken by `local_ci.sh` running green end to end
  (Task 42) and by the 4.8 interface audit; what is genuinely left of them is §2.13 and §2.14.

### 4.6 Phase 11 — Box3D as the default 3D physics engine (September 2026)

`modules/box3d_physics/` implements `PhysicsServer3D` on official `erincatto/box3d` (`9e5a4cd`, MIT,
C17, vendored in `thirdparty/box3d`), registered as `"Box3D Physics"` with default priority. Chosen
over the `Stink-O/box3d-godot` GDExtension because that one only exposes its own nodes and cannot
back `RigidBody3D`/`CharacterBody3D`/`Area3D`. Validated by headless GDScript scenes run against
Box3D, Jolt and GodotPhysics3D with matching results. The structural differences that trip people up
(per-(object, shape) instances with baked transforms, `_custom_filter()` making every layer/mask and
area-monitoring decision because Box3D's filter bits are symmetric, areas as sensor shapes on a
static body, a hand-written narrow phase for `body_test_motion`, single-threaded on web) are in
`CLAUDE.md`. Remainders are §2.23.

### 4.7 Phase 12 — light projectors and positional shadows (September 2026)

Verified against native Vulkan: mean absolute difference 0.007 with no pixel above 16 on the user's
project; spot-only and omni-only scenes 0.000 / 0.016. No defect found.

### 4.8 Phase 13 — CI editor builds ship the WebGPU shader baker (September 2026)

CI editors were built without `webgpu=yes`, so they shipped no `WEBGPU_SHADER_BAKER_ENABLED` and no
`tint_convert_cli`; `tint_convert_cli` also had no Windows port. Both fixed — the tool now builds on
Windows via Git Bash + LLVM `clang++`, objects go in a per-OS `.build/<os>/` so a checkout shared
with WSL cannot mix ELF and COFF, and the CI editor artifacts ship the binary next to the editor
(inside `Contents/MacOS` on macOS).

### 4.9 Phase 14 — specialization constants as pipeline constants (September 2026)

`freeze_spec_constant_ops()` is now conditional: anything
`spirv_preprocess::spec_constants_overridable()` accepts survives into WGSL as `@id(N) override` and
is set at pipeline creation through WebGPU pipeline constants — one base module per shader, no runtime
re-conversion. Rejected cases (non-scalar constants, an `OpSpecConstantOp` Tint cannot lower,
spec-constant array sizes/composites/workgroup sizes) are frozen to defaults and go through the
legacy re-patch-and-re-convert path. All-or-nothing per shader. Premise was verified empirically
first: across all 196 real engine shader variants, **0** were unsupportable. Also in this phase: the
13 shaders missing from `SHADER_REGISTRY` added (sweep 196 → 274 modules, 0 failures), two wrong
stage declarations that had silently disabled two more, and an `EM_ASM` body split by the
preprocessor (now caught by a pre-commit hook needing no Emscripten).

### 4.10 Tasks 13, 29-37 — export-time shader baking, end to end

One visible bake warning chased into seven defects. Final state
`{baked: 392, precompiled: 1, cached: 1, translated: 0, specialized: 0}` on a cold start with storage
cleared — **zero runtime shader translation**.

| task | what it was |
|---|---|
| 29 | `BuiltIn ViewIndex` from a multiview variant the XR-off editor still compiled; fixed in the baker *and* at source (`vrs.cpp` left `VRS_RG_MULTIVIEW` on) |
| 30 | `translated` conflated a real baking gap with spec-constant re-conversion; split into `translated`/`specialized`, proving all 37 were real |
| 31 | The baker compiled with the **editor's** device capabilities, not the export target's → 37 → 16 |
| 32 | Materials embedded in scenes are not enumerated by the baker (real gap, not the cause of the 16) |
| 33 | A stale `user://` shader cache permanently shadowed the export's baked cache, once defeating baking entirely (`baked: 0, translated: 193`); `res://` now searched first |
| 34 | A pathless `StandardMaterial3D` created during the first frame, reachable by no exporter walk; fixed by baking every live version instead |
| 35 | Monochrome glyph atlases expanded LA8→RGBA8 on every upload; now rasterized RGBA8 directly. Its follow-up: the fix made every glyph match the colour-glyph test, so **all text rendered white** until a per-glyph `color_glyph` flag replaced atlas-format sniffing in both text servers |
| 36 | Editor and template built from different commits → different `GODOT_VERSION_HASH` → the whole baked cache silently unreachable. Now warns at runtime |
| 37 | `RGB8 not supported by hardware` described an unconditional expansion as a per-GPU shortfall |

Task 13's own Phases 1-4 (a record-then-bake subsystem for spec-constant variants) were **removed**
as dead weight once Phase 14 landed, together with the bug surface they kept producing. The one thing
export-time baking structurally cannot cover is a shader that does not exist at export time
(`Shader.new()` + `set_code()`); the right answer there is persistence, not precomputation — a
per-viewer IndexedDB WGSL cache keyed by the SPIR-V hash `_spv_to_wgsl_cached()` already computes.

### 4.11 Task 14 — load time and the loading bar

Cold stall on the user's project went **~9.2 s → ~1.0 s**, mostly from §4.10 plus turning
`verbose_stdout` off (~1.5 s) and glyph-atlas upload coalescing in `frame_pre_draw` (~800 ms, 41%;
66 → 31 MB uploaded before the first frame). Separately, the startup no longer runs as one unbroken
`callMain()`: it is split into frame-sized steps with per-phase progress pushed to JS, so the loading
bar covers the engine's own startup instead of stopping at "download complete" (~48 ms cost). Before
that, `godot-before-callmain` + two `requestAnimationFrame`s and a "Starting engine..." notice made
the freeze legible. Remaining wall-clock work is §2.4.

### 4.12 Task 24 — anti-aliasing (MSAA 2D/3D, TAA, DOF)

Eleven distinct bugs from user reports of black screens, error spam and silently corrupted DOF, over
five rounds against the real project: MSAA sample-count clamping (WebGPU accepts only 1 or 4),
`copy.glsl`'s destination-format gap for TAA's 2-channel velocity buffer, `ClusterDebugShaderRD`
building an unused bind group every frame, a completely unimplemented `command_resolve_texture()`
(MSAA was never resolved at all), a depth/float sample-type fallback feeding fake zero depth to DOF,
an unclamped core sample-count table, an SDFGI/VoxelGI texture-array binding split that never worked
during export-time baking, a multisampled-depth reclassification gap, a regressed older depth/float
check, a missing Forward+ DOF-depth guard Forward Mobile already had, and a format-converting
compute-shader resolve fallback for the one case WebGPU's native resolve structurally cannot handle.
Commits `79ce22b0c1`, `0979f8238a`, `96e8ef446a` + a final round-5 fix.

#### The `_depth_alias` mechanism — removed 2026-10-14 (old §2.12)

Tint splits a mixed-usage depth texture into two globals — Depth at binding B and a Float alias at
B+1, named `*_depth_alias` — and the driver used to scan the generated WGSL for that name and
synthesize the extra BGL entry, merged-layout entry and bind group entry it implies, in five places.
All of it was dead, for a structural reason: Tint only emits the alias for an *ambiguous* (`depth = 2`)
`OpTypeImage`, and `spirv_preprocess.cpp:2008` rewrites every `depth = 2` image to `depth = 1` before
Tint ever sees the module, so one `texture_depth_2d` variable now serves both sampling modes. Checked
as well as argued: **0** occurrences of `_depth_alias` in `wgsl_precompiled.gen.h` and **0** across
all 216 baked shader containers of a real project's export. It had already cost one investigation
round by looking live, so a comment at the old declaration site records why it cannot come back.

The second half of that item — adding the reverse depth/float fallback to the plain
`UNIFORM_TYPE_TEXTURE` branch, which its combined-sampler sibling has — was **deliberately not done**.
It is robustness rather than a fix, and §4.14 is the record of it having actively hidden a hard Dawn
error behind quietly wrong pixels. Leaving the loud failure is the better default; if it is ever
added it should be behind something that still reports.

Regression check, after: smoketest 21/21 (swiftshader — the real GPU was unavailable, see §2.27),
driver unit 370/370, shader corpus 14/14, preprocessing 238/238, and a `dev_mode=yes` web build clean.

### 4.13 Tasks 38, 41 — Firefox

Two independent blockers, both fixed and verified in both browsers. **38**: `enable subgroups` is
Chrome-only, so Firefox rendered **no 3D at all** (UI only) — fixed by
`spirv_preprocess::lower_subgroup_ops()`. **41**: `rgb10a2unorm` storage textures fail on Firefox —
fixed by promoting the three `texture-formats-tier2` storage formats when the device lacks the
feature (`rgb10a2unorm` → `rgba16float`), taking Octmap from 14 validation errors to 0.

### 4.14 Tasks 42-45 — the test tiers became real

- **42**: first full green `local_ci.sh` (12 passed, 0 failed, 1 skipped), after two harness fixes.
- **43**: `create_local_rendering_device()` returned null with no diagnostic. Fixed — and the
  diagnostic immediately disproved this task's own premise (the scene was running OpenGL).
- **44**: **ten demo scenes were testing OpenGL, not WebGPU**, silently, because Godot picks the web
  driver from `rendering_method.web`. Fixing the harness exposed four real failures, all fixed: canvas
  SDF `R16_SNORM` → `R16_SFLOAT` (735 errors → 0), `depth_buffer` (group 1) excluded from
  `_reclassify_single_component_depth_textures()` (44 errors each → 0, `5f4b63c136`), the depth
  back-copy switched to `copy_depth_to_rect()`'s `r32f` variant instead of an `rgba16f`-declared one
  (49 errors → 0), and block-aligned compressed texture-to-texture copies (BPTC no longer black).
  Both bugs behind Godot's `DEPTH_TEXTURE` (proximity fade / refraction) were independent.
- **45**: `Gradient{Texture1D,Texture2D}::get_image()` regenerates instead of reading the GPU back,
  taking `demo_compute_heightmap` from the tier's last skip to a pass. The driver's readback cache is
  one-shot consume-and-requeue, so a *single* call can never see its own result.
- Also here: the CI scene-smoketest job had been testing nothing for as long as it existed; the
  octmap filter needs six storage images where WebGPU guarantees four (found by reproducing a
  CI-only failure locally with `CI=1`); and CI was shipping an unbaked template and saying so 274
  times.

### 4.15 Task 46 — performance audit

**Landed**: the shader container no longer ships SPIR-V it will not use — `index.pck` 101.5 MB →
57.1 MB (−43.7%) with every behavioral counter byte-identical; SPIR-V is 66.8% of a baked container's
payload. A dead nested scan was removed from `command_bind_render_uniform_sets()`. The per-frame half
is **instrumented, not fixed** (`godotWebGPUFrameStats`): everything else found in the per-draw path
was already handled. Measured on swiftshader, which is how §4.17's Forward-Mobile-vs-Forward+ trap
was found and corrected — an export warning written on that wrong conclusion was removed again.
Open descendants: §2.6, §2.20, §2.22, §2.27.

#### §2.3 — a dlink export under network throttling (closed 2026-10-14)

Every number this project had ever taken was over loopback, where a 40 MB download costs ~130 ms.
`profile_phases.mjs` now has `--throttle` (CDP `Network.emulateNetworkConditions`, named profiles)
and `--compress` (`Content-Encoding: gzip`, which every real host does and none of this suite's
servers did), so the question could finally be asked. All figures: `godot-demo-projects/3d/platformer`,
real GPU, cold profile, Chrome, time from navigation to `before-callmain` (the load phase — fetch +
instantiate + shell, excluding the engine's own CPU stall). This table is the **unbaked** export
(pck 1.09 MB) on purpose: with the wasm as ~97% of the payload it isolates the dlink term. The
representative baked configuration is the last bullet below, and §2.27.

| link | non-dlink | dlink | penalty |
|---|---|---|---|
| loopback | 0.51 s | 0.50 s | none |
| fibre, 100 Mbit | 4.05 s | 4.65 s | +0.60 s (+15%) |
| cable, 20 Mbit | 17.9 s | 21.3 s | +3.4 s (+19%) |
| dsl, 5 Mbit | 70.8 s | 83.8 s | +13.0 s (+18%) |
| cable + gzip | 6.17 s | 6.71 s | +0.54 s (+9%) |
| dsl + gzip | 21.8 s | 23.5 s | +1.6 s (+7%) |

**The premise was wrong.** The fear was ~20 s of download that could not overlap compilation. The
non-streaming `WebAssembly.instantiate` of the 45.3 MB side module measures **46-52 ms at every one of
those bandwidths** — constant, because it is compile time, and Chrome's baseline compile of 45 MB is
that fast. Nothing else is serialized: the penalty tracks bytes exactly. dlink's payload is 49.8 MB
against non-dlink's 42.0 MB (+19%), because dlink moves the whole engine into the side module
(`index.wasm` 40.7 MB → 1.3 MB, `index.side.wasm` 45.3 MB) and triples `index.js` (0.28 → 2.1 MB).
Gzip shrinks the *relative* penalty too, since the extra JS compresses well.

**The preload mitigation is refuted, not deferred.** A `<link rel=preload>` or early `fetch()` of
`.side.wasm` can only recover time when the connection would otherwise be idle, and it never is: the
side module's fetch begins within ~150 ms of the main module's finishing, and in a realistic export
the pck is downloading across that whole window anyway. Overlapping two downloads on one
bandwidth-limited link does not make them finish sooner. Do not implement it.

**What the numbers do say:**

- **Serve compressed.** 3.5× end to end (81.2 s → 23.5 s on the representative baked export at
  20 Mbit); brotli is another ~17% over gzip (9.5 vs 11.9 MB for the side module). Nothing in this
  repo mentioned it — see §2.22.
- **dlink also costs ~12% engine CPU**, not just bytes: identical work through the side module's PLT
  measures `containerCreateMs` 7419 ms vs 6615 ms non-dlink, same project, same build, both
  translating the same 355 shaders. Two non-dlink runs agreed to within 1% (6615 / 6560 ms), so the
  gap is well clear of the noise. Previously unmeasured, and the stronger reason to prefer a
  non-dlink export where GDExtension is not needed.
- On the **representative** configuration (baked, editor and template from the same commit, 20 Mbit)
  dlink's share falls to **+3.3 s raw / +0.7 s gzipped (+3-4%)**, because §2.27's 150 MB pck dwarfs it.

The whole exercise also produced §2.27, which is a much larger effect than the one it set out to
measure.

### 4.16 The Tint / SPIR-V arc

*Superseded 2026-10-25: Tint was replaced by SPIRV-Cross and deleted (§4.36). This entry is the history of the Tint
years; its pass and patch numbers refer to that tree (tag `pre-SPIR-V-Cross`).*

The pipeline was `GLSL → SPIR-V (glslang) → 12 binary-rewriting passes → WGSL (Tint, linked in) → GPU`,
with Tint vendored under `thirdparty/tint` (its patches were listed in `thirdparty/README.md`).

- **naga → Tint (May 2026)**: replaced the 1.1 MB prebuilt Rust/WASM naga blob with Google's Tint
  (C++20, BSD-3) specifically to make an upstream PR possible — Godot has no Rust dependency and no
  precedent for prebuilt WASM in-tree. The 6,500-line Rust preprocessor became
  `spirv_preprocess.cpp`; `naga_to_tint_debugging.md` (§5) mapped each regression back to the naga
  pattern that had already solved it.
- **Fixes with their own tasks**: 15 (`particles.glsl` ICE from the Dawn `db49a549` → `b975919d`
  resync — vendored Tint patch 0010), 16 (export fails to load when shader baking is on — patch
  0011), 17 (`rewrite_copy_logical`'s naive opcode swap produced invalid SPIR-V for real UBO struct
  copies), 18 (SPIR-V 1.4+'s `OpEntryPoint` interface-list rule triggering a real Tint reader bug),
  21 (the SPIR-V 1.3 downgrade turned out unnecessary — fixed properly in vendored Tint, removing two
  preprocessing passes), 23 (that removal regressed `eliminate_dead_resources()`, reintroducing "18
  samplers in the Vertex stage"), 19 (two more instances of Task 8.7's blanket-visibility bug on
  depth-alias entries).
- **The 154 export-time bake warnings**: Class 1 (100×, `OpSelect` condition/result vector-size
  mismatch) fixed by a new `broadcast_select_scalar_condition()` pass; Classes 2-3 (`ZeroExtend`
  needing SPIR-V 1.4, and generic Tint crashes) were root-caused in Tasks 22 and 26 to **WGSL
  language limits on variants WebGPU never selects** — all benign, bake-time only, never dispatched
  at runtime, and the baker now skips them up front instead of forking a doomed child. Task 29 closed
  the last one. Note Class 1's entry once claimed a fix that was not in the working tree — the code
  was written twice.

### 4.17 Phase 15 — Godot 4.8 upstream sync (October 2026)

`webgpu-4.7.2` + upstream `e7cfa294a0b81bed7986be04a848cc1832a3f083`: 2,794 commits, 3,872 files, 109
conflicts. Box3D rewritten for 4.8's split physics headers; driver adapted and interface-audited.
Every container-runnable tier green (§1), `warnings=extra werror=yes` clean across 9 web and 600
native objects.

**Then the first real-hardware run found two black-3D regressions the whole green suite could not
see** (because swiftshader runs Forward Mobile, SSAO/SSIL are Forward+-only, and SDFGI voxelization
lives in `scene_forward_clustered.glsl`): SSAO's `RB_FINAL` kept this fork's old `RGBA8_UNORM`
override after upstream moved both `ssao_interleave.glsl` and the texture to `r8` — and one invalid
bind group invalidates the whole command buffer, so `Queue.Submit` dropped every 3D frame — and
4.8's new `STORAGE_ATOMIC` usage bit on SDFGI's geometry-facing texture was requested unconditionally
although WebGPU has no texture atomics. Both fixed and verified on an RTX 4080 SUPER: Chrome 0 errors
(was 6,658+), Firefox 0 real errors, both reporting Forward+, matching a native-Vulkan capture.
Trap 11 in §3 is this phase's main lesson.

### 4.18 Phase 16 — the git lineage repair (October 2026)

A past history rewrite had detached this fork from upstream entirely (`merge-base` landed in November
2015), so an upstream sync wanted to replay ~85,000 commits instead of ~2,800. Diagnosed, then
**repaired** from the `backup/pre-claude-author-strip` branches: `webgpu-4.8`, `webgpu-4.7.2` and
`main` are grafted back onto upstream's real lineage with every tip tree byte-identical and no commit
invented (force-pushed 2026-10-05). Nothing special is needed any more — merge upstream normally. The
`git replace` refs and the superseded regeneration script are in this phase's history. **Do not
delete `backup/pre-lineage-repair/*` or `backup/pre-claude-author-strip*`.**

### 4.19 Phase 17 — the Forward+ feature matrix (October 2026)

`webgpu_tests/forward_plus/`: 64 features, each rendered off and on, asserting the frame changed by a
calibrated amount with no driver errors. Thresholds are one third of the delta measured on **native
Vulkan** through the same fixture (`-- --fp-matrix=DIR`) — never on WebGPU, which would bake in
whatever the port currently does, bugs included. 10 further features are listed in `features.mjs`'s
`UNCOVERED` with a reason each. Built because §4.17 shipped SSAO and SDFGI broken through a fully
green suite. **64/64 on real GPU** since 2026-10-14: WebGPU and native Vulkan agree closely across
the whole Forward+ surface.

Four bugs found on its first run: VoxelGI's `R8_UINT`/`R8_SINT`/`R8G8_UINT`/`R8G8_SINT` missing from
`texture_get_usages_supported_by_format()` although `_promote_storage_format()` always handled them
(`87d32a26c2` — same omission class as the `R16_UINT` pair fixed earlier for SDFGI); two *fixture*
bugs where the subject was built at runtime inside the measurement window (`VoxelGI.bake()`, and
`generate_lods()` which returns nothing on web — §2.15), now committed as baked assets; and the
uniform-set/pipeline-layout mismatch whose fix also explained proximity fade being ~10× too weak
(0.320 → 3.234 against Vulkan's 3.265 — one root cause, two symptoms).

#### The residual (old §2.1) — closed 2026-10-14, **64/64**

The last 2 errors per run were a **cache keyed on a pointer it does not own.**
`WGUniformSet::rebind_cache` maps a *target* shader's `WGPUBindGroupLayout` → a bind group rebuilt
against that layout, so one uniform set can be bound under any variant's pipeline. Those keys belong
to shaders the uniform set neither owns nor came from: RenderingDevice makes a uniform set depend
only on the shader it was **created** from (`rendering_device.cpp:5281`), while the scene's set 0 and
set 1 live for the whole frame and get adapted to dozens of variants. `shader_free()` releases the
dying shader's layouts; emdawnwebgpu's handles are heap objects, so the next
`wgpuDeviceCreateBindGroupLayout()` can land on the freed address — and from then on the cache hit on
the *recycled* pointer and bound a group built for the dead layout. Pointer equality said it matched;
Dawn compared contents and rejected the command buffer, naming the dead shader's variant
(`bgl:…:2:set0` under `plyt:…:18`). Fix: `_purge_rebind_caches_for_shader()`, called from
`shader_free()` before the layouts go, walks a new `live_uniform_sets` registry and erases (and
releases) every entry keyed on them.

That explains every property that made it undiagnosable: it needs shader churn, so it only appeared
partway through a long matrix run and never in a single-feature repro; it drifted between features
because which variant dies when is incidental; and the driver's own bookkeeping was *self-consistent*
at the failing draw, so no amount of checking the bind path could see it.

Found by elimination, with two instruments built along the way and kept:

- **`_reconcile_bind_groups()`**, called from every draw and dispatch. WebGPU validates every group
  in the pipeline layout, including ones the shader never reads, where Vulkan only cares about the
  sets a pipeline accesses — and RenderingDevice's rebinding is Vulkan-shaped, re-emitting binds only
  for sets the new pipeline declares a non-zero format for. This compares each slot against
  `WGShader::pipeline_group_layouts` (the exact array given to `wgpuDeviceCreatePipelineLayout`) and
  re-adapts a mismatching slot from the uniform set it came from, warning (budget 8) when it cannot.
  One pointer compare per group on the hot path. It has **never fired** — including across two full
  matrix passes with no reload — which is what proved the bind path was not the problem and sent the
  search to the cache. Kept as a canary and because that validation-scope difference is structural.
- **`WGCommandBuffer::BoundGroupState::{layout, source_set}`** — each slot now records the layout its
  group was built for and the set it came from, which is what makes the above decidable.

Two latent bugs fixed in passing: `command_next_render_subpass()` began a fresh encoder without
calling `invalidate_bind_groups()`, so the redundancy cache could skip a bind the new pass needed;
and neither it nor `command_begin_render_pass()` cleared `last_bound_state`, which the mid-pass
restart replays. `reset_bound_group_state()` now runs wherever a fresh encoder makes the slots
genuinely empty (render pass begin, next subpass, compute pass begin).

Verification, all on the real GPU (RTX 4080 SUPER, Chrome): matrix **64/64 PASS**; two full
back-to-back matrix passes with no page reload (256 configuration changes, the maximum shader churn
available) **0 errors, 0 reconcile warnings**; scene smoketest 21/21 on Chrome *and* Firefox;
resource lifecycle 6/6; screenshot comparison 8/8 (0.000% cross-browser); shader corpus 14/14;
driver unit 370/370; preprocessing 238/238; and a `dev_mode=yes` (`warnings=extra werror=yes`) web
build clean, since none of the above would catch a CI-fatal warning (§3).

### 4.20 Phase 18 — WebRTC web/native parity (October 2026) — CLOSED

Ten findings, nine resolved. Coverage: nine fixtures identical to native across three export variants
and two browsers (54 clean diffs), `MultiplayerAPI`/`@rpc`, a congestion tier under real bandwidth
pressure (netem in an unprivileged namespace, no root), and a validated two-machine run. Both tiers
run from `local_ci.sh`; the parity tier also gates GitHub CI. There was no feature to add —
`modules/webrtc/` is platform-agnostic and the web side is a JS library binding.

| finding | resolution |
|---|---|
| F1 | `get_max_packet_size()` 1200 (web) vs 16384 (native) — fixed; `put_packet()` now reports send failures |
| F2 | Buffered packets destroyed on remote close (web only) — fixed, `close()` keeps the queue |
| F3 | Silent drop vs unbounded queue on backpressure — fixed (buffer grows); a single burst had lost 435 of 500 packets |
| F4, F5, F7 | `is_negotiated` default; unset reliability values (-1 vs 0); pre-open `get_id()` (65535 vs 0) — all fixed; all three trace to trap 13 in §3 |
| F6 | Upstream `webrtc-native` aborts the editor on first project scan (exit 134) — **upstream's**; worked around in both tiers' `export.sh` |
| F8 | A `maxPacketLifetime` typo meant both unreliable transfer modes were **fully reliable on every platform**; found by code review, not by the harness |
| F9 | Web's data-channel send queue caps at ~16 MiB; native's does not — confirmed, benign, documented, no code change |
| F10 | After F8, web's unreliable modes delivered 13-25% where native delivered ~95%, with `put_packet()` returning `OK` every time. **Fixed**: default `unreliable_lifetime` 1 ms → 50 ms (`DEFAULT_UNRELIABLE_LIFETIME_MS`, hardcoded in two independent places before) |

F10 is worth understanding before touching this area: a 1 ms lifetime causes **no loss by itself**
(0% loss link: 100% on both platforms) because it governs *retransmission*, not first transmission.
It is therefore invisible on a loopback or clean LAN — including this project's own two-machine LAN
control — and hits players on wifi or mobile. `life50` is the control and agrees across platforms, so
the divergence was the lifetime *value*, not the mechanism. The fork now differs from stock Godot by
this one default (§2.7).

### 4.21 Toolchain upgrade, and the performance pre-history (May-September 2026)

**The emsdk upgrade (September 2026)**: Emscripten 4.0.11 → **6.0.9**, vendored Tint/Dawn resynced
`db49a549` → `b975919d` (10 patches, one of them a real upstream Tint bug found during the resync —
`atomics.cc`'s `TypeForAccess()` Matrix/Vector handling). Multi-draw-indirect adopted as a native
fast path; several magic-ordinal/JS-string feature-detection workarounds replaced with the named
enums the new headers provide. Verification was a full regression sweep plus the user's real project
over a 330 s real-GPU capture with the whole effect matrix stacked, and native Vulkan regression-free.
Two caveats recorded honestly at the time: the planned pre-upgrade baseline capture was never taken
(Phase 6 was diffed against written state instead), and the rollout phase's "open a PR" step was
never done, since this repo commits locally only. Four deferred feature-adoption items are §2.21.

**IPC and per-draw overhead (May 2026)** — the arc that made WebGPU competitive at all, and whose
conclusions should not be re-derived:

- The dominant cost was **not** Mojo IPC per call. Dawn's wire client already batches commands into a
  shared-memory ring buffer and flushes at sync points (`queue.submit()`, `encoder.finish()`); the
  real per-call cost is the **WASM→JS trampoline**, ~0.3 µs on an M3 Ultra, ~2.6 ms over ~8,700
  render-pass calls per frame — 11% of frame time on desktop and 20-45% on mobile.
- Landed: `firstInstance` dedup, instance batching, shadow-pass merging. Combined effect:
  per-frame WASM→JS crossings down **99.96%**, frame time from 3.25× native to near parity (the
  stress scene settled at ~1.44× before the last round of work).
- A full command-stream recorder (record commands into flat WASM memory, replay in one crossing) was
  designed in depth and **never implemented** — the per-call optimizations above closed enough of the
  gap first. The design is in git history (§5) if the remaining ~8,700 crossings ever matter again.

**Upload and creation stalls (May 2026)**, all fixed:

- **`buffer_unmap()` copied the entire 32 MB staging buffer to the GPU on every call**, even for 64
  written bytes — ~10 ms per texture upload, which is what made `Window.new()` 9-10 ms,
  `OptionButton.new()` 10-22 ms and `ImageTexture.create_from_image()` ~10 ms on web against <0.1 ms
  native. Dirty-range tracking fixed the whole class.
- **Batched `Texture2DArray` uploads** via opt-in API traits (all non-WebGPU drivers keep the existing
  transfer-worker path bit-identically): on a real project with ~1 GB of atlases, web
  `load_body_ms` 10,697 → 3,711 ms (−65%), `create_from_images` 2,877 → 240 ms (−92%), and ~300 MB
  less peak VRAM per tier.

**Abandoned approaches, recorded so they are not retried blind**: ASYNCIFY for synchronous buffer
readback (unacceptable cost; the experiment survives on branch `asyncify-experiment`), and a Tint Web
Worker for off-main-thread SPIR-V→WGSL translation (worked, never merged — then made obsolete by
build-time precompilation and Phase 14's `override` constants, which removed runtime conversion
entirely; branch `async_shader_pipeline`).

**Benchmarks (May 2026, M3 Ultra, macOS, Chrome 136)**: `webgpu_tests/benchmark/` has 7 scenes with
GDScript profilers printing `BENCHMARK_RESULT|…`. The Three.js comparison's fairness assessment is
worth keeping if those numbers are ever republished: Godot auto-batches same-texture `Sprite2D`
nodes while Three.js has no equivalent for individual sprites, so scene A is **not** a fair
comparison; the explicit-batching (`MultiMeshInstance2D` vs `InstancedMesh`), unique-material PBR and
instancing scenes are. All recorded numbers predate Forward+ and the current driver — §2.17.

### 4.22 build-linux.sh build-time trim (October 2026)

What a full `./build-linux.sh` run costs is 11 SCons invocations that share nothing by design (one
OBJSUFFIX per variant, ~3,000 objects each, ~33,000 compiles cold); nothing below changes that number
— only building fewer variants, or several at once in separate trees, would. What was trimmed:

- **The WGSL bake is no longer forced every run.** The script used to `rm wgsl_precompiled.gen.h`
  before the web loop because SCons could not see tint_convert_cli or vendored Tint as inputs.
  `drivers/webgpu/SCsub` now depends the table on a fingerprint `Value` (size+mtime of every
  `thirdparty/tint`, `spirv-tools`, `spirv-headers` and `tint_cli` source, pruning `.build/` and
  `__pycache__`; the hash of `bin/tint_convert_cli`; `glslangValidator --version`). Verified: a
  no-op web build is 9 s and does not bake; touching a vendored Tint file re-bakes (11 s including the
  host-tool rebuild) and settles on the next run. The guarantee now holds for a plain `scons` too.
  `--force-shader-rebake` keeps the old behavior.
- **The bake itself is parallel**: `wgsl_precompile.py` fans glslangValidator out over a thread pool and
  splits `tint_convert_cli --batch` (strictly serial internally, one fork per module) into one chunk
  per core. 33 s → 3.2 s for 274 modules, output byte-identical. `WGSL_PRECOMPILE_JOBS` caps it.
- **Prerequisites are checked before being redone**: dpkg for the apt list, `scons --version`, and the
  activated `emcc --version` against the pinned 6.0.9 — so `emsdk install` no longer re-extracts (and
  un-patches) the toolchain on every run.
- **Only the editor binary the run built is copied** to `builds/` and `editors/`; the old
  `bin/godot.linuxbsd.editor*` glob also shipped a 1.1 GB dev_build editor and a double-precision one.
- **mold/lld** for the native links, auto-detected. Measured honestly: with the default
  `debug_symbols=no` the editor links in ~6 s with ld.bfd and ~2 s with mold, so this is small here;
  it matters for a dev_build editor. (Earlier 4–8 min "link" times were contended recompiles.)
- **`--ccache` is opt-in**, via a symlink masquerade dir (not `c_compiler_launcher`, which would break
  `WhereIs("emcc")`-based toolchain detection; `emcc` itself is not shadowed for the same reason, so
  only em++ and native compiles are cached). SCons records the resolved compiler path as an implicit
  dependency of every object, so the first run with it — and the first run without it afterwards —
  recompiles every variant once (~4 min per variant on 24 cores). Worth it for branch switches and
  reverts, not for forward-only builds.
- `--skip-native-templates`, `--web-targets`/`--web-threads`/`--web-dlink` for iteration (default is
  still the full 8-variant matrix, with a warning when narrowed), and a per-step timing table.

Where the code lives: `build-linux.sh` (flags, `setup_ccache()`/`use_ccache_path()`, `NATIVE_LINKER_ARGS`,
`apt_packages_missing()`, the `scons --version`/`emcc --version` fast paths, `step()`/`print_timings`; `ccache` and
`mold` were added to `APT_PACKAGES`, so a run with install enabled now apt-installs both), `drivers/webgpu/SCsub`
(`_pipeline_fingerprint()`), `drivers/webgpu/wgsl_precompile.py` (`job_count()`, `_convert_spirv_chunk()`, the
two-phase main loop). No SConstruct, `detect.py` or engine change; a default run's outputs are identical to before,
except that `builds/` and `editors/` no longer receive the stray editor binaries.

Numbers behind the design (24 cores, 61 GB, Emscripten 6.0.9, SCons 4.10.1), as a baseline for the next
"is this slow?":

| What | Measured |
|---|---|
| Editor full compile (default `debug_symbols=no`, `-j24`) | 4 min 10 s wall, ~72 min CPU |
| Editor pure relink | ~6 s ld.bfd, ~2–3 s mold |
| Web variant, no-op `scons` read | 9–12 s |
| Web variant, one-file rebuild + link | 30–50 s non-dlink; ~3 min dlink (side-module link) |
| WGSL bake (274 stage-variants, 81 files, 189 unique, 56 validate-only) | 33 s serial → 3.2 s parallel |
| Fingerprint cost per `scons` read | ~31 ms |
| ccache coverage of a web variant | em++ objects only; ~520 C objects of ~2,950 stay uncached (`emcc` is not shadowed) |

Considered and not done: a parallel web matrix (needs per-variant `.sconsign` and a per-variant `.web_zip` staging
dir — an engine/SCsub change, declined), trimming the default matrix to four variants (declined; every export-preset
combination must have a template), `c_compiler_launcher` (changes the signed command string and, for `emcc`, the
toolchain detection), `fast_unsafe`/`implicit_cache` (correctness). Verification was stubbed dry-runs of the script
(scons/cp/zip/rm replaced, `--help`, filter validation, timing table, matrix order) plus real single-variant scons
checks of the fingerprint and the bake; the full `./build-linux.sh` has not been run end to end since the change.

Trap learned on the way (§3 trap 24): **two SCons runs in one tree poison each other** through
`modules/modules_enabled.gen.h`, whose content differs by `debug_features`. A release-variant probe
run alongside a template_debug build compiled `register_module_types.gen.*.o` against the wrong
header while sconsign recorded the right one; the release link then failed on
`initialize_objectdb_profiler_module` and the object looked up to date forever. Same mechanism as
Task 9.5 Round 15. Check `pgrep -ax scons` before building; if it happens, delete the objects of
every source that includes `modules_enabled.gen.h` compiled in the overlap window. The cleanup on 2026-10-16/17
removed 95 such objects (plus the five `register_module_types` ones) across 7 variants — 28 `linuxbsd.editor`,
3 `linuxbsd.editor.dev`, 6 `web.template_debug.nothreads`, 16 `web.template_debug.wasm32`,
10 `web.template_release.nothreads.dlink`, 16 `web.template_release.nothreads`, 16 `web.template_release.wasm32`.
The editor and the release/nothreads web variant were rebuilt and link cleanly; the other variants recompile their
handful on their next build. The editor objects were also rebuilt once with the plain `/usr/bin/g++` path after the
ccache experiments, so the tree matches the script's default (no `--ccache`).

### 4.23 Export-time bake: batched and parallel, a WGSL cache, a separate shader pack, cache prunes (October 2026)

Asked as "can the shader bake on export be cached reliably, to speed up re-exports and remote deploys during
development". The assessment found that the existing cache could not, and that caching was not even the main
lever; all of it was then built on 2026-10-17. Uncommitted at the time of writing.

**What an export cost before** (the user's `cameraSim` project, 24 cores): 27–31 s baked against 3.8 s unbaked, and
24 s of that was `tint_convert_cli` — 1,447 calls, 17 ms mean, run one at a time behind the fork-safety mutex in
`wgsl_bake_subprocess.cpp` (§3 trap 26). The baker's own cache under `.godot/exported/<hash>/shader_baker/` was
written every export and read only to append containers from earlier exports that this one did not enumerate
(shipping stale ones: the user's `builds/index.pck` carried 8 such, 2.3 MB), never to skip a bake; and its paths fold
in `GODOT_VERSION_HASH`, so every editor commit started a fresh directory — 42 of them, 2.3 GB, in that project.

**What changed:**

- **Batched, parallel bake.** `ShaderBakerExportPlugin` gained a second phase: with the platform's consent
  (`ShaderBakerExportPluginPlatform::uses_stage_batching()`) it keeps each variant's SPIR-V after glslang, hands
  every stage to a `ShaderBakerStageBatch`, then builds the containers on the pool. The WebGPU batch
  (`webgpu::WgslBakeBatch`, `drivers/webgpu/wgsl_bake_subprocess.{h,cpp}`) dedupes stages by SHA-256, screens
  the unconvertible ones, writes one temp directory, and runs ~4 chunks per core of `tint_convert_cli --batch`
  concurrently through `posix_spawn()` (no engine code runs in the child, so no lock). Results go to a
  process-wide table that `bake_wgsl_via_subprocess()` reads first; the old per-stage path remains as the
  fallback for a stage no batch covered, and as the Windows path inside a chunk (`OS::execute()` is fork-free
  there). Other baker platforms (Vulkan/Metal/D3D12) are unchanged. Progress bars: "Converting shaders",
  "Packing shaders"; cancel works. Cold export **7.8 s** (924 unique stages, 1.8 s of Tint across 96 batches);
  capped at 4 jobs still 8.6 s.
- **Content-addressed WGSL cache.** Entries at `<OS cache>/godot/webgpu_wgsl_bake/v1-<sha256 of
  tint_convert_cli>/<xx>/<sha256 of SPIR-V>.wgsl`, written temp-plus-rename, successes only (a cached failure
  would turn a one-off crash into a permanently unbaked shader), pruned after 30 days. The WGSL is a pure function
  of those two inputs, which is why this survives engine commits where the engine's own cache cannot; rebuilding
  Tint invalidates it through the tool hash. Warm export **4.2 s** (every stage from cache). `WEBGPU_BAKE_CACHE=0`
  disables, `WEBGPU_BAKE_CACHE_DIR` relocates, `WEBGPU_BAKE_JOBS` caps the batch concurrency.
- **Separate shader pack.** Web preset `shader_baker/separate_pack` (default on; off when the pck is encrypted,
  since the side pack is written plain). The baker hands its files to the platform
  (`ShaderBakerExportPluginPlatform::uses_separate_pack()`, `take_separate_pack_files()`) instead of
  `add_file()`; `EditorExportPlatformWeb::_save_shader_pack()` writes them sorted with `PCKPacker` to
  `<name>.shaders.pck`, lists it in `fileSizes`, the PWA cache and the shell config (`shaderPack`); `engine.js`
  preloads it beside the main pack; `ProjectSettings::_setup` mounts `<main pack>.shaders.pck` after the main pack
  (also for a native `<exe>.pck`). `index.pck` 14.9 MB, `index.shaders.pck` 35.6 MB with the same md5 across every
  export of the same shaders. The exporter warns when the template's `index.js` lacks `shaderPack`.
- **Prunes.** `EditorExportPlatform::_prune_stale_export_caches()` removes `.godot/exported/<hash>` siblings not
  touched for 7 days at the start of an export. `ShaderRD::_initialize_cache()` removes every group directory under
  its own shader name that is not in `group_sha256` — in the editor (`.godot/shader_cache`, 1,107 → 80 group dirs on
  the project copy) and in exported games (`user://shader_cache`, which on web is IndexedDB and used to grow across
  deploys). Strict by design: alternating two engine builds over one user directory recompiles on each switch.

**Verified:** all 163 containers of a pre-change export are byte-identical by content hash in the new output; four
exports (cold, warm, warm, warm with prunes) produce the same `index.shaders.pck`; the rebuilt nothreads template
loaded in system Chrome fetches `index.wasm`, `index.pck` and `index.shaders.pck` and reports
`baked 747 / translated 0` on Forward+. The unconvertible-stage screen and the `WEBGPU_BAKE_DEBUG_DUMP` hooks
behave as before; the batch summary is one verbose line
(`WebGPU shader baker: N stages -> U unique to convert (...); C converted, R rejected by Tint, ...`).

**Not done:** `file_cache`'s re-shipping of containers from earlier exports within one hash directory (upstream
behavior) is unchanged; §2.27's variant pruning is still the open size question; the seven other web templates
need a `build-linux.sh` run before their presets export with this editor; no TASKS §2 entry was closed because none
existed for export time.

Code: `drivers/webgpu/wgsl_bake_subprocess.{h,cpp}`, `editor/export/shader_baker/shader_baker_export_plugin.{h,cpp}`,
`editor/shader/shader_baker/shader_baker_export_plugin_platform_webgpu.{h,cpp}`, `platform/web/export/export_plugin.{h,cpp}`,
`platform/web/js/engine/{engine,config}.js`, `core/config/project_settings.cpp`, `editor/export/editor_export_platform.{h,cpp}`,
`servers/rendering/renderer_rd/shader_rd.cpp`.

---

### 4.24 Loading screen: per-stage text and visibility from the export preset, flex layout (October 2026)

Asked as "I want the loading screen text to be editable from the export screen, each stage with a text field
prefilled with the current value and tickboxes for the spinner and the text, and the chrome spaced evenly under
the logo like a flexbox". Built 2026-10-17.

**Export side** (`platform/web/export/export_plugin.cpp`): `web_loading_screen_stages[]` lists the 16 stages a
player can see — `initializing_webgpu`, `downloading`, `starting_engine`, the 12 engine startup phases
(`initializing_engine` … `rendering_first_frame`) and `compiling_shaders` — each with its old hard-coded string as
the default. `get_export_options()` emits `loading_screen/<stage>/text`, `/show_text` and `/show_spinner` per stage
(the inspector renders the nested path as a "Loading Screen" section with a subsection per stage), and `_fix_html()`
serializes them as the new `$GODOT_LOADING_SCREEN` placeholder, a JSON object keyed by stage. Documented in
`platform/web/doc_classes/EditorExportPlatformWeb.xml`.

**Shell side** (`misc/dist/html/full-size.html`): `showStage(key, suffix)` replaces the scattered
`setStatusLabel()`/`setBusyLabel()` calls and sets text and spinner explicitly on every transition; the download
percentage and the shader-compile count are the only suffixes. The `STARTUP_PHASES` table's third column is now a
stage key. A download progress callback that lands after `godot-before-callmain` (the preloader's final report)
no longer pulls the text back to the download stage. Layout: `#status` is a flex column (`gap: 1rem`, centered) holding splash, spinner, label, bar, notice
in that DOM order — the chrome used to be absolutely positioned at `bottom: 10%` and the DOM order had the bar
first. A hidden label/spinner keeps its box (`visibility`), so the bar never jumps between stages; one that no
stage ever shows gets `.unused` (`display: none`) so the column closes up. The full-size splash is `width: 100%;
height: auto` plus flex shrink, not `height: 100%` — with `height: 100%` the image box filled the viewport and
the letterboxed picture left the chrome floating far below the logo. What remains under the logo with the default
splash is that image's own margins (`main/splash.png` is 800×600 with the logo in the middle).

**Verified** against a stub `Engine` (scratch copy of the shell with the placeholders substituted) in Chrome:
stage order, custom text, hidden text reserving its line, unused spinner removed, no console errors; and
end-to-end on the user's project through `html/custom_html_shell`. A custom shell written before this change keeps
working as long as it does not reference `$GODOT_LOADING_SCREEN`; the stock template needs a rebuild (the shell
is packed into the template zip by `platform/web/emscripten_helpers.py`).

### 4.25 Per-variant generated sources (October 2026)

Asked for as "concurrency safety" after the pros/cons in §3 trap 24 were weighed. Built 2026-10-17.

Six generated files had one shared path each while their **content depends on the build options**, so two builds in
one tree overwrote each other's copy:

| File | Varies by |
|------|-----------|
| `modules/modules_enabled.gen.h` | the enabled module list |
| `modules/register_module_types.gen.cpp` | the detected module list |
| `modules/modules_tests.gen.h` | the enabled modules' test headers |
| `platform/register_platform_apis.gen.cpp` | the registered platform APIs |
| `core/disabled_classes.gen.h` | `disable_classes` |
| `scene/theme/default_theme_icons.gen.h` | `target` (editor/template_debug add the remote-debug icons) |

Each variant now writes its own copy under `gen/<variant>/` (`env["GEN_DIR"]`, named by the same suffix the objects
use), and that directory is **prepended to `CPPPATH`** so the existing `#include "modules/modules_enabled.gen.h"`
spelling resolves per build — none of the 55 files that include these had to change. `methods.gen_path()` builds the
path; the six `SCsub` call sites pass it instead of a bare filename.

**Measured** on `web.template_release.wasm32.nothreads`: building the linuxbsd editor in between invalidated **30**
objects before the change and **0** after. Both variants' trees coexist under `gen/` and differ by 9 modules
(`betsy`, `camera`, `cvtt`, `lightmapper_rd`, `objectdb_profiler`, `openxr`, `raycast`, `webxr`, `xatlas_unwrap`) and
3 theme icons (101 vs 98).

**The gotcha this exposed**, and the reason it is not a pure build-system change: a generated source may no longer
spell its own includes relative to the directory it used to live in. Three did — `register_module_types.gen.cpp`'s
`#include "register_module_types.h"`, and `register_platform_apis.gen.cpp`'s `#include "register_platform_apis.h"`
plus its per-platform `#include "{p}/api/api.h"` — and all three now use the project-root spelling the rest of the
engine already uses. Nothing enforces that rule, so a future generator can reintroduce it; it fails at compile time,
not silently.

**One-time cost:** adding a `CPPPATH` entry changes every compile command, so every variant recompiles once (§3 trap
25 is the same mechanism). Worth folding into the same sitting as any ccache decision, to pay it once.

### 4.26 ccache on by default, with a free opt-out (October 2026)

Follow-on from §3 trap 25, which had just cost a day's worth of confusion: a near-total rebuild that reads as tree
corruption is almost always the ccache flag being toggled, because SCons signs every object with the resolved
compiler path and `--ccache` swapped `em++` for a masquerade symlink.

`build-linux.sh` now defaults `USE_CCACHE=yes` and takes `--no-ccache` instead of `--ccache` (the old flag still
works). The part that matters: **`--no-ccache` does not remove the masquerade directory from `PATH`.** It exports
`CCACHE_DISABLE=1`, so ccache hands straight off to the real compiler while the path SCons signed the objects with
stays identical. Opting out, and back in, therefore costs nothing — which is what made the flag a trap before.
`ccache -C` is the way to actually discard the cache.

**Verified end to end through the script**: a `--no-ccache` run recompiled the editor in full (3046 objects — the
one-time cost of the masquerade entering `PATH`), and the very next run *with* ccache on recompiled **0**. That pair
is the property that matters: the flag no longer decides what gets recompiled. On
`web.template_release.wasm32.nothreads`, dry runs with `CCACHE_DISABLE=1`, without it, and with the masquerade
removed from `PATH` all report 0 stale.

One loose end, recorded rather than guessed at: the first web build after the masquerade appeared recompiled 2221
objects, but a later dry run with the masquerade *removed* reports 0 stale — so for web objects the masquerade is
evidently not what the signature turns on (`platform/web/detect.py` makes every source depend on
`get_compiler_version()`, a parsed version dict, and `env["CXX"]` stays the bare name `em++`). The 2221 was not
attributed. It is one-time either way, and it coincides with the recompile §4.25 forces.

Only the first run after this landed pays the one-time recompile of all 11 variants, and it coincides with the one
§4.25 forces anyway, so a single `./build-linux.sh` settles both.

### 4.27 Shader-bake reporting, and a stale installed editor (October 2026)

Reported as 28 export-time warnings — "leaving one shader stage unbaked (tint_convert_cli: Tint crashed (likely
TINT_UNIMPLEMENTED))" — on the user's `sewer-shift` project. The three browser-console items in the same report are
§4.28.

**The 28 were already fixed; the editor running them was stale.** Re-exporting that project with a current editor
gives **0 rejected by Tint, 0 tool failures**: 2317 stages, 558 duplicates, 913 from the WGSL cache, and 846 caught
up front by `_wgsl_unsupported_reason()` and reported at `print_verbose`, not WARN. The pre-check was extended in
`d862f8f0ed`, so an editor older than that still forked Tint and let it abort. The reason the installed editor was
older is its own bug: `build-linux.sh` installed it with `cp`, which fails with `ETXTBSY` against a binary that is
currently executing, and a headless editor left running from another session is enough. `set -euo pipefail` then
aborted the run *before any template was built*, and the previously installed editor stayed in place. It now
installs via a temp file plus `rename()`, which swaps the directory entry atomically and leaves a running process
on its own inode.

What the 846 actually are, now that the messages name them:

| Reason | Stages | Shaders |
|--------|--------|---------|
| `gl_ViewIndex` (multiview), which WGSL has no equivalent for | 836 | `SceneForwardClusteredShaderRD` vertex + fragment, 418 each |
| 16-bit integers | 6 | FSR2 / volumetric fog compute |
| image atomics (`OpImageTexelPointer`) | 4 | FSR2 compute |

All three are genuine WGSL limitations on variants the WebGPU renderer never requests, which is the case
`_wgsl_unsupported_reason()` exists for.

**Both messages now name the shader.** `bake_wgsl_via_subprocess()` takes an optional context string, filled at the
call site from the container's `shader_name` and `RDC::SHADER_STAGE_NAMES[stage]`, and all six per-stage reporting
sites use it. "one shader stage" 28 times was unactionable; `[SceneForwardClusteredShaderRD (Fragment stage)]` is not.

### 4.28 Console hygiene on web: a warning channel, a verbose readback note, pointer lock (October 2026)

Three items from one console paste of the user's `sewer-shift` export. The shader-bake half of that
same report is §4.27. The theme is that **a web export's console had no way to say "this is fine"** --
everything the engine printed arrived as `console.error` -- so three harmless messages read as three
failures, and the one real question (is anything actually broken?) could not be answered by looking.

**Web now has a real warning channel.** `OS::print_error()` hands warnings and errors alike to the
logger, `StdLogger` writes both to stderr, Emscripten routes stderr through `Module.printErr`, and
every shell logs that with `console.error()`. A `WARN_PRINT` was therefore indistinguishable from a
crash. `WebLogger` (`platform/web/os_web.cpp`, installed in place of the plain `StdLogger` in
`OS_Web::initialize()`) overrides `log_error()` and routes `ERR_WARNING` -- and only that -- through a
new binding `godot_js_os_print_warning()`, which calls the page's `onPrintWarning` config callback or
`console.warn()`. It emits the same two lines `Logger::log_error()` would have, script backtraces
included, as one console entry. `ERROR`, `SCRIPT ERROR` and `SHADER ERROR` are untouched, so nothing
that is a genuine fault got quieter.

Details worth keeping:

- The binding is deliberately **not** `__proxy: 'sync'`. A warning can be raised on the separate
  render thread (§2.5) while the main thread is blocked inside `callMain()`, which is exactly the
  shape that deadlocks; `console.warn` is available on a worker, so there is nothing to proxy for.
- `onPrintWarning` is plumbed the way this fork's other callbacks are: `config.js` (jsdoc + `parse` +
  `getGodotConfig`) → `GodotConfig.on_print_warning` in `library_godot_os.js`. `iframe.js` sets it
  with the same force-quit guard its `onPrintError` has. A page that sets nothing gets `console.warn`.
- Verified in the linked template, not just in source: `_godot_js_os_print_warning` survives DCE and
  carries the `GodotConfig.on_print_warning ... else console.warn(text)` body.

**The readback message is `print_verbose`, not `WARN_PRINT_ONCE`.** Handing back the zero-filled
shadow is the documented contract of an asynchronous readback (§2.13), not a fault, and it fires on
the first read of any buffer. Combined with the above it was the worst case: a routine event, printed
as a warning, rendered as an error, with a wasm backtrace attached. To see it now, add `--verbose` to
`GODOT_CONFIG['args']` in the exported HTML.

**Pointer lock no longer logs an unhandled rejection.** The same console carried `WrongDocumentError:
Failed to execute 'requestPointerLock'`. Mouse capture itself works, confirmed by the user, so this
was only an unhandled promise rejection: `GodotDisplayCursor.lockPointer()` called
`canvas.requestPointerLock()` and ignored the promise Chrome 113+ returns. A rejection there is
routine -- no user gesture, a request inside the browser's own unlock cooldown, or a canvas momentarily
out of the document -- so it is now swallowed with the same `if (promise) { promise.catch(...) }` idiom
`requestFullscreen` in that file already used, and `releasePointer()` is wrapped to match. Nothing
about capture behavior changes. If capture ever *does* fail to engage, `WrongDocumentError` is the
thread to pull: it means the canvas was not in the lock's document at that instant.

**The `ws://127.0.0.1:6007` failure is Chrome's, and cannot be demoted.** It is the browser's own
network-stack log for a failed WebSocket handshake, emitted from `new WebSocket()` inside
`_godot_js_websocket_create`; it is not an exception, never passes through an engine print path, and no
JS can suppress or re-level it. It also only appears in a run whose debugger host is unreachable:
one-click deploy bakes `--remote-debug ws://<host>:<port>` into the exported HTML's `args`
(`export_plugin.cpp:183`), so a `tmp_js_export` tab reloaded after the editor's debug session ended
retries it forever. A plain export carries no debug host at all -- verified by grepping one for the
port, where the only hit is a GL enum. Untick *Deploy with Remote Debug*, or stop reloading a stale
one-click tab, and the line is gone.

### 4.29 Forward+ parity sweep: analyzed against native Vulkan, not against the notes (October 2026)

Asked for "a final sweep … 100% feature complete and parity with native Forward+", done by reading the driver,
the shaders and the spec and then measuring every feature against native Vulkan, not by trusting §1's "no
known-broken shipped feature" — which turned out to be wrong (§3 trap 28). Everything here is uncommitted on
`webgpu-4.8` at the time of writing; what is left is §2.31.

**The parity tier.** `run_forward_plus.mjs --parity` renders every arm natively first (`--fp-matrix`, the same
`fp_scene.gd` on Vulkan) and fails an arm whose WebGPU frame differs from native by more than
`PARITY_TOLERANCE` (0.05 mean per channel, with per-feature `parityTol` for SDFGI's atomics and FSR2's fp16). Both
sides run at a fixed 60 fps with a 240-frame settle and a frozen capture frame; see trap 28 for why each of those
was needed. Arms that only make sense as parity checks (`sdfgi_camera_move`) are `parityOnly`. New arms:
`omni_shadow_cube`, `sdfgi_camera_move`, `depth_texture`, `mipmap_bias`, `texture_array`, `shader_builtins`,
`point_coord` (71 arms). `local_ci.sh` runs it as a stage; `--console` echoes the page's console for triage.

**What was wrong, and the fix (WebGPU vs Vulkan mean diff, before → after):**

| feature | cause | fix |
|---|---|---|
| SDFGI while the camera moves (25.1 → 0.025), static (1.77 → 0.028) | `command_clear_color_texture` cleared only slice 0 of a 3D texture; buffer→texture and clear uploads went through `wgpuQueueWriteTexture`, which runs before the encoder's earlier copies | clear every z-slice; uploads recorded as encoder copies (`clear_zero_buffer`, `flush_from_shadow`); 3D z offsets in texture↔texture and texture→buffer copies |
| volumetric fog (2.05 → 0.001) | shadow atlases are depth textures in a float slot (trap 29), read as 0 | renamed to `*_depth_atlas`, own nearest `depth_sampler` (binding 22), `texture_linear_depth()` gather helper |
| omni cube shadows (missing → 0.015) | `API_TRAIT_FORCE_OMNI_DUAL_PARABOLOID` forced every omni light to dual-paraboloid | trait 0; `cube_to_dp`'s source renamed to `source_depth_cube` so the reclassifier sees it |
| `DEPTH_TEXTURE`, refraction, proximity fade (→ 0.002) | `copy_depth_to_rect` sampled the depth buffer in a float slot | `COPY_MODE_SIMPLY_COPY_DEPTH_FORMAT` (`SOURCE_IS_DEPTH_FORMAT` in `copy.glsl`) |
| contact shadows (→ 0.003) | relied on a border colour of 0 | `read_depth()` emulates the border |
| soft / transmittance shadow reads | the fork had replaced 7 linear depth reads with NEAREST | `textureLod_linear_depth()` (gather + bilinear weights): soft shadows now exact |
| mipmap bias (TAA/FSR's -0.5, FSR's scale bias) | WebGPU samplers have no LOD bias; silently dropped | `apply_sampler_lod_bias` pass + `SamplerLodBias` UBO (set 1, binding 40) from `MaterialStorage`; 0.003 |
| texture arrays (VoxelGI > 1 probe, lightmaps > 1, SDFGI's read-only `src_occlusion`, particle colliders) | `flatten_binding_arrays` collapsed every array to element 0 | `expand_binding_arrays` pass, layout in `spirv_binding_layout.h`, per-stage budget (trap 30) |
| `isnan`/`isinf`, `textureQueryLod`, `POINT_COORD`, `early_fragment_tests` in user shaders | Tint ICE — the wasm instance aborts | `lower_wgsl_unsupported_ops` pass |
| FSR2 | `OpImageTexelPointer` on the reconstructed depth (Tint ICE, so the pass was skipped), wave ops in SPD | `NO_IMAGE_ATOMICS` storage-buffer form, `SPD_NO_WAVE_OPERATIONS` (`thirdparty/amd-fsr2/patches/0004-…`); 0.55 residual, §2.31 |
| a material reading `POINT_COORD` without writing `POINT_SIZE` (drew nothing; upstream bug, D3D12 too) | `POINT_COORD` sets `uses_point_size`, so where point size is emulated the mesh is drawn as six vertices per point, but the quad expansion was behind `POINT_SIZE_USED` alone: six coincident vertices, degenerate triangles. Natively it drew 1-px points with `POINT_COORD` pinned to 0.5 | both scene `_inc.glsl` files define `POINT_SIZE_USED` when `POINT_COORD_USED` is set; new `point_coord` arm. Found by the parity run itself: no error anywhere, only an empty frame |
| constant-alpha blending (`BLEND_FACTOR_CONSTANT_ALPHA`) | WebGPU has one RGBA blend constant and no constant-alpha factor; the alpha factors read the colour's alpha | emulated: the pipeline remembers it, `_apply_blend_constants()` broadcasts alpha into the constant |
| depth clamp (cluster light volumes crossing near/far) | ignored | `depth-clip-control` requested, `primitive.unclippedDepth` |
| `sample_mask`, `limit_get()` gaps (index, framebuffer/viewport dims, per-stage, stride, UBO alignment), texture/attachment limits left at defaults | — | filled from the device; `engine.js` raises 1D/2D/3D dimension, array layers and `maxColorAttachmentBytesPerSample` to the adapter's |

Also recalibrated: `glow_map` (10.4 → 0.7) and `proximity_fade` (1.09 → 0.19), whose floors had been set on the leaky fixture (trap 28) — native and web agree on both to 0.003. Also: a spurious "This export ships a baked shader cache…" warning on native `--path` runs of an unexported
project (the editor's own `.godot/shader_cache` from another build looked like a shipped cache) is now limited to
export templates.

**Tests added:** preprocessing tests 24-27 (`wgsl_unsupported_ops.frag`, `binding_arrays*.{frag,comp}`, each output
validated with `spirv-val`); `driver_unit_tests/test_binding_layout.mjs` (binding-layout invariants against the header,
every engine texture array fits, binding 40 agrees across both scene shaders and `MaterialStorage`); the six matrix
arms and the parity tier above; `self_test_forward_plus.mjs` covers the parity judge.

### 4.30 Web audio: Sample playback brought to parity with AudioServer, and a parity tier (October 2026)

Asked for a tip-to-tail review of web audio against native. The Stream path (AudioWorklet / ScriptProcessor feeding
`AudioServer`'s own mix) is the native mixer and was fine apart from the items marked *stream* below; the Sample path
(the web default) re-implements the mixer on a Web Audio graph and had drifted in many places (§3 trap 32).

| area | was | now |
|---|---|---|
| surround | `GodotChannel` indexes one off (C read the LFE gain, SR read past the vector); output always stereo | volume vector index *i* → output channel *i*; the context uses the device's channels in the layouts where Godot's order is Web Audio's (2, 5.1, 7.1), like the native drivers |
| loops | ping-pong and backward played as forward loops | unrolled into the buffer exactly like `AudioStreamPlaybackWAV` (mirror at `loop_end`, backward from the start position); IMA ADPCM loops forward, as natively. Rendered from a non-looping copy of the WAV |
| volume | `AudioParam.value` jumps (zipper clicks on every 3D move) | ramps over one 512-frame mix block; a new bus starts at full volume; a dropped bus, stop and pause fade over one block; resume fades in |
| buses | single solo, mute honored while soloing; sends to earlier buses kept; new buses from `set_bus_count` unconnected; `move_bus` always inserted at `to - 1`; `add_bus` ignored -1 | `AudioServer::_mix_step()`'s rules: solo chains (several soloed buses, mute ignored), invalid sends → Master, `move_bus`/`add_bus` indexing identical |
| `set_bus_layout()` | synced sample buses inside the rebuild loop: `get_bus_index()` read **freed** `Bus` pointers, and sends to later buses resolved to -1 | synced once after the loop (`servers/audio/audio_server.cpp`) |
| 3D | attenuation high shelf ignored for samples | new `AudioDriver::set_sample_playback_highshelf_params()`; a `BiquadFilterNode` matching `AudioFilterSW` at and above the cutoff (Web Audio's shelf slope is fixed at S = 1, Godot's Q = 1: up to ~1 dB apart in the transition) |
| positions | a worklet per playback posting one message per 128-frame quantum; pitch clamped to 4 in it | computed from the context clock and the playback rate (no worklet; `audio.position.worklet.js` and its export/PWA plumbing removed) |
| polyphonic | sample streams ignored `pitch_scale`; `is_stream_playing()` stayed true forever after a sample stream ended on its own (upstream) | both fixed in `audio_stream_polyphonic.cpp` |
| memory | the whole decoded sample copied into a new `AudioBuffer` on **every play**; registered samples never freed | playbacks share the stream's buffer; streams whose `AudioStream` was freed are unregistered on the next registration (and every 64 starts) |
| marshaling | every call turned 64-bit ObjectIDs into strings both ways; volumes/pitch crossed into JS every frame (JS-side skip) | integer handles; unchanged volumes, pitch and high-shelf params are skipped in C++; registration/activity answered from C++ maps. 3D platformer audio glue **0.029 → 0.002 ms/frame** (`tests/web_audio_profiling.mjs`) |
| *stream* | output truncated to 16 bits (`>> 16`); a mono microphone fed to Godot as if stereo (double speed); the nothreads capture ring buffer dropped data on wrap (`subarray(high, to_write)`) | full-precision conversion; the worklet up-mixes its input to two channels; wrap fixed |

Shared-code edits (all no-ops on native): `AudioDriver::set_sample_playback_highshelf_params()`, the
`set_bus_layout()` sync, the polyphonic fixes, and dropping the duplicate sample pitch update in the 2D/3D players
(`set_playback_pitch_scale()` already routes samples).

**Tests.** `webgpu_tests/web_audio/run_tests.mjs` (no build, ~10 s): 40 checks rendering the real driver in an
`OfflineAudioContext` against ports of `AudioStreamPlaybackWAV::mix()`, `AudioFilterSW` and the bus rules — the loop
modes are compared sample for sample. `run_e2e.mjs` (after `./export.sh`): the same GDScript natively and in both web
exports — finish times, positions through loops/seeks/pitch/pause, bus edits, 3D, polyphonic — 18/18 on nothreads
(served without COOP/COEP) and threads. Scene smoketest 21/21 on the new nothreads template.

**Not reproducible on the Sample path:** bus effects, and streaming (a sample is decoded whole on its first
`play()`). Those need `playback_type = Stream`, which is the native mixer; in a thread-enabled export it mixes on its
own thread. Defaults are unchanged; the options are §2.32, and both gaps are now warned about (below).

**Follow-up the same day (smoketest coverage, warnings).** The fixture became a self-checking suite (`main.gd` prints
`[AUDIO-CHECK] PASS`/`FAIL`) with Ogg/MP3 samples, seek and pitch changes mid-play, polyphony, a 2D player, and Stream
playback through a capture effect, a bus meter, a generator and a randomizer; it is the smoketest's `audio_parity`
scene (Chrome and Firefox; new `click_to_start` and `timeout_ms` scene options) and `local_ci.sh` runs both audio tiers.
Writing it found:

| found | fix |
|---|---|
| a stream's loop settings and data were captured when it was first registered as a sample: turning `loop` off later still looped forever on web (`finished` never fired) | the WAV/Ogg/MP3 setters emit `changed` (they mostly did not); the web driver listens and renders the stream again on its next start |
| every non-sampleable stream (generator, microphone, script streams) warned "cannot be sampled" on each `play()` on web, though only `Default` was asked for | warns only when Sample is requested explicitly |
| **upstream**: `AudioServer::finish()` never freed the playback list, so a sound playing or fading out at quit leaked its playback and stream ("ObjectDB instances were leaked at exit") | `finish()` frees the list and the bus-details graveyards once the drivers stopped |
| bus effects and long samples silently did nothing / hitched | warnings at export and at runtime (§2.32) |

### 4.31 Editor: one drag-and-drop behavior for every list (October 2026)

Asked for: dropping several files from the FileSystem dock onto list-like UIs (e.g. AudioStreamRandomizer's streams)
did nothing, while `Array` properties took them — make every array/dictionary-like UI behave the same, as flexibly as
possible. An inventory of the editor (every `ADD_ARRAY` user and every custom list panel) found multi-file drops only in
`EditorPropertyArray` (resource and node arrays only), SpriteFrames, ResourcePreloader and the TileSet source lists.

**One resolver**, `editor/inspector/editor_collection_drop.{h,cpp}`: dragged files/folders, a resource or nodes become
values for a slot of a given type — resources (class and script class checked, *any* of a hint's classes; the old array
code required all of them), nodes, relative NodePaths, or paths (a `uid://` for `@export_file`, `res://` otherwise,
absolute for the global hints, folders for the dir hints, extension filters honored); untyped slots take resources and
NodePaths. Then:

| UI | before | now |
|---|---|---|
| `EditorPropertyArray` | resource/node arrays only; untyped arrays and `Array[String]`/`PackedStringArray` (file paths) refused; drop only on the header and Add button | every array type the resolver covers; the expanded list takes drops too |
| `EditorPropertyDictionary` | no drops | entries per item: as values keyed by name (deduplicated) or next int, or as keys with default values when the values hold plain data |
| `EditorInspectorArray` (property arrays: the audio streams, MultiplayerSpawner scenes, ItemList/OptionButton/TabBar items, ...) | only its own reorder drags | appends an element per item into the element property that takes it, sets `name`/`text`; header, list and Add button; one undo step (count-property arrays append; move-function arrays insert last-first at the end so the recorded undos stay valid). An empty array's element properties come from a probe instance of the class |
| Project Settings > Autoload | accepted any drag, then added nothing | scripts and scenes become autoloads; reordering only takes its own drags |
| Project Settings > Localization | reorder only | translations, remaps, remap options and POT sources take files through the same add functions as their file dialogs |
| Audio bus effects | reorder only | `AudioEffect` files/resources append |
| Animation Library dialog | none | library files add libraries, animation files dropped on a library add to it |

**Test**: `tests/editor_drop/run.sh` — real drags (`Control.force_drag()` + pushed mouse events) in a windowed editor
under Xvfb: 32/32 (headless cannot work: its editor viewport is 2x2). Not covered: the Animation Library dialog.

### 4.32 AccessKit prerequisite no longer re-downloaded (October 2026)

`misc/scripts/install_accesskit.py` (called by `build-linux.sh`, `build-macos.sh`, `build-windows.ps1` and the CI
workflows) deleted and re-downloaded the AccessKit archive on every run. The `version` file it writes as the last step
of an install now marks a complete one: when it names the pinned version (`ac_version`, 0.23.1) the script exits
without a download, another version is replaced, and `--force` reinstalls. Bumping `ac_version` is all an upgrade takes.

### 4.33 Braced GDScript (`.gdb`) 1.0 (October 2026)

GDScript written with `{ }` blocks in `.gdb` files, with `//`/`/* */`/`///`/`/** */` comments, `else if`,
`c ? a : b` and arrow lambdas, loaded by the same `GDScriptLanguage` (so `.gd` and `.gdb` mix freely), plus one
engine formatter and editor support. Built on 2026-10-18 on branch `gdb/m0` in a worktree, merged into
`webgpu-4.8` with the tag `gdb-1.0`. Every phase of the plan is done; nothing has been tried by hand in Rider or
the Godot editor yet, which is what is left. The plan, `webgpu_notes/Braced GDScript (.gdb) — Implementation
Plan.md`, is gitignored and only on disk; `docs/braced_gdscript/` is the maintained description.

| area | what landed | evidence |
|---|---|---|
| Engine | tokenizer brace mode (D2/D3), Tier A and B syntax, `.gdbc` binary-token export (D8/D9), analyzer/LSP/class cache for `.gdb`, `gdscript_braces=no` | 9 `.gd`/`.gdb` twin tests share their `.out`; every upstream test converted to `.gdb` prints what it printed, in text and binary-token mode |
| Formatter | presets `godot`/`js`, `.editorconfig`, line wrapping, trailing commas, alignment (off by default), `--check --diff`, `--range`, `--stdin`, `--gdscript-format-server` (~0.3 ms per request) | idempotent over 703 scripts in both presets and at 40 columns with every rule on; 300,000 fuzzed inputs clean |
| Converter | `.gd` ⇄ `.gdb`, CLI and the FileSystem dock (paths in other files updated) | round trip of every upstream test prints what it printed |
| Godot editor | per-path comments and Auto Indent, `}` unindents, Reformat Code (selection or file, Ctrl+Alt+L), reformat on save, New Script and templates | unit tests of the hooks |
| Rider plugin 1.0.0 (`../godot-gdb-rider`) | `.gdb` as a dialect of Rider's GdScript language (completion, navigation, rename, inspections offline), generated code braced, inspections, intentions, postfix, docs, breadcrumbs | 43 tests; PSI identical to the `.gd` twin's for 459 of 500 corpus files, the rest explained; Plugin Verifier compatible |
| VS Code 1.0.0 (`../godot-gdb-vscode`), tree-sitter (`../godot-gdb-tree-sitter`) | formatter through the server, Format Selection; a grammar with highlight/fold queries | grammar parses all 515 `.gdb` files |
| Tests and CI | `tests/braced_gdscript/run.sh`, fuzzing, a `.gdb` project in the web scene smoketest (passes in Chrome and Firefox as `.gdbc`), CI quick tier and a `gdscript_braces=no` build | |

Web templates built before this merge cannot load `.gdb` scripts: rebuild them (and the editor) from this tree.
`build-linux.sh` passes `gdscript_braces` to every build of a run (`--no-gdscript-braces` for a standard editor and
templates), so a braced editor never exports `.gdb` scripts for templates that cannot load them, and ends by building
the Rider plugin and the VS Code `.vsix` into `builds/<version>/editor_plugins/` (`--skip-editor-plugins`, or `--plugins-only` for just
those; a failure there only warns). The syntax version (`--gdscript-braces-version`, 1) is mirrored by the Rider lexer and the
tree-sitter grammar; `docs/braced_gdscript/internals.md` says when to bump it. The ES6 `.gdj` alternative was
assessed and rejected (`webgpu_notes/ES6 language (.gdj) — Assessment.md`; its conclusion is also in
`docs/braced_gdscript/faq.md`).

### 4.34 Editor: per-project default folders for new files (October 2026)

Asked for: extend the per-project default script folder (`editor/script/default_script_folder`, added 2026-09-28) to
materials, shaders, scenes, settings and resources, and give the create dialogs a check box (off by default) that makes
the chosen folder the new default. Models and audio were dropped from the request: they are imported, not created.

**One helper**, `editor/file_system/editor_default_folders.{h,cpp}`: the `editor/default_folders/{scripts, materials,
shaders, scenes, settings, resources}` project settings, the category of a resource type (Script, Shader/ShaderInclude,
Material, PackedScene, the configuration types Environment/CameraAttributes/Compositor/Theme/AudioBusLayout/Shortcut/
InputEvent/PhysicsMaterial, then every other Resource; a script-class resource falls under its native base), and the
"Make this the default folder for ..." check box, whose tooltip names the current default. Ticking it writes the setting,
saves `project.godot` and shows a toast. The old script setting is moved to `editor/default_folders/scripts` when a
project is opened (in memory; it reaches disk with the next settings save).

| Dialog | proposes the default folder | check box |
|---|---|---|
| Create Script (`ScriptCreateDialog`) | as before, unless opened from a FileSystem dock folder | under Path, hidden for built-in scripts |
| Create Shader (`ShaderCreateDialog`, new `use_default_folder` argument; its `config` binding now names all six arguments) | unless opened from a FileSystem dock folder | under Path, hidden for built-in shaders |
| Create New Scene (`SceneCreateDialog`, FileSystem dock only) | no: it creates in the dock's folder | yes |
| Save Scene As (`EditorNode`'s shared `file` dialog) | for a scene saved the first time | yes, as a `FileDialog` option |
| Save Resource As (`EditorNode::save_resource_as()`: inspector, resource picker, script/shader/theme editors, FileSystem "New Resource") | for a resource that is not a file yet, unless a folder was given | yes, by the resource's category |

`EditorNode`'s `file` dialog serves every scene/resource open and save, so each popup sets or clears the option
(`_set_file_default_folder_option()`), which also resets it to unticked.

**Test**: `tests/default_folders/run.sh` — an editor plugin drives the real dialogs (menu items, OK button, check
boxes) in a windowed editor under Xvfb and checks the proposed paths, the settings and `project.godot`: 30/30.

### 4.35 CI: editors that start, and manual editor builds (October 2026)

Asked for: why the macOS editor does not run on an M2 MacBook Pro, and workflows to build each platform's editor on
demand. Cause and fix: §3 trap 41 (permissions lost in `upload-artifact`; Linux had the same problem). The release
draft and README now also say how to get past Gatekeeper the first time (ad-hoc signed, not notarized: System
Settings → Privacy & Security → *Open Anyway*, or `xattr -dr com.apple.quarantine`).

Manual builds: `editor_linux.yml`, `editor_macos.yml`, `editor_windows.yml` (Actions → Run workflow, optional extra
SCons flags) and `editor_all.yml` (all three in parallel). Each is the release editor job of its platform's
`*_builds.yml` cut down to the editor (same flags, no tests or Visual Studio project files) and uploads the same zip a
release ships. They are separate files rather than an input on the release workflows because a matrix of `include`
entries cannot be filtered by `exclude`, and an `if:` on every step of three workflows would be worse. Keep their
flags in step with the release jobs. Only the Linux half of `package-editor` was run locally (a zip of non-executable
files comes out `-rwxrwxr-x` and the unpacked editor starts); the macOS and Windows halves first run on GitHub.

### 4.36 Tint → SPIRV-Cross: the converter swap (October 2026) *(was §2.35; work tasks 00–19)*

**What changed.** SPIRV-Cross's WGSL backend ([Ludicon/spirv-cross-wgsl](https://github.com/Ludicon/spirv-cross-wgsl),
vendored as `thirdparty/spirv-cross-wgsl`, its own namespace beside Metal's `thirdparty/spirv-cross`) now does every
SPIR-V → WGSL conversion after the unchanged `run_pipeline()` passes: the runtime (`spirv_to_wgsl.cpp` →
`spirv_cross_wrapper.cpp`), `wgsl_precompile.py`, the export-time bake and every test tier. Tint is deleted
(`thirdparty/tint`, 830 files; `tint_wrapper.*`, `tint_ir_transforms.*`; the temporary `webgpu_wgsl_backend` SCons
option), ~204k lines in all. The host tool is `bin/wgsl_convert_cli`, built by `drivers/webgpu/wgsl_cli/build.sh` (C++17
now, ~210 objects instead of ~570); `WGSL_DEBUG_DUMP_PREPROCESSED`, `WGSL_CONVERT_CLI` and `--skip-wgsl-cli` replace the
`TINT_*` names and `--skip-tint-cli` (the old env var and flag still work for one release). The tag `pre-SPIR-V-Cross`
marks the tree before; the user's decision and the evaluation it was made on are §2.35. It ran 2026-10-25 as one lead
and parallel workers on a work board (`webgpu_notes/work/`, retired afterwards; its `results/` are in git history).

**The bar** (user, 2026-10-25): Forward+ at parity with native Vulkan, no test tier worse than at `pre-SPIR-V-Cross`,
cameraSim by eye, and Firefox desktop and Chrome for Android working *and as fast as with Tint or faster*. Every part
held:

| check | Tint | SPIRV-Cross |
|---|---|---|
| Forward+ `--parity`, Chrome desktop (4080 SUPER) | 71/71 baked | **71/71** unbaked (every stage converted at runtime) and baked; fsr2 0.142, fog 0.001 |
| Firefox desktop (Linux, wgpu/Vulkan): parity, smoketest, naga | 69/71, 23/23 | **69/71, 23/23** — the same two SDFGI arms, 0.277, Firefox-only (§2.40); naga 276/276 engine variants, 2564/2566 project stages on both |
| cameraSim (Ava MetaHuman), 5 views vs native Vulkan | MAD 0.00–0.03 | **MAD 0.00–0.03** — run-to-run noise; user: "Matches"; `baked 1570 / translated 0` |
| desktop Chrome GPU, heavy Forward+, median ms/frame | 1.47 | **1.43**; perf_guard scenes equal, benchmark_postfx +0.014 ms (+2%) |
| Firefox desktop GPU, heavy, ms/frame | 3.88 | **3.82**; postfx +10% fps; shadows/pbr ~1.8% fewer fps at ~1000 fps, CPU-side, GPU equal |
| Razr 60 (Mali-G615, Chrome 155) GPU ms/frame: fp / heavy / postfx / platformer / stress | 16.9 / 73.8 / 23.3 / 29.6 / 28.8 | **16.8 / 71.3 / 23.2 / 28.7 / 28.1** with patch 0019; fp_check Look set (34 frames) identical to Tint's |
| fp_check `index.shaders.pck` raw / gzip -9 | 29.6 / 4.43 MB | 34.8 / **3.70 MB** (−17% compressed) |
| release nothreads wasm | — | −1.6% raw, −2.0% gzip (SPIRV-Tools stays) |
| converter CLI, clean build (-j12) | 63 s | **44 s** (368 → 7 converter objects; both compile SPIRV-Tools' 199) |
| web template: converter + driver objects cold + precompile, no link | 41.9 s, 380 TUs | **10.7 s, 20 TUs** — ~31 s per variant, ~4 min over `build-linux.sh`'s 8 (small with a warm ccache: 121 vs 102 s relink) |

Firefox for Android was ruled out of scope by the user. Every engine variant converts per module several times faster
(§2.35's table; the bake was never converter-bound, §3 trap 26).

**What it took, by task** (each merged as `Merge work/<n>`, `git log --first-parent pre-SPIR-V-Cross..`):
- *00* foundation: vendored backend, `spirv_cross_wrapper`, the CLI building either converter, `smoke.sh`.
- *01, 15, 06, 12* — the four SPIRV-Cross patches (`thirdparty/README.md`): **0001** barrier/load/barrier on a workgroup
  variable → `workgroupUniformLoad` (FSR2's luminance pyramid failed barrier uniformity); **0015** uniform compute builtins
  reach the inner entry point as parameters, not `var<private>` (screen-space contact shadows failed it the same way);
  **0006** 16-bit norm storage formats (CanvasSdf's r16snorm); **0019** never emit WGSL `for` (§3 trap 58).
- *02* the driver's WGSL reads moved into `wgsl_text_scan.{h,cpp}` (~1,500 lines of `strstr` replaced); found SSR's
  `source_depth` that SPIRV-Cross's output shape would have sent to the blank fallback (§3 trap 64).
- *03* the in-engine converter on SPIRV-Cross: `-fwasm-exceptions` on its translation units and the link only, the
  rest of the engine stays `-fno-exceptions`; a throw is a failed conversion. All 8 web variants link.
- *04, 14* specialization constants: `spec_constants_overridable()` unchanged on evidence (93 of 276 variants keep 216
  overrides, none frozen); `wgsl_convert_cli --spec-info`/`--raw`, `run_spec_ops.sh`; `freeze_spec_constant_ops()` now
  evaluates CompositeExtract/Insert, VectorShuffle and vector ops on constant composites and SRem/SMod (it folded them
  to 0, with Tint too), and warns with `OpConstantNull` where it cannot.
- *05* real projects: fp_check, sewer-shift, cameraSim and the smoketest projects, 2,566 unique stages, all converted
  with Tint-identical interfaces, output 1.09–1.14× Tint's size (`spirv_cross_eval/project_export.sh`, `project_eval.py`).
- *06* every offline tier on the converter of choice (backend-neutral assertions); the fuzzers, broken before the swap,
  fixed and ported.
- *07, 16* Forward+ parity; the baked "fog race" was the capture landing on two jitter phases (§3 trap 56).
- *08, 11, 12* GPU timing on desktop Chrome, Firefox and the Razr 60 (rotated launch order, ≥3 launches, medians;
  `perf_guard/frame_time.mjs`, `phone_bench.mjs`, `firefox_prefs.mjs`, `WEBGPU_AUDIT=<file.jsonl>`). A `var<private>`
  candidate for SPIRV-Cross's module-scope `const` arrays (SSIL, §2.30) measured no different and was not applied.
- *13* `run_pipeline()` validates its input (SPIRV-Tools, Vulkan 1.2, ~2.3 ms a module) and rejects malformed SPIR-V
  instead of asserting; every optimizer pass runs `check_ir_preconditions()` first; `rewrite_copy_logical` capped (a
  valid 1e8-element copy took 6.2 GB / 51 s); fuzzers instrument the passes (~5,000 edges, all 27 passes, 10 min each
  clean). The lead made full validation opt-out for the engine's runtime (`e99b18555e`): its input is glslang's, and on
  the web it would be pure load time (§3 trap 65).
- *17* FSR2's softness on WebGPU was a dropped view swizzle (amd-fsr2 patch 0005, §3 trap 55); `drivers/webgpu/SCsub`
  now also fingerprints the FSR 1/2 shader sources; `run_forward_plus.mjs` refuses a stale export (§3 trap 57).
- *18* cameraSim by eye (above). *19* is §4.37, done on the same board.
- *09* Tint removed, the CLI renamed, CI keys/artifacts renamed (`wgsl-cli`, `wgsl-convert-cli`; not yet run on GitHub,
  §2.38). Verified with the editor and all 8 web variants built `dev_mode=yes`, smoke 276/276 against the stored
  references, shader_corpus 14/14, preprocessing 291/0/1, driver unit 379/0, spec_constant_overrides 5/0 offline,
  determinism, fuzzers, `local_ci.sh --dev-mode --no-safari` 28/1/5 (the one failure, web audio e2e nothreads in a
  headless page, fails identically on a 2026-10-23 Tint-era export).

**Interface references.** With no Tint to compare against, `webgpu_tests/spirv_cross_eval/interface_reference.json`
(bindings, kinds and override ids of the 276 engine variants, as Tint emitted them at `7f2bf18eb2`) and
`webgpu_tests/wgsl_text_scan/scan_reference.txt` (the driver's text-scan facts for the same variants, both modes) are
what the smoke check and the text-scan tier compare with; SPIRV-Cross matched 276/276 and 552/552. After a deliberate
interface change, regenerate (`compare_interface.py --write`, `WRITE_REFERENCE=1 run_tests.sh`) and say why in the
commit.

**Renamed after the merge** (the user's choice): `lower_tint_unsupported_ops()` → `lower_wgsl_unsupported_ops()` and
the fixture `tint_crash_inputs.frag` → `wgsl_unsupported_ops.frag`. Pass rationales keep their Tint history in past tense. Browser
Tint (Chrome/Dawn compiling our WGSL) is unrelated and untouched. `spirv-headers`' broad `unified1` extraction stays
for SPIRV-Tools; whether `spirv.hpp11` can return to the vulkan-sdk pin was not tried.

### 4.37 Web export: a screen that stays on, its orientation, fullscreen on the first tap (October 2026) *(was §2.36; work task 19)*

Asked for (user, 2026-10-25): signal performance, keep the screen awake, lock the export panel's orientation. Merged
`6d8a3e6dc0`.
- *Screen awake:* `DisplayServerWeb::screen_set_keep_on()` takes a Screen Wake Lock and re-requests it on every
  `visibilitychange` to visible (the browser drops it when the page is hidden); the constructor applies
  `display/window/energy_saving/keep_screen_on` as X11/Wayland/Windows do; released at exit. `FEATURE_KEEP_SCREEN_ON`
  is true where `navigator.wakeLock` exists (not in insecure contexts).
- *Orientation:* `screen_set_orientation()` / `screen_get_orientation()` map to `screen.orientation.lock()`, requested
  at once and on every fullscreen entry, since Chrome for Android allows it only in fullscreen. `FEATURE_ORIENTATION`
  needs `lock()` **and** `(any-pointer: coarse)`: desktop browsers have `lock()` and always reject it. Export option
  `html/orientation` (default Project Setting): any other value is written into the exported `project.binary` as
  `display/window/handheld/orientation` (`get_custom_project_settings()`, as iOS does) — no `GODOT_CONFIG` key, so
  custom shells get it too. The PWA manifest follows the same value; `progressive_web_app/orientation` is hidden and
  honored only for old presets. The manifest's `display` clamp read past its array; fixed.
- *Fullscreen on the first tap* (user decision): `html/fullscreen_on_first_tap`, default on (Engine config
  `fullscreenOnFirstTap`, default off for hand-written shells). Where the **primary** pointer is coarse, the canvas
  enters fullscreen on the first tap's `touchend` (§3 trap 63), retried until it succeeds and never again once the
  page has been fullscreen; entering it fires the orientation lock. Desktops, touchscreen laptops included, are
  unaffected.
- *Performance:* every adapter request already asked for `powerPreference: 'high-performance'` except the worker
  capability probe in `engine.js`, which now does. The web has no stronger signal (`powerPreference` is a hint; the
  Compute Pressure API only observes).
- Refusals (no API, hidden page, lock outside fullscreen, desktop) print only with `--verbose`, never as warnings.

**Behavior change:** a project that never set `display/window/handheld/orientation` has the default, Landscape, so on
a phone the export now locks landscape when it enters fullscreen and an installed PWA's manifest says
`landscape-primary` instead of `any` — what native Android does with the same project. Set Sensor for free rotation.
**Known edge:** a feature-tag override (`display/window/handheld/orientation.web_android`) still beats
`html/orientation` at run time, because the export writes the base key only.

**Verified:** Razr 60, Chrome 155: first tap → fullscreen and `landscape-primary` / `portrait-primary` as exported,
with the option off a tap stays windowed; screen awake 60 s untouched against a 30 s system timeout, dozing 30 s after
the tab closed. Desktop Chrome and Firefox: no errors, features reported unsupported where expected. Lead: editor +
release template built, smoke OK, driver unit 379/0, fixture export clean with `fullscreenOnFirstTap: true`.

### 4.38 Occlusion culling on the web: the raycast module (Embree) in web templates (October 2026) *(was §2.45; work task 22)*

Merged `b13c7bee24` (work/22: `10fe02ea56`), default decided in `508f4ed3b2`. The text below is §2.45 as it stood when
it closed; only its last line (what is still open) was updated.

**Decision (user, 2026-10-26):** on by default in every web template (`module_raycast_enabled` defaults to yes in
`platform/web/detect.py`; `build-linux.sh --no-web-raycast` leaves it out), with a Web export option
`rendering/occlusion_culling` (default on). Off overrides `rendering/occlusion_culling/use_occlusion_culling` to false
for that export, so it saves the per-frame cost; it cannot shrink the download, since presets share one template. A
script that sets `Viewport.use_occlusion_culling` itself still enables culling (the fixture and occlusion_bench both do,
so neither can test the option). Checked with a minimal project that relies on the project setting: option on, 0 of 20
occluded boxes drawn; off, 20 of 20. The measurements below are from before the decision.

Web templates leave out the raycast module, as stock Godot does (`platform/web/detect.py`, upstream GH-70621: memory
and binary size), so the web had no occlusion culler and the Forward+ matrix's `occlusion_culling` arm was KNOWN
(29 -> 29 objects); `features.mjs` now marks it `needsRaycast`, KNOWN only against a template without the module. It is a build option, not a port problem: `module_raycast_enabled=yes` (or
`build-linux.sh --web-raycast`) builds Embree under Emscripten 6.0.9 with no Embree patch (its SCsub already passes
`-msimd128` and the SSE defines on web, and patch 0003 already reads the thread count from the browser).

**One engine fix was needed, and it is an upstream bug**: `RaycastOcclusionCull::Scenario::update()` commits the
Embree scene on a `Thread`, and in a `threads=no` build `Thread::start()` is an empty stub, so the scene was never
committed nor swapped in and nothing was ever culled. Fixed by committing synchronously without `THREADS_ENABLED`
(`modules/raycast/raycast_occlusion_cull.cpp`; §3 trap 69). Embree itself gets `threads=1` on the web
(`get_processor_count()` is capped at 2), so it never spawns pthreads of its own.

Measured (RTX 4080 SUPER, Chrome, release templates built at one commit, only the module differing):

| | nothreads | threads |
|---|---|---|
| .wasm raw / gzip / brotli | +1.00 / +0.23 / +0.17 MiB (40.99 -> 41.99 raw) | +1.03 / +0.24 / +0.17 MiB |
| heap at startup and with 576 occluders active | no change (138 / 166 MB, heavy scene 286 / 412 MB) | no change |
| load to first `_ready` (5 loads each) | 5186 vs 5186 ms | — |
| matrix arm | 29 -> 8, parity 0.002, as native; 82/82 `--parity` and `--parity --baked` | (fixture exports nothreads) |
| scene smoketest | 23/23 Chrome, 23/23 Firefox | — |

`webgpu_tests/occlusion_bench/` (24x24-block city, 20k props with their own materials, uncapped, main-thread ms per
frame in a nothreads export = the whole engine frame):

| view | culling off | on (default 900 rays) | on, `rays=4096` |
|---|---|---|---|
| street (mostly occluded): ms / objects / fps | 7.85 / 6229 / 107 | 5.00 / 3334 / 147 | 3.03 / 1381 / 147 (off run: 8.87) |
| top (nothing to cull): ms | 19.0 | 21.1 (+2.1) | 21.7 (+1.7) |

The occlusion buffer is `rays per thread x WorkerThreadPool threads`, clamped to at least one ray per 32x32 px: a
nothreads export gets 900 rays (40x22 at 1280x720) where a 24-thread desktop gets 12 288, so it culls less than native
(street 6229 -> 3334 here, native 2329 -> 257 in the light scene) but costs less. A threaded export renders on its own
thread (§2.5); there the render thread's measured CPU time went 7.44 -> 2.36 ms on the street and 9.94 -> 10.54 ms on
top. The cost when nothing is occluded is the per-instance HZ test, ~0.1 us per object in frustum. GPU time was not the
limit in any case on this card (street 0.81 -> 0.35 ms).

Open: upstreaming the nothreads fix (stock Godot has the same bug; §3 trap 69).

### 4.39 Forward+ matrix to 82 arms (October 2026) *(work task 21)*

Merged `4f39eb0e58` (work/21: `fa048a8f22`). Eleven arms were added to `webgpu_tests/forward_plus/` (71 → 82), and
`UNCOVERED` shrank to what WebGPU cannot have: `multiview_xr`, `vrs`, `msaa_2x_8x`. New arms, native Vulkan delta
(every one within 0.004 of native under `--parity` on Chrome and Firefox alike; `lightmap_gi` off-frame 0.018):

| arm | native delta | what it renders |
|---|---:|---|
| `skeletal_animation` | 1.995 | the skin deforms (mesh now under its `Skeleton3D`) |
| `particle_trails` | 7.247 | own `GPUParticles3D`, `RibbonTrailMesh`, billboard Y to velocity |
| `lightmap_gi` | 12.62 | `lightmap_room.tscn`, baked by `bake_lightmap.sh` (the editor's Bake Lightmaps pressed by a throwaway plugin under Xvfb) |
| `fog_volume` / `fog_volume_shader` | 5.17 / 5.67 | `FogVolume`, with and without a fog shader |
| `stencil_outline` / `stencil_xray` | 0.957 / 3.70 | the stencil material modes |
| `bent_normal` | 0.675 | bent normal map |
| `smaa` | 0.243 | SMAA |
| `compositor_effect` | 20.95 | `fp_compositor_effect.gd`, GLSL compute on the colour buffer, compiled at run time |
| `occlusion_culling` | render info | an `ArrayOccluder3D` wall with 20 boxes behind it; native 29 → 8 objects, frame identical |

Harness additions: render-info counts per frame (`[FP] INFO` on the web, `<id>_<o>.info` natively) and the arm keys
`maxDelta` and `infoDrop`, so an arm can be judged on what was drawn rather than on pixels; `knownFail` (a documented
failure reports KNOWN, and one that starts passing reports XPASS and fails the run); `needsRaycast` (KNOWN only against
a template whose wasm lacks Embree, a normal arm otherwise, §4.38); `--feature a,b` runs a subset. Because the new
materials and shaders are built in GDScript at run time and can never be baked, the baked run's translated-stage
ceiling went 80 → 130 (measured 117); §2.39 has why that is looser, not fixed.

Results: 81/82 on the lead's full run 2026-10-26, unbaked and baked, with `occlusion_culling` KNOWN (web templates had
no culler); **82/82** unbaked and baked once the raycast module was in the templates (§4.38). The same day the parity
fixture's stale native shader cache was found to move native SDFGI by 7 points; `run_forward_plus.mjs` now deletes
`fp_check/.godot/shader_cache` before rendering the native reference (`8acda4dfa9`, §3 trap 68).

### 4.40 README rewritten, and the GitHub wiki (October 2026)

`README.md` was rewritten (`6b82ff35aa`, then `bb3745b71f`, `33955ddabf`, `508f4ed3b2`): what the fork does and how,
browser support, a native / WebGL / WebGPU compatibility table, rendering features, performance, web audio, engine
additions, Box3D, braced GDScript, building, testing, and links into the wiki. Its performance table sits between the
`<!-- perf-table:begin -->` / `<!-- perf-table:end -->` markers and is generated by
`webgpu_tests/benchmark/readme_perf_table.py --write` from a `run_ramp.sh` result: never hand-edit it, re-run the
benchmark and regenerate.

The wiki, https://github.com/Shane-Gadsby/godotwebgpu/wiki, is the user documentation: **Braced GDScript** (7 pages:
overview, language, formatting and conversion, editors, mixing and exporting, FAQ, internals), **Box3D Physics** (6:
overview, project settings, feature support, differences from Jolt, web and threading, internals) and **Engine
Additions** (12: overview, editor default folders, drag and drop onto lists, script editor input, web export options,
shader baking, threaded rendering on the web, web platform, web audio, MetaHuman importer, building, notable fixes),
plus `Home`, `_Sidebar` and `_Footer`. Its local clone is `/mnt/109313D2109313D2/godot-editors/godotwebgpu.wiki`, a
separate git repository (branch `master`), committed and pushed on its own: the user allows pushing the wiki; this
repository is pushed only when the user says so. A user-visible change in those three areas updates the wiki page too.
`docs/braced_gdscript/` stays the developer reference; facts the wiki's writer found missing there were added to it.

### 4.41 MetaHuman strand NaN and the bokeh DOF black hexagon (October 2026) *(work task 26)*

User report (2026-10-26): a black hexagon over Ava's face in camera-sim at exactly 4.80 m / 1.63–1.64 m, native Vulkan
on the 4080 too, with the user's own findings (the `eyelashes_strands` mesh, the DOF's `NaN × 0`, the clamp that hides
it, and an unproven value). Merged `0f0dfd50fe`.

- **Source, proven:** Ava's eyelash groom has a width of exactly 0 at every strand tip (1574 vertices; brows and scalp
  hair have none), so `v_coverage` is 0 there and interpolates to -1.07e-9 beside them; `pow(-1.07e-9, 0.5)` is NaN on
  NVIDIA and lavapipe. Proven by read-back, not a probe (§3 trap 70): a read-only `CompositorEffect` before DOF found one
  pixel, (621, 338), NaN in all four channels, so `ALPHA` was NaN, which rules out the anisotropic lighting; an additive
  same-vertex material found the -1.07e-9 fragment there, one of 92 overlapping lash fragments.
- **Fixes:** `HAIR_STRANDS` clamps `v_coverage` before `pow()`; the importer's opaque and hashed variants replace a
  shared `HAIR_STRANDS_ALPHA_LINE` and print an error if it stops matching; `HAIR_SHELL`'s binormal normalize has a
  floor. The bokeh DOF's `dof_finite()` (`bokeh_dof_inc.glsl`) counts a non-finite color as black, tested on the
  exponent bits with a select: once per pixel in Forward+'s compute `GEN_BLUR_SIZE`, per tap in Mobile's raster passes;
  about 4 ALU ops each, finite frames bit-identical. A deliberate divergence from upstream (an upstream sync may
  conflict in `bokeh_dof*.glsl`).
- **Numbers:** the user's 42-position sweep (4.70–4.90 m × 1.55–1.75 m), native: 3 failures before, 0 after; with the
  DOF fix alone the NaN is still at the same 3 positions but no black reaches the face. WebGPU (Chrome, real adapter,
  Forward+): 0 before and after (the NaN does not occur there).
- **Tests:** `webgpu_tests/dof_nan/run.sh` (Forward+ compute and Mobile raster, every bokeh mode: 18/24 failed before,
  24/24 after; lead rerun 0 failures), Forward+ matrix arm `dof_nan` (84 arms with §4.42's; WebGPU FAIL 1.068 → PASS 0.001, parity
  0.003), `webgpu_tests/metahuman/nan_check.gd` in `run.sh` (lead rerun: verify and nan 0 failures, after a fresh
  import, §3 trap 71). Smoke on the merged tree: 276 modules, 0 invalid.
- **Open:** glow, SSR and auto-exposure would still spread a NaN; whether to guard them too is the user's call (default
  no, until a report). The user's camera-sim needs a **Reimport** of `scenes/Ava/head.dna` for the strand clamp; the
  DOF fix alone already prevents the hexagon.

### 4.42 Camera white balance on CameraAttributes (October 2026) *(work task 24)*

User request (2026-10-26): a White Balance attribute on `CameraAttributesPhysical` and `CameraAttributesPractical`, as on
a real camera. Merged `449f085aa6`. Added on the shared base `CameraAttributes` (both inherit it, and exposure already
lives there): `white_balance_temperature` (Kelvin, 2000–10000, default 6500) and `white_balance_tint` (-150..150,
default 0), plus `RenderingServer.camera_attributes_set_white_balance(rid, temperature, tint)`. User wiki page: Engine
Additions → Camera White Balance.

- **Model** (`camera_attributes_storage.cpp`): camera semantics, the temperature is the illuminant the camera is set
  for. Illuminant on the Planckian locus with the Krystek approximation `Light3D` already uses for
  `light_temperature`, tint along the isotherm at 1/3000 Duv per unit, so ±150 is ±0.05 Duv (the extent of Unreal's
  `WhiteTint` at ±1; Lightroom-style numbers; positive = magenta image, as in Lightroom), von Kries adaptation in Bradford cone space onto the same model's 6500 K / tint 0 white. So
  the default is exactly the identity, and a light of temperature T under a white balance of T renders exactly like the
  default 6500 K light.
- **Plumbing:** `RendererCameraAttributes::white_balance_compute_gains()` computes three cone gains on the CPU; the
  shaders apply linear sRGB → LMS, the gains, LMS → linear sRGB with constant matrices, so only 12 bytes cross. Forced
  by the Forward+ tonemap push constant, 112 bytes against Vulkan's 128-byte guarantee: a 3x3 does not fit, the gains
  do (**now exactly 128, no headroom**, with a `static_assert`). Applied in linear scene-referred color after exposure,
  FXAA and pre-tonemap glow, before tonemapping; the softlight glow is balanced on its own. Skipped at exactly 6500 K /
  0: Forward+ push-constant flag bit 7; Mobile spec constant 18 `use_white_balance` (`TONEMAP_MOBILE_ADRENO_BUG` moved
  to bit 19); Compatibility a `vec4` in the Tonemap UBO behind a uniform branch, at both exposure sites in `scene.glsl`
  and in `sky.glsl`.
- **Interface references regenerated on purpose:** the only change is override id 18 in the `tonemap_mobile` fragment
  variants (lead check: the scan reference's 1,743-line diff is renumbering; per module, only that fact is new).
- **Verified:** default bit-identical to the pre-change editor in all three renderers on the 4080 SUPER and lavapipe
  (0 of 57,600 pixels); 3200 / 10000 K and ±75 tint (±0.5 before task 29) within 1/255 of the model in all three and on WebGPU; WebGPU
  frames pixel-identical to native Vulkan; Forward+ arm `white_balance` (3200 K, tint 30; tint 0.2 before task 29, the same frame); smoke 276/276 on the merge.
- **Known gap:** Compatibility does not white balance a `BG_CANVAS` background (copied with exposure only).
- **User decisions** on the hand-offs page (`wb-tint-units`, `wb-model`, `wb-range`, `look-white-balance`): first
  shipped with the defaults (-1..1 at 0.05 Duv, Planckian everywhere, 1000–15000 K); then the user picked
  (2026-10-26) Planckian everywhere, **2000–10000 K** and tint **-150..150** keeping the physical extent (±150 = ±0.05
  Duv, 1/3000 Duv per unit), and judged the look "like a camera". Work task 29 (merged `add69b409d`) changed the
  inspector hints, the clamp in `white_balance_compute_gains()`, the scale constant `WHITE_BALANCE_DUV_PER_TINT`, the
  class docs and the Forward+ arm (tint 0.2 → 30, frame unchanged: 0 of 230,400 px). The old units were never
  released, so nothing converts them.

### 4.43 Bake isolation: the baker's capability answers stay inside the baker (October 2026) *(work task 27)*

User report (2026-10-26): exporting camera-sim for the web with FSR 2 on logged "Mismatch uniform type ... Expected
'Image', supplied: 'StorageBuffer'" for four FSR 2 passes, over and over. Proven to be the *editor's* frames during the
bake, not the web runtime (§3 trap 73): reproduced exactly (same four messages, same "left out 4455 shader group(s)")
with the editor of the time on a scratch copy of camera-sim. Merged `756b99bf6f`.

- `RenderingDevice::has_feature()` applies the bake's target answers only inside `RD::ShaderBakeFeatureOverrideScope`
  (per thread; opened in `_begin_customize_resources`, `_customize_resource`, `_customize_scene`). Outside a bake it
  costs one thread-local read. `shader_bake_feature_override_is_active()` still means "a bake is running".
- FSR 2's capability variants became shader groups with bake requirements (four groups for depth clip, reconstruct and
  lock: atomics × FP16; two for the luminance pyramid; FP16 groups for accumulate, autogen reactive and TCR autogen),
  only the selected one enabled; `FSR2Effect::Device::use_image_atomics` records the answer the resources and bindings
  follow. The WGSL of the groups WebGPU enables is byte-identical, so the precompiled table and stored references are
  unchanged. Checked for the same split everywhere else that reads `has_feature()` (list in the task's `for-tasks.md`):
  none.
- Numbers: `bake_isolation/run.sh` FAIL on the old editor (4 mismatches, 7 FSR 2 stages in the editor's flavor), PASS on
  the merge (8 editor frames drawn during the bake, 0 errors, 8/8 FSR 2 passes baked for WebGPU); fixture export FSR 2
  cache misses 7 → 0 and stages translated at load 2 → 0; camera-sim (dlink nothreads) 64 mismatches → 0, 1275 baked,
  0 translated in Chrome and Firefox; `--parity --feature fsr2,taa` PASS unbaked and baked (fsr2 0.142, taa 0.006, as
  before); smoke 276/276.
- New tier `webgpu_tests/bake_isolation/run.sh` (~1 min, Xvfb + Vulkan, no browser). CI runners may lack Vulkan under
  Xvfb.
- Note for the user: camera-sim's `project.godot` lost FSR 2 (`scaling_3d/mode=2`) at 16:56 the same day, presumably
  because of these errors; with this fix it can go back on.

### 4.44 NaN guards for glow, SSR and auto exposure (October 2026) *(work task 28)*

User decision 2026-10-26 (hand-offs `nan-guards`: "Guard all three now"), the follow-up §4.41 left open. Merged
`287109ddfd`. The bokeh
DOF's guard moved to a shared include, `servers/rendering/renderer_rd/shaders/effects/finite_inc.glsl`
(`finite_or_zero()` / `finite_or_zero3()` / `finite_or_zero4()`, `is_non_finite*()`; distinct names because WGSL has no
overloads), and every effect that reads the scene color now passes it through once, where it is read:

- **Glow:** Forward+ `copy.glsl` `MODE_GLOW` first pass, the four `local_cache` loads per thread (inside the existing
  `FLAG_GLOW_FIRST_PASS` branch); Mobile `blur_raster.glsl` `MODE_GLOW_GATHER`, its four samples. Later levels read
  that output.
- **Auto exposure:** `luminance_reduce.glsl` `READ_TEXTURE` (once per pixel) and `luminance_reduce_raster.glsl`
  `FIRST_PASS` (once per texel read). The raster path only runs where storage is unavailable; Mobile does not
  apply auto exposure at all (its own warning), so it is guarded but not observable there.
- **SSR:** its input is `RB_LAST_FRAME`, made by `copy_to_rect(..., sanitize)` and `make_mipmap()`, which upstream
  already sanitized: inf (|x| > 3e10) and NaN (`notEqual(color, color)`) to 100. **That NaN test never fired on the
  4080** although glslang emits the correct `OpFUnordNotEqual` (the driver assumes no NaN). It is now
  `finite_or_zero4()`: NaN counts as black, infinity is still 100 from upstream's line. The SSR shaders themselves are
  unchanged. Side effect: every other `make_mipmap()` user (screen-texture mips for refraction/`SCREEN_TEXTURE`,
  runtime texture mip generation) now turns a NaN into 0 instead of (in intent) 100, and actually catches it.
- **Bokeh DOF** uses the shared helper; same code as before (`dof_finite()` is gone).

Numbers (4080 SUPER, native Vulkan and WebGPU Chrome real adapter; before = base `2eca8b0cf5` built in the same tree):

| Check | Before | After |
|---|---|---|
| `dof_nan/run.sh` Forward+ glow additive / softlight (pixels changed) | 59,791 / 6,434 (a blob at the luminance cap: NVIDIA's `min()` drops the NaN) | 4 / 4 |
| `dof_nan/run.sh` Mobile glow additive / softlight | 1,591 / 200 | 4 / 4 |
| `dof_nan/run.sh` SSR roughness 0 / 0.4 (black pixels) | 212 / 23,812 (the second after the first's NaN persisted) | 4 / 4 |
| `dof_nan/run.sh` auto exposure, Forward+ (mean brightness) | 0.4001 → 0.0000, whole frame | 0.4001 → 0.4001 |
| Tier total | 7 failures | 0 (all 24 DOF cases still pass) |
| Matrix `glow_nan` / `ssr_nan` / `auto_exposure_nan`, native delta | 1.261 / 28.684 / 97.513 | 0.005 / 0.004 / 0.003 |
| Same arms, WebGPU (unbaked) | FAIL 1.261 / 28.685 / 97.514 | PASS 0.005 / 0.004 / 0.003, parity ≤ 0.003 (baked too) |

Finite frames bit-identical natively: `glow`, `glow_map`, `ssr`, `ssil`, `refraction`, `auto_exposure`, `dof_far`,
`dof_near`, `dof_nan` and the off frames of the new arms, 0 differing pixels before vs after. WebGPU `--parity` on
the 10 guarded/new arms passes unbaked and baked (translated 133, under the 150 gate). Interface references unchanged
(smoke: 276/276 identical, 552/552 scan facts, 0 invalid).

Cost: an AND, a compare and a select per component (12 ALU ops per vec4): Forward+ glow first pass 48 per thread at
half resolution (~12 per scene pixel), Mobile gather 48 per quarter-resolution fragment, auto exposure 9 per pixel,
the last-frame copy +4 per pixel over upstream's test. Once per frame each; well under a microsecond at 1080p on the
4080, not separable from noise, so not measured with `gpu_time.mjs`.

Tests: `webgpu_tests/dof_nan/run.sh` now covers glow (additive, softlight), SSR (roughness 0, 0.4; Forward+ only, a
reported SKIP on Mobile) and auto exposure as well (`ONLY=dof,glow,ssr,exposure` narrows), gated also on changed
pixels (glow ≤ 64, SSR ≤ 256) and auto exposure on mean brightness (≤ 2 %). Forward+ matrix arms `glow_nan`,
`ssr_nan`, `auto_exposure_nan` (`maxDelta` 0.05; 87 arms).

### 4.45 The editor's update check looks at this fork's releases (October 2026) *(work task 30)*

User request (2026-10-26): "Check my fork's releases for the 'new version' checks". Merged `4b4bd00e62`.

`EngineUpdateLabel` (`editor/project_manager/engine_update_label.{h,cpp}`, the project manager's "Update available"
link) asked `godotengine.org/versions.json` and compared stock Godot's versions with 4.8.dev, which told this fork's
users about the wrong product. It now asks `api.github.com/repos/Shane-Gadsby/godotwebgpu/releases`
(`EngineUpdateLabel::FORK_RELEASES_REPO`; an empty string restores upstream's check, whose code is left intact).
Every change is tagged `// WebGPU fork`. User decisions pending on the hand-offs page (`update-newer-rule`, default
commit date, implemented; `update-repo`, default `Shane-Gadsby/godotwebgpu`).

- **Which release:** published releases only (drafts never; the unauthenticated API does not list them anyway).
  Prereleases only under *Check Newest Preview* (and *Auto* on a non-stable build, which this fork always is: 4.8.dev).
  The engine version is read from the release's asset names (`godot-webgpu-export-templates-<v>.tpz`,
  `godot-webgpu-editor-<v>-<os>.zip`): another major version is never offered, *Check Newest Patch* keeps the minor
  version, a release whose assets name none is not filtered. Newest by `published_at`.
- **Newer how:** the release's commit comes from its `main-<sha>` tag (or a full SHA in `target_commitish`). If it is
  the build's own `GODOT_VERSION_HASH`, the build is up to date. Otherwise a second request
  (`/repos/.../commits/<sha>`) gets the release commit's committer date, which is compared with
  `GODOT_VERSION_TIMESTAMP` (the build commit's committer date, `methods.py`): later = update available. An unknown
  time (a build without git) never reports an update. Chosen because releases carry no machine-readable fork version
  (it is only in the hand-written title) and the binary has none, so a version compare would need `version.py`, the
  release workflow and the naming habit to change; the commit date needs nothing new and is right for every case
  that matters (older build: told; the release's own build: not told; a later webgpu-4.8 build: not told). Its one
  blind spot: a build from a branch committed after the release but not containing it (webgpu-4.8 before a fix
  only on `main`) is not told. Two requests per check against GitHub's 60/hour unauthenticated limit.
- **The label** shows the release title without the "Godot WebGPU " prefix ("Update available: 4.8.dev 1.5.1 - OSX
  fix.") and opens the release's GitHub page. Network mode, offline label, error/retry, settings are unchanged.
- **Testing:** `tests/editor/project_manager/test_engine_update_label.cpp` (3 cases, `--test --test-case="*EngineUpdateLabel*"`).
  A dev build (`DEV_ENABLED`) reads `GODOT_UPDATE_CHECK_AS=<hash>:<commit UNIX time>` to pose as another build
  against the live API, so the label's states can be seen without building an old commit; release editors ignore it.

### 4.46 CI: `main` green after the 2026-10-26 merges (October 2026)

Asked for (user, 2026-10-26): find why PRs to `main` fail and fix everything until it passes, pushing `webgpu-4.8` and
merging to `main` directly, with the 🧪 WebGPU Tests runs those PRs start cancelled to save runner time.

- **#20's merge, 📊 Static checks failed** (run 40311774904), so no build ran. Fixed in #21 (`efa411b92a`): six
  shebang scripts made executable; ruff in `webgpu_tests/benchmark/readme_perf_table.py` (a regex with backslashes
  inside an f-string expression, moved into a variable, same output) and `sharpness.py` (variable `l`), plus ruff
  format; codespell and file-format fixes in `webgpu_notes/work/`; clang-format of
  `webgpu_texture3d_array_inc.glsl` (macro continuation spacing only). §3 trap 75.
- **#21's merge, the GCC sanitizer editor failed to link** (run 40315006599; every other job green): the SCU build
  never compiled `tests/editor/project_manager/test_engine_update_label.cpp`. Fixed in #22 (`f11d75f5d4`) by adding
  `"/editor/project_manager"` to the tests unit in `scu_builders.py`. §3 trap 74.
- **#22's merge: all green** (run 40321431357), release drafted as `main-41def1729690f515f19130b4a4dc099b80c80416`.
  The #21 run drafted `main-7509fee2ab8bd18b08a07075771712d621ad9fa9` too, since `draft-release` does not wait for the
  sanitizer editors (#20's design); its artifacts are the same.
- The 🧪 WebGPU Tests runs for #20 (a repeat), #21 and #22 were cancelled by request; the last complete one passed on
  #20 earlier the same day. Neither fix changes anything that workflow exercises (formatting, a benchmark
  report script, the SCU folder list).

### 4.47 Camera grain on CameraAttributes (October 2026)

User request (2026-10-28): noise grain as a practical and physical camera attribute, realistic, correct at every game
resolution, with a strength control and a type selector defaulting to the most realistic type. On the shared base
`CameraAttributes`: `grain_enabled` (off by default), `grain_type` (`GRAIN_TYPE_FILM` default, `_FILM_MONOCHROME`,
`_DIGITAL`), `grain_intensity` and `grain_size` multipliers; the ISO is `exposure_sensitivity`, now shown in the
inspector while grain is on even without physical light units. `RenderingServer.camera_attributes_set_grain()`.

- **Model** (`camera_attributes_storage.cpp`, comment block above `camera_attributes_set_grain()`): a 36x24 mm frame
  as in `CameraAttributesPhysical`. Film: Kodak diffuse RMS granularity fitted as `8 * (ISO/100)^0.23` (T-Max
  100/400/P3200, Portra 400), sqrt(density) growth around D = 1 at middle gray, 6 um clumps growing with
  `(ISO/100)^0.4`, density noise applied as stops of exposure through the negative's gamma 0.6 *before* the tonemapper
  (the tonemapper plays the print); color film is 65% shared + 35% per-layer noise (a look choice, documented as
  such). Digital: 6 um photosites, 1400 e-/um² at ISO 100, scene 1.0 = saturation, shot + read noise (1.5 e- pre-gain,
  8 e- post-gain), Bayer weights 2 / sqrt(2) / 2, auto exposure treated as gain (auto ISO).
- **Resolution:** Selwyn's law. The shader gets the grain's own amplitude and its cell size in output pixels: cells
  below a pixel give white noise scaled by the cell size, larger ones cellular noise (nearest of a 3x3 jittered
  lattice). A first version interpolated a lattice and renormalized per pixel; that made per-pixel variance exact but
  inflated low-frequency power ~30% once cells passed a pixel, i.e. a 4K image looked grainier than a 1080p one.
  Measured after the fix (numpy port and native renders box-downsampled to a common grid): within ~10% from 360p to
  2160p, and 720p vs 1440p->720p within a few percent per luminance band in all three renderers.
- **Plumbing:** one packed `uint` (type, log cell size, two log amplitudes, 8-bit frame seed from
  `get_frame_number()`; layout in `camera_grain_inc.glsl`), because the Forward+ tonemap push constant was exactly 128
  bytes after §4.42: it takes the former `pad`. Mobile push constant 80 -> 96 bytes; Compatibility a `uint` in the
  Tonemap UBO, applied in `effects/post.glsl` (a copy of the include in `drivers/gles3/shaders/effects/`), and grain
  forces the post pass. Zero means off: a uniform branch, no new flags, spec constants or variants, so the interface
  references are unchanged (smoke 276/276, 552/552, 0 invalid). Not drawn in debug draw modes or reflection probes.
- **Verified:** native Forward+ (4080 SUPER, Vulkan), Mobile and Compatibility (llvmpipe) on a -7..+5 stop ramp and a lit
  scene; WebGPU Forward+ in Chrome (lovelace, 48 textures) matches native per band within 0.02 code values with a
  pixel-identical no-grain baseline. No Forward+ matrix arm: the grain is random per frame, so the parity check cannot
  compare it.
- **Known limits:** with FSR 1 or SMAA the grain is drawn at the 3D resolution and upscaled; Compatibility's post pass
  loses a little deep-shadow detail compared with its inline path (the same as glow, SSAO or adjustments); Mobile and
  Compatibility have no auto exposure, so no auto-ISO noise there. The seed repeats every 256 frames.

### 4.48 Camera LUTs on CameraAttributes, and a .cube importer (October 2026)

User request (2026-10-28): camera LUTs as a physical and practical camera attribute. Decisions (Hand-offs board
`lut-input`, `lut-cube-import`, `lut-with-env`): both display and camera-log inputs, selectable; a `.cube` importer;
with an Environment LUT too, both apply, camera first. On the shared base `CameraAttributes`: `lut_enabled`,
`lut_texture` (Texture3D), `lut_input` (`LUT_INPUT_DISPLAY` default, `_ARRI_LOGC3`, `_SONY_SLOG3`, `_PANASONIC_VLOG`,
`_CANON_LOG3`, `_RED_LOG3G10`, `_ACESCCT`), `lut_strength`; `RenderingServer.camera_attributes_set_lut()`.

- **Model** (`camera_lut_inc.glsl`): a display LUT takes the tonemapped image sRGB-encoded, after tonemapping and
  post-tonemap glow, before the Environment's adjustments and color correction. A log input converts scene-linear
  Rec.709 to the camera's gamut and log curve (code values over 0-1, full range) and the LUT replaces the tonemapper,
  for the scene and the softlight glow alike. LUTs are sampled at texel centers (input 0 and 1 on the first and last
  entries, as .cube defines them; the Environment's own LUT path does not). Gamut matrices come from colour-science
  0.4.6 (Bradford to the ACES white for ACEScct); all six curves were checked against its encodings (exact; Canon Log 3
  is v1.2: the v1 coefficients are 0.12 off). Strength blends with the tonemapped image in sRGB-encoded values.
- **Plumbing:** Forward+ packs enable, input and strength into push-constant `flags` bits 8-23 (the push constant has
  no room left) and binds the LUT at set 3 binding 1; Mobile a `uint` in the former padding and set 0 binding 4;
  Compatibility a `uint` in the Tonemap UBO, `USE_CAMERA_LUT` in `effects/post.glsl` on texture unit 4, and the LUT
  forces the post pass. The renderers bind the default 3D texture when there is no LUT and only enable it for a real
  3D texture. **Interface references regenerated on purpose:** the only change is the new sampler + `texture_3d`
  pair in the four `tonemap` and two `tonemap_mobile` fragment variants (scan reference diff is additions plus
  renumbering).
- **`.cube` importer** (`ResourceImporterCubeLUT`, `editor/import/resource_importer_cube_lut.cpp`): 3D, 1D, and a
  1D shaper followed by a 3D table; `DOMAIN_MIN/MAX`, Resolve's `LUT_1D/3D_INPUT_RANGE`. A 3D table over 0-1 is kept
  exactly; anything else is evaluated on a grid (max(3D size, 33), 65 for 1D). Written straight to a
  `CompressedTexture3D` file, uncompressed RGBA half floats: a first version saved an `ImageTexture3D`, which saves by
  reading its data back from the RenderingServer, so a `--headless` import wrote an empty texture, and three files
  imported on worker threads hung the import. Unit tests in `tests/editor/import/` (folder added to the SCU tests
  unit, trap 74); not run here (needs a `tests=yes` editor).
- **Verified:** an identity LUT under each log input shows the encoding itself, compared with colour-science on a
  7-hue x 12-stop grid; display inverts (3D and 1D) against 1 - sRGB. Forward+ native (4080 SUPER, Vulkan): all
  within 0.61/255. Mobile: within 2.6/255 up to scene value 1.44; above ~2.0 its RGB10A2 scene buffer clips before the
  LUT, so log LUTs lose the brightest highlights on Mobile. Compatibility (llvmpipe), against its own post-pass
  baseline: log within 2.4/255, display within 5/255 (its approximate sRGB encode, shared with its Environment LUT).
  WebGPU Forward+ in Chrome (lovelace, 48 textures): pixel-identical to native Vulkan in every case. Smoke after the
  reference regeneration: 276/276, 552/552, 0 invalid.
- **Known limits:** display LUTs are SDR (HDR output clips at 1.0); LUT output is treated as sRGB, not BT.1886;
  the Mobile highlight clip above; `.cube` files with both tables and `DOMAIN_*` apply the domain to the 1D table.

### 4.49 Camera3D autofocus and handheld shake (October 2026) *(work task 38, camera batch)*

Camera realism batch (work board `camera-batch.md`, tasks 33-38). Scene side only (`scene/3d/camera_3d.*`, no renderer
change), merged `c2d0987359`. **Autofocus:** Manual / Single (`autofocus_trigger()`) / Continuous, on a tracked Node3D
or the nearest physics-ray hit of normalized screen points (collision mask); a servo in diopters with a time constant
(exact per-frame solution, frame-rate independent) and optional hunting overshoot; drives
`CameraAttributesPhysical.frustum_focus_distance`, or moves CameraAttributesPractical's near/far distances and
transitions keeping the band's diopter width; signals `focus_acquired` / `focus_failed`; runs in the editor preview,
with editor-save hooks that store the user's value. **Handheld shake:** seeded band-limited sinusoid sums per axis
(rotation, position, breathing) with exact RMS and spectrum; stabilization removes the set fraction above ~0.5 Hz
(first-order high-pass per partial); optional focal-length-referenced amplitudes; applied in
`_get_adjusted_camera_transform` so `transform` is untouched while `get_camera_transform()` and picking include it.
Verified (lead, merged tree): `results/38-camera3d-af-stabilization/tests/run.sh` OK at 30/60/144 fps (AF error after
one time constant 0.3679 = e^-1; shake RMS within 0.6%, spectrum within 0.7%, IS model within 0.0002), the windowed
editor check, and shake-off renders pixel-identical to the pre-merge editor in Forward+, Mobile and Compatibility; smoke
276/276, 552/552. Limits: AF writes into the attributes resource (cameras sharing it share focus); ray points not with
`physics/3d/run_on_separate_thread`; shake with physics interpolation untested. User answer `shake-in-editor`: shake
in the editor only behind a tick box, default off (`shake_visible_in_editor`, §4.55). §3 trap 78.

### 4.50 Camera finish pass: in-camera noise reduction and sharpening; Camera3D monitoring overlays (October 2026) *(work task 37, camera batch)*

Merged `ffda7ef1d3`. A **camera finish pass** after the 3D image is complete (after tonemapping, FSR/SMAA, debug draw and
compositor effects), at output resolution, before 2D; skipped entirely when nothing in it is on.
- **CameraAttributes:** `noise_reduction_{enabled,strength,chroma_only}` (bilateral on display Y'CbCr, Tomasi-Manduchi
  1998; chroma range sigma 2x luma's) and `sharpening_{enabled,amount,radius,halo}` (unsharp mask on luma, radius in
  pixels of a 1080-line frame, halo = CAS-style clamp to the 3x3 range). Model in `camera_attributes_storage.cpp`
  ("Camera finish").
- **Camera3D, Monitoring group:** `monitoring_visible_in_output`, zebras (95 IRE; optional second zebra as a ±5 IRE
  window at 70), focus peaking (Sobel on display luma), false color (ARRI ALEXA bands), histogram (luma/RGB, 256 bins,
  GPU compute). Sent to the server only in the editor or with Visible in Output, so a game with editor-only overlays
  runs nothing; `CameraData::monitoring` carries them to the renderer.
- RD `effects/camera_finish.*`, `camera_finish.glsl` + `camera_histogram.glsl`; Compatibility `GLES3::CameraFinish`
  (everything but the histogram: no compute). Single-view only (XR skipped).
- **Verified (lead, merged tree, all three renderers):** default 0 px vs the pre-merge editor; zebra coverage = fraction
  above level (0.0470/0.0469); false color 100% of pixels on the band table; histogram bars exact vs a CPU histogram
  (Forward+, Mobile); peaking IoU 1.0 vs CPU Sobel (0.997 Compatibility, which has no DOF); NR at ISO 6400 luma sigma
  5.6 -> 1.95 at strength 1, chroma-only leaves luma; sharpening overshoot within 0.6/255 at 1080p, halo width doubles at
  2160p (peak ~10% under a dense Gaussian there: taps stride 2 px, documented); Visible in Output off = 0 px in a game
  run, overlays drawn in an editor run. Worker: WebGPU pixel-identical to native except the animated zebra phase.
- Not yet: a by-eye check in the real editor (Preview check box, inspector preview); questions `zebra-colors`,
  `zebra2-window`, `peaking-source` on the hand-offs page (defaults built).

### 4.51 Camera motion blur from the shutter, and rolling shutter (October 2026) *(work task 34, camera batch)*

Merged `4a96bd2ae6`. CameraAttributes `motion_blur_*` (enabled, shutter_angle 180°, strength, max_length 0.05 of frame
height) and `rolling_shutter_*` (enabled, readout_time 20 ms); `CameraAttributesPhysical.motion_blur_use_shutter_speed`
(default on) takes the exposure time from `exposure_shutter_speed`. ProjectSettings
`rendering/camera/motion_blur/motion_blur_quality` (8/16/32 taps). Blur = motion per frame x exposure time / frame
time; rolling shutter shows row y at readout x (y/H - 0.5). Pass `effects/camera_motion_blur.*` (own push constant):
prep (velocity + depth; the sky gets camera motion from depth) -> tile max -> neighbor max -> gather (rolling shutter by
fixed-point iteration, then McGuire et al. 2012's reconstruction with Jimenez 2014's weights). After DOF, before auto
exposure and glow; Forward+ turns motion vectors on (and resolves them under MSAA) whenever it is on. **Forward+ only**
(desktop and web); Mobile and Compatibility ignore it (documented). Two fixes to McGuire's published filter, found by
measuring: its weights stepped a moving edge (0.39 -> 0.87 instead of a ramp through 0.5) and its tap spacing shortened
every blur to S/(S+1). Verified: blur length within 0.3% of speed x exposure at 720p/1080p (worker; lead reproduced
0.9997 at 1080p 1/50 s), skew within 1.1% geometry / 0.5% sky (lead: sky 0.995), WebGPU 1.000 at 165 Hz, MSAA 0.998;
off bit-identical in all three renderers (lead, merged tree). Trap for measuring it: the test square wraps around the
frame, and a capture whose exposure spans the wrap measures short (frames=60 at 1/50 s read 0.82). User answer
`motion-blur-editor`: in the editor only behind tick boxes, default off (`motion_blur_visible_in_editor`,
`rolling_shutter_visible_in_editor`, §4.55).

### 4.52 CameraAttributes lens optics and the CameraEffects uniform buffer (October 2026) *(work task 33, camera batch)*

Merged `779120b64e` (conflicts with 34/37 resolved by applying task 33's diff onto the merged side and checking it adds
and removes exactly task 33's lines). Sensor formats (replacing the hard-coded 36x24; grain now uses the sensor
height), physical vignetting (cos⁴ + pupil overlap; practical strength/falloff), Brown-Conrady distortion (automatic per
focal length, auto-crop), lateral chromatic aberration, anamorphic squeeze (Camera3D keeps the horizontal FOV), ND
filters and T-stops, Airy diffraction softening and focus breathing; Forward+, Mobile, Compatibility and WebGPU.
- **CameraEffects uniform buffer** (`camera_effects_inc.glsl`, `CameraEffectsUBO` in `camera_attributes_storage.h`):
  std140, 512 bytes: `uvec4 header` (x lens bits, y glare bits, z exposure-automation bits, w free), `vec4 lens[8]`,
  `glare[6]` (task 35), `exposure[2]` (task 36), `reserved[15]`. Forward+ tonemap set 1 binding 1, Mobile set 0 binding
  5, Compatibility UBO binding 16 behind `USE_CAMERA_EFFECTS`. Filled per pass by
  `RendererCameraAttributes::camera_attributes_fill_effects_buffer()` (`_fill_effects_lens/_glare/_exposure`), owned
  per viewport (`CameraEffectsBufferRD`), uploaded only when it changes; a shared all-zero buffer when nothing is on;
  Mobile skips the subpass and Compatibility forces the post pass when in use. **New users of it add their bits to
  `camera_attributes_uses_effects_buffer()`.**
- Order in the pass: lens resample (distortion, CA, diffraction) at the first read, FXAA following the lens, glow read
  where the lens maps the pixel, vignetting after pre-tonemap glow, then white balance, grain, tonemapping, LUT.
- Fixed on the way: Compatibility's fast-sRGB round trip was off by up to 6.8/255 near black (exact inverse now).
- **Verified (lead, merged tree):** defaults pixel-identical to the pre-merge editor in 9/9 renders (3 attribute kinds x
  3 renderers); vignetting vs cos⁴ x pupil overlap within ~0.01; distortion dots within 0.035 px of Brown-Conrady
  (0.12 px Compatibility) over displacements up to 34 px; CA spread/radius 0.00500 at 540p and 1080p; Airy sigma
  2.455 px vs 2.465 at 4K (Compatibility ~5% narrower, documented). Worker: FOV for all 7 formats within 2e-6°, WebGPU
  pixel-identical to native on 8 cases. Interface reference: the UBO in the six tonemap fragment variants only.
- Not tested: ND/T-stop on screen, MSAA, XR, scaled viewports. Follow-ups: a Forward+ matrix arm `lens_optics`;
  longitudinal CA in the DOF pass. Questions `breathing-default`, `ca-aperture` on the hand-offs page.

### 4.53 Auto exposure metering modes and auto white balance (October 2026) *(work task 36, camera batch)*

Merged (no conflicts). CameraAttributes (both kinds) `auto_exposure_metering_mode`: Average (default, untouched),
Center-weighted (Nikon's 75% of the weight in a 12 mm circle), Spot (size in % of frame, UV point), Highlight-weighted
(a percentile exposed to 1.0); auto white balance (speed, temperature range; manual temperature/tint stay on top as a
bias, neutral = display white). Both on the GPU without readback in a new `CameraMetering` effect
(`camera_metering.glsl`, 7 compute variants); the AWB gains reach the Forward+ tonemapper at set 1 binding 2, gated by
`header.z` bit 1. Forward+ only (auto exposure already was); Mobile and Compatibility keep Average and manual white
balance, documented, so `camera_attributes_uses_effects_buffer()` is not extended. Metering uses the arithmetic weighted
mean (like Average), not a log-average. **Upstream bug found, not fixed:** Godot's Average reduction drops the right 20%
of a 1080p frame (and the bottom 27% at 720p) because each level rounds its size down; left as is so defaults stay
identical. **Verified (lead, merged tree):** defaults 18/18 renders pixel-identical in all three renderers; center and
spot (center and off-center) within 0.0005 EV of a Python reference from the AE-off HDR frame at 1080p and 720p,
highlight within 0.03 EV. Worker: AWB brings a gray card under 3200 K from r/g 1.94, b/g 0.38 to 1.0000/1.0002;
WebGPU pixel-identical (metering) / within 1/255 (AWB); +0.05 ms per feature at 1080p. Questions `metering-statistic`,
`average-dropped-edges`, `awb-neutral` on the hand-offs page.

### 4.54 Bokeh shapes, tilt-shift, starburst, halation; Environment lens flare, veiling glare, diffusion (October 2026) *(work task 35, camera batch)*

Merged `ec882a87c2` (conflicts with task 36 resolved by applying task 35's diff onto the merged side, checked to add and
remove exactly its lines; `tone_mapper.cpp` by hand, `CameraAttributes.xml` element by element).
- **CameraAttributes:** aperture blades (count, curvature, rotation; override the project's bokeh shape per camera),
  cat's eye bokeh, starburst (2 spikes per blade for odd counts, 1 for even, stronger stopped down), halation.
  **CameraAttributesPhysical:** tilt-shift (Scheimpflug hinge rule: each pixel's own focus distance in the DOF).
- **Environment:** lens flare (ghosts on the line through the center), veiling glare, diffusion filter (+ black variant).
- **Glare chain:** an unthresholded RGBA16F mip chain built with glow's passes, bound to the tonemap pass (Forward+ set 2
  binding 2, Mobile set 0 binding 6); parameters in the CameraEffects `glare[6]` region / `header.y`. Bokeh push
  constant now exactly 128 bytes. Compatibility gets none of it (no DOF; its post pass sees light clipped near white),
  documented. Mobile: RGB10A2 clips highlights, so glare from bright sources is ~20x weaker.
- **Verified:** worker (native vs independent models): blade vertex angles within 0.1-3°, cat's eye ratios within ~0.04
  of the two-circle model, tilt band at row 339 vs 344.5 predicted, spikes 6/14/8 for 6/7/8 blades, flare ghosts within
  1.6 px of the line, veiling glare 0.00212 vs 0.00212, WebGPU repeats native (halation ~12% tighter, spikes ~30%
  brighter: Chrome's filtering). Lead (merged tree): defaults 9/9 identical; halation sigma 13.47 / 54.17 px vs 13.44 /
  53.76 predicted, no blue; diffusion leaves a flat field unchanged. A `-Wenum-conversion` in
  `_glare_requested_bits()` fixed at integration (would have failed werror).
- **Upstream problem found, not fixed:** Mobile's DOF ignores CameraAttributesPhysical (`bokeh_dof_raster()` never sets
  the physical flags), so a physical camera on Mobile has no DOF; fixing it changes existing Mobile renders.
- Not run: Practical-blur bokeh shapes and cat's eye on Mobile; GPU cost (the starburst is the expensive part).
  Questions `starburst-default-strength`, `halation-min-size`, look `look-bokeh-glare` on the hand-offs page.

### 4.55 Camera follow-ups from the user's answers; handheld shake presets (October 2026) *(work task 39, camera batch)*

Merged. From the hand-offs page: **zebra colors user-selectable** (`monitoring_zebras_color` default black,
`monitoring_zebras_second_color` default white; new `RS.camera_set_monitoring_zebra_colors()`; the camera finish push
constant is now a full 128 bytes); **editor tick boxes, default off**: `CameraAttributes.motion_blur_visible_in_editor`
and `rolling_shutter_visible_in_editor` (one per group: each group's enable check box hides the rest of its group) and
`Camera3D.shake_visible_in_editor` (unticked, shake does no work in the editor; a running game is unaffected; autofocus
unchanged). **Handheld shake presets** (user request): `Camera3D.shake_preset` with Custom (default, today's values) and
Handheld, Handheld Stabilized, Handheld Walking, Running, Shoulder Rig, Gimbal, Tripod in Wind, Vehicle Mount, sources
cited in the code; editing any shake value switches to Custom; saved scenes keep their preset. Verified (lead, merged
tree): `results/39-*/tests/run.sh` OK (68 checks: defaults and default-colored zebras pixel-identical in all three
renderers, zebra colors within 1/255, tick boxes in editor vs game runs, presets' measured RMS within 5% and band
centers within 2.4% over 120 s); task 38's `tests/run.sh` OK after ticking `shake_visible_in_editor` in its editor test.

### 4.56 Web loading screen: progress bar toggle and colors (October 2026) *(work task 42; §2.49)*

User request. Web export options `loading_screen/progress_bar/enabled` (default on) and, shown only while it is on,
`border_color` / `background_color` / `fill_color` (defaults `#b2b2b2` / `#efefef` / `#0075ff`, Chrome's own colors).
Both default shells hide the bar when off and style it (CSS custom properties, `::-webkit-progress-*` /
`::-moz-progress-bar`, the indeterminate sweep) only when a color differs from its default, so at defaults every browser
keeps its own look and exports made before look the same. Custom shells get a sibling placeholder `$GODOT_PROGRESS_BAR`
(`enabled`, three CSS hex colors, `customColors`), so shells walking `$GODOT_LOADING_SCREEN` are unaffected. Verified:
default export HTML differs only by the new shell code; default rendering pixel-identical in Chrome and Firefox for both
shells; 14/14 real exports (7 presets x Chrome and Firefox) start without errors with the expected colors and hiding;
every run through one launch helper that asserts the adapter (the first, flagless runs were discarded, §3 trap 79);
dev_mode build and smoke OK (lead, merged tree). Not done: Safari; a by-eye check that unticking the toggle hides the
three pickers in the editor's export dialog. Wiki: Web Export Options updated.

### 4.57 Longitudinal chromatic aberration; Mobile depth of field for physical cameras; camera arms in the Forward+ matrix (October 2026) *(work task 44)*

Merged. **Longitudinal CA** (user answer `ca-aperture`: lateral stays physical, longitudinal in the DOF pass):
`CameraAttributes.longitudinal_chromatic_aberration_enabled/_strength` + an RS setter, in the DOF pass on Forward+ and
Mobile, off by default. Model: an achromat's red and blue focus 1/2200 of the focal length behind green (Smith;
Kingslake and Johnson); Practical cameras use a ±2.7% blur scale. It uses the bokeh push constant's last free word (still
128 bytes). Measured on a defocused edge (strength 10): fringe 2.10 px vs 2.13 modeled at f/1.4, about half at f/2.8;
green behind focus, magenta in front. **Mobile physical DOF** (user answer `mobile-dof-physical`: Fix it):
`bokeh_dof_raster()` now takes the physical model as the compute path does; before, a CameraAttributesPhysical camera had
no blur at all on Mobile (edge radius 0.00 px), now 8.15 / 4.16 px vs Forward+ 7.89 / 4.03 and the model 7.81 / 3.91;
existing Mobile scenes with a physical camera now blur. **10 Forward+ matrix arms** (87 -> 97, category `camera`):
lens_optics, bokeh_shape, longitudinal_ca, tilt_shift, starburst, halation, lens_flare, veiling_glare_diffusion,
camera_lut, in_camera_processing; worker: full matrix 97/97 unbaked with --parity, the 10 new arms baked too, WebGPU within
0.003 of native. Interface references: only `(3,120)` added to the Mobile DOF composite fragment (it now reads the push
constant). **Lead (merged tree):** dev_mode editor and web template_release builds clean (0 warnings, 292 precompiled),
smoke OK, and lens_optics / longitudinal_ca / camera_lut pass `--parity` (native diff 0.002-0.003).

### 4.58 Average meters the whole frame; log-average metering; sharper halation; starburst default 20 (October 2026) *(work task 40)*

User answers on tasks 35/36. **Average auto exposure now meters the whole frame** (`average-dropped-edges`: Fix it):
Godot's Forward+ `luminance_reduce.glsl` rounded each level's size down and dropped the right ~20% of a 1080p frame and the
bottom ~27% at 720p; each 8x8 block now writes its sum, levels round up, the last divides by the pixel count. Existing
auto-exposed Forward+ projects change (test scene: -0.07 EV at 1080p, +0.32 EV at 720p), documented; Mobile's raster
path already covered the frame and never applies auto exposure. **`auto_exposure_metering_log_average`** (`metering-
statistic`: arithmetic default, log-average option): Reinhard's exp(mean(log(v + 1e-4))) for Average, Center-weighted
and Spot (Highlight is a percentile, unchanged). **`halation_high_quality`** (`halation-min-size`: option, current
default): a full-resolution first level for the Forward+ glare chain (radius 1: 3.38 px vs 3.36 model, 3.95 without; +0.18
ms at 1080p). **`starburst_strength` default 20** (`starburst-default-strength`). Verified (lead, merged tree): dev_mode
build clean, smoke OK; 16/18 default renders pixel-identical to the pre-merge editor in all three renderers, the two
others the intended Average change (Forward+ auto-exposed scenes); Average now reads the whole-frame mean (1.0280 / 1.0215
vs 1.0276 / 1.0211 at 1080p / 720p; before 0.978 / 1.278). Worker: log-average within +0.0005 EV of a Python reference,
WebGPU pixel-identical for metering. §3 trap 80. Question `glare-lod-calibration` on the hand-offs page.

### 4.59 Micro shadows on Environment (October 2026) *(work task 49; survey §2.48, `pp-micro-shadows`)*

`Environment.micro_shadows_enabled` (off) / `micro_shadows_opacity` (1.0), RS `environment_set_micro_shadows()`. Each
directional light is multiplied by `mix(1, saturate(N.L + 2*AO^2 - 1), opacity)` (Brinck and Maximov 2016, HDRP's
`ComputeMicroShadowing`), left alone where N.L < 0; N the normal-mapped normal, AO the material's own (before SSAO and
`ao_light_affect`); not with per-vertex lighting. Forward+ and Mobile carry the opacity in the scene UBO's former `pad`
word (no new bindings or variants, references unchanged); Compatibility has its own copy and one more vec4 in its scene UBO.
Worker: per-pixel factor vs the formula on an AO-gradient quad at 5 light elevations, mean error <= 1e-4 (max 0.0009
Forward+, 0.002 Mobile, 0.035 Compatibility near black); off / opacity 0 / no AO map bit-identical in 27 comparisons;
full Forward+ matrix 98/98 at parity, new arm `micro_shadows`. Lead (merged tree): dev_mode editor and web template clean,
292 precompiled, smoke OK; `micro_shadows` and `omni_light` at parity. Cost not measured (a few ALU per pixel per
directional light behind a uniform branch). Wiki: Engine-Additions-Micro-Shadows.

### 4.60 Build scripts: the no-threads, no-dlink web templates first (October 2026) *(work task 50; §2.50)*

User request. Every `build-*` helper builds and installs `web_nothreads_{debug,release}.zip` right after the editor (and
`wgsl_convert_cli`), prints "Ready to export", then the rest. `build-linux.sh`: editor -> wgsl_convert_cli -> emsdk
patches -> the pair -> Linux templates -> the other web variants; the narrowing flags still apply (a filter excluding
threads=no or dlink=no skips the pair with a warning); the timing table gains a "ready to export" line; new `EDITORS_DIR` /
`TPZ_COPY_DIR` overrides. `build.sh` / `build-macos.sh` / `build-windows.ps1` gained the non-dlink pair first and **no
longer install their dlink pair as `web_nothreads_*.zip`** (§3 trap 81); native template builds use the PATH from before
`emsdk_env`. The pair reaches the editor at once because `builds/<version>/templates` is a symlink to the export templates
folder on the user's machine; the ready line warns when they differ. Verified: one narrowed `build-linux.sh` run (ready at
2:18, total 6:03, exit 0), `bash -n` / shellcheck / the PowerShell parser clean, smoke OK; not run: the Linux native
template step after the pair, the dlink variants, macOS/Windows end to end (the next full run confirms it: "Ready to
export" about 2-3 minutes in).

### 4.61 Web Start button screen (October 2026) *(work task 51; §2.51, camera-sim spec + user styling)*

`html/start_button/enabled` (off) and, shown only while ticked: `text`, `background_color` (#3a3a3a), `text_color`,
`border_color`, `border_width`, `border_radius`, `font_family`, `font_size`, `padding`, and `custom_state_colors` with
`hover_color` / `pressed_color` (otherwise 15% / 30% darker). Passed in `$GODOT_CONFIG` as `startButton` (documented in
`config.js`; the config JSON now escapes `<`). Both default shells get a `'start'` mode; the old startup branch is
`startEngine()`, run on press; the missing-features check stays at page load; the button is never focused by script;
`cursor: pointer`, focus ring, Enter/Space. Nothing of the engine runs before the press. Worker: default export differs only
by the config key and shell code, game pixel-identical in Chrome and Firefox; `start_test.mjs` (cross-origin iframe) 15/16:
before the press `engine.rtenv` unset, no canvas width, only index.html / index.js / splash requested, no parent scroll; up
in 5-10 s after; styles, focus, keys OK; the one miss is Firefox not showing `:active` while Space is held. Lead (merged
tree): dev_mode editor and web template clean, smoke OK, launches audited. Not done: a windowed-editor check of the
dialog; Safari. Questions `start-button-*` on the hand-offs page. Preload is task 53.

### 4.62 Web `DisplayServer.window_set_size()` resizes the game (October 2026) *(work task 52; §2.52, camera-sim)*

`godot_js_display_desired_size_set()` resized the canvas synchronously, so the next `process_events()` size check saw
no change: the root `Window` never emitted `size_changed`, the viewport kept the old size and the WebGPU surface was
never reconfigured ("surface texture differs from configured"); under the render thread the OffscreenCanvas was not
resized at all. It now returns whether the canvas changed, and `DisplayServerWeb::window_set_size()` then runs the
browser-resize path (`_canvas_size_changed()`). Canvas resize policy 0 (None) no longer consumes a pending page resize.
Worker: `results/52-web-window-set-size-signal/resize_test.mjs` under policies 0/1/2, nothreads and threads. Lead
(merged tree): dev_mode editor and web template clean, the test passes under all three policies (nothreads), 0
warnings. The shell's `fitCanvasToViewport()` refit after an engine-driven resize went to task 54. §3 trap 82.

### 4.63 Web start screen downloads the game while it waits (October 2026) *(work task 53; §2.51, camera-sim spec)*

`html/start_button/preload` (on) and `html/start_button/preload_progress` (on; shown only while `preload` is), both
shown only with `html/start_button/enabled` and passed as `GODOT_CONFIG.startButton.preload` / `.preloadProgress`. New
`Engine.prototype.prefetch(onProgress)` fetches through the `Preloader` exactly what `startGame()` will ask for (the
wasm, the main pack, the shader pack, the shader extras at low priority, and the texture pack `pickTexturePack()` picks
from the adapter's features), with no wasm compile and no GPU device; `startGame()` reuses every download and its
progress continues from what is loaded. The `Preloader` keys downloads by URL, forgets a failed one so the next
request retries it, frees buffers once the files are in the engine's file system, and no longer hangs forever on a
connection cut mid-body. `Engine.pickTexturePack` is public. Both default shells prefetch on the start screen unless
Save-Data is on, showing "Downloading N%" then "Ready". Worker: 18/18 runs (9 scenarios, Chrome and Firefox headed,
adapter asserted; cross-origin iframe, baked Forward+ project with both texture families): each file requested once,
engine idle before the press, press-to-game 0.89 s (Chrome) / 0.73 s (Firefox) against a warm-cache export, a
throttled download's bar never goes backwards, Save-Data fetches nothing early, a failing main pack is refetched on the
press. Lead (merged tree, with 46): dev_mode editor and web template 0 warnings; `ready`, `savedata`, `fail` and `auto`
pass in Chrome and Firefox. Not done: Safari, a real network, the windowed-editor dialog. The `webgpu-full-size.html`
shell's own device request asks for 16 textures per stage, so it runs Forward Mobile (pre-existing). §3 trap 83.
camera-sim can drop its `window.fetch` wrapper for `prefetch()` (results/53-web-preload-before-start/for-tasks.md).

### 4.64 Screen-space light shafts (October 2026) *(work task 46; survey §2.48, `pp-light-shafts`)*

`DirectionalLight3D.light_shafts_enabled` opts a light in (the first that does is the sun); `Environment.light_shafts_*`
(intensity, threshold, decay, tint) and `rendering/environment/light_shafts/quality` (16/32/64 samples). Mitchell's
radial blur (GPU Gems 3 ch. 13) as an exponentially weighted mean of the sky mask on the way to the sun, in frame
heights, with a lengthwise prefilter over one march stride, so the result is the same at every resolution and sample
count. RD (Forward+, Mobile, web): four quarter-resolution raster passes (`RendererRD::LightShafts`) added to the
scene light before DOF and tonemapping, own push constant, no tonemap binding. Compatibility: `GLES3::LightShafts` +
`post.glsl` `USE_LIGHT_SHAFTS` (texunit 6), single view. Worker: defaults bit-identical (6/6), render vs an
independent numpy model correlation 0.997-0.999 native and 0.990-0.9996 WebGPU, WebGPU within 0.5-3% of native
energy, +0.07 ms GPU on WebGPU at 1152x648. Lead (merged tree): defaults 0 px against the pre-merge editor in all
three renderers; model correlation 0.998 Forward+, 0.997 Mobile, 0.972 Compatibility, 0.990 WebGPU; WebGPU shaft
energy 0.987 of native. §3 traps 84, 85.

### 4.65 Environment color grading (October 2026) *(work task 45; survey §2.48, `pp-grading`)*

A **Color Grading** group on `Environment` (`grading_*`, 28 properties, off by default): color filter, hue shift,
vibrance; lift/gamma/gain; shadows/midtones/highlights with ranges; channel mixer; split toning; eight `Curve` curves
(master, R, G, B, hue-vs-hue, hue-vs-sat, sat-vs-sat, lum-vs-sat). RS `environment_set_color_grading()` and four more.
Display-referred, in URP's LutBuilderLdr order (`servers/rendering/storage/color_grading.cpp`, sources cited), baked
on the CPU worker threads into one 33³ RGBA16F LUT of lattice points only when a grading value changes (1.5 ms median
with everything on, 4.6 ms single-threaded as a no-threads web export pays). Forward+ / Mobile / web: the LUT uses the
existing color correction slot (no tonemap binding or interface change); a 2-variant compute shader
(`effects/color_grading.glsl`) composes it with the user's `adjustment_color_correction` LUT and outputs linear for HDR
viewports; the tonemap samples it at lattice points (§3 trap 86). Compatibility: post-pass `USE_COLOR_GRADING`
(texunit 5) before the user LUT. Vibrance is clamped at full desaturation (below -0.5 it inverted saturated colors).
Worker: 25 cases against an independent numpy reference (729 lattice + 512 off-lattice patches): Forward+ within
1/255 of the baked-LUT model and 0.65/255 of the exact math; Mobile 1-3/255; web pixel-identical to native in 31
captures; defaults 0 px (12 renders). Lead (merged tree with 46 and 47): defaults 0 px against the pre-merge editor in
all three renderers (off, off-chart, user 3D LUT alone); six cases within 1/255 of the LUT model in Forward+, 1-2/255
in Mobile and Compatibility (Compatibility measured on its post-pass input, `compat_check.py`); web pixel-identical to
native Forward+ in all seven compared cases. Questions `grading-hdr`, `grading-look`. No Forward+ matrix arm yet.

### 4.66 Panini projection (October 2026) *(work task 47; survey §2.48, `pp-panini`)*

`CameraAttributes.panini_enabled / panini_distance / panini_vertical_compression` (Sharpless, Ballestrem and Thomas
2010) in task 33's lens resample of the tonemap pass (header bit 32, free `lens[2].zw`, `lens[4].zw`, `lens[7].zw`; no
layout change, no new binding), all three renderers and the web, off by default. The image is scaled so no border
shows: the horizontal FOV is kept, the vertical one narrows (to 2/3 of its tangent at 120°, d = 1). Vertical
compression is this fork's definition: 1 = the paper's projection, 0 = horizontal lines straight. The math lives in
`servers/rendering/panini_projection.h`, shared with Camera3D, whose `unproject_position`, `project_ray_normal`,
`project_local_ray_normal` and `project_position` follow it (orthogonal cameras and XR skip it; lens distortion is
not followed, documented). Worker: dots within 0.39 px of an independent numpy model in every renderer and the web;
Camera3D unproject 6e-6 px from the model; verticals straight; spheres round at d = 1, s = 1. Lead (merged tree):
off 0 px against the pre-merge editor in all three renderers; dots within 0.29 / 0.29 / 0.37 px (Forward+ / Mobile /
Compatibility) and 0.36 px on the web (baked export). The worker's web captures had soft edges against native even
with Panini off (an unbaked headless export; not seen in the lead's baked one). Not tested: MSAA, scaled 3D, frustum
offsets. Questions `panini-vertical-compression`, `panini-fov`, `panini-look`. §3 trap 87.

### 4.67 Web: the embedding page drives the game's fullscreen (October 2026) *(work task 54; §2.53, camera-sim)*

`html/embed_fullscreen/enabled` (off) and `html/embed_fullscreen/allowed_origins` (comma-separated, shown only while
enabled; empty = any parent), passed as `GODOT_CONFIG.embedFullscreen`. A page on another origin cannot fullscreen the
game's canvas (its click is not user activation in a cross-origin iframe), so it fullscreens the iframe and says so
with `postMessage`: parent → game `{type: 'godot:fullscreen', on}` on every `fullscreenchange` and in answer to game →
parent `godot:ready`; game → parent `godot:exit-fullscreen` (`window_set_mode(WINDOWED)` while the parent holds it;
the mode changes on the parent's answer, `godot_js_display_fullscreen_exit()` returns 2) and `godot:request-fullscreen`
(`window_set_mode(FULLSCREEN)` once a protocol parent is known; canvas request after 1 s without an answer).
`GodotDisplayScreen.isFullscreen()` counts the parent's fullscreen, so the existing sizing path draws at iframe size x
DPR and fires `size_changed`. Only `event.source === window.parent`, only listed origins; the parent's echo of the
game's own canvas fullscreen is ignored; option off or not framed = no listener. `full-size.html` refits from a
`ResizeObserver` on the canvas (task 52's leftover). Worker: `embed_test.mjs` on two origins, all 8 points in Chrome
and Firefox; task 52's `resize_test.mjs` with a scale check under policies 0/1/2. Lead (merged tree with 48): dev_mode
editor and web template 0 warnings; Chrome all checks pass; Firefox 59/59 on a rerun after a first run where the
parent's own fullscreen request was ignored 6 times (§3 trap 88); resize test passes under all three policies. Not
done: threads template, Safari, the export dialog by hand. Question `embed-request-option`. camera-sim's switch-over
steps are in `results/54-web-embed-fullscreen-messages/for-tasks.md`.

### 4.68 GTAO as an SSAO method (October 2026) *(work task 48; survey §2.48, `pp-gtao`)*

`rendering/environment/ssao/method` (ASSAO default, or GTAO) and `RS.environment_set_ssao_method()`. `effects/gtao.glsl`
ports XeGTAO (Intel, MIT; Jimenez et al. 2016) and writes the same AO texture ASSAO does, so the scene shader and the
SSAO debug view are unchanged; §2.34's RT AO can be a third method writing it too. Reuses the Environment's SSAO
settings (default intensity and power give XeGTAO's reference). Quality levels 2x2, 3x2, 3x3, 9x3 and 9-18x4 slices x
steps (XeGTAO's two lowest presets darken open ground without TAA). Forward+ and web only (Mobile has no SSAO,
Compatibility keeps its own). With TAA the noise changes per frame (XeGTAO's design; slight shimmer, question
`gtao-taa-noise`). Worker: against a numpy ray-traced reference, GTAO Medium MAE 0.028 / correlation 0.90 / creases
+0.05 too light, against ASSAO's best 0.037 / 0.83 / +0.15; web within 0.001 of native; Medium at half size 0.31 ms at
1080p (ASSAO default 0.29 ms), High full size 2.0 ms (native). Lead (merged tree with 54): default method AO buffer and
final image 0 px against the pre-merge editor; the reference numbers reproduce (gtao q2 full 0.0275 / 0.902, web
0.0275 / 0.902); smoke OK with the five GTAO modules added to the interface references; **full Forward+ matrix 99/99
at parity, unbaked and baked** (new arm `gtao`). §3 trap 89.

### 4.69 Glare chain calibrated per lod (October 2026) *(work task 55; user answer `glare-lod-calibration`: Recalibrate)*

Halation, the diffusion filter and the lens flare blur now have their modeled size at every resolution. Three faults,
measured from HDR captures (§3 trap 80): (1) the chain's blur per lod is sigma² = A + B·4^L lod-0 texels² (Forward+
half resolution A -1.233, B 5.181; full resolution -1.556, 5.182; Mobile -0.157, 0.440), not 2.68·2^L, which matched
only at 720p; (2) the sampler's blend between mips is not linear (§3 trap 90), so `camera_glare_fetch()` reads the two
integer lods and blends them with a closed-form weight whose best-fitting Gaussian has the requested sigma; (3)
Mobile's raster levels drift on odd sizes (§3 trap 91), corrected in the glare fetch. CameraEffects `glare[5].z`
(A/B) and `.w` (raster flag) taken, `glare[2].w` now the diffusion's base sigma; the glare block is full. Floors in the
class docs: about 4 px standard, 1.9 high quality, 2 on Mobile. Existing glare looks change by up to ~10% in size (the
user's choice). Worker: Forward+ halation within 3.2% at every height (720-2160) and radius, was -22% to +9%;
diffusion within 3.9%; flare ghosts -9% to +8% (unexplained, was -26% to +28%); WebGPU halo sigma within 1-3% of native.
Lead (merged tree): defaults 0 px against the pre-merge editor (24 captures, 3 renderers); Forward+ halation at 1080
and 2160 within 2.6% from radius 1 up (centered and nine-point sources); Mobile nine-point within 3.4%, a single
centered point up to +8% at radius 4 (sub-texel phase of the raster chain; was +15%); smoke OK apart from task 57's
then-unregenerated bindings. Lead, web (merged tree with 56-58): halo sigma within 3.3% of native at radius 4 and 1.2% at radius 16 (the high-quality radius-1 halo 8%, 8-bit quantization of a 3 px halo). Blended reads double the glare's texture reads (up to 48 per pixel for the flare), **not timed: the halation cost in the class docs (0.25 / 0.42 ms) and the wiki (0.08 / 0.26 ms) predate it and disagree; re-time and settle one pair.**

### 4.70 Custom tonemapper (October 2026) *(work task 56; survey §2.48, `pp-custom-tonemap`)*

`Environment.TONE_MAPPER_CUSTOM` with `tonemap_custom_slope / toe / shoulder / black_clip / white_clip` (Unreal Film's
parameters and defaults 0.88 / 0.55 / 0.26 / 0 / 0.04) and `RS.environment_set_tonemap_custom()`, all three renderers
and the web. An analytic curve (no Unreal code): a straight line in log10, tanh toe and shoulder, 0.18 maps to 0.18;
per channel in Rec.709, clips at 1.0 (questions `custom-tonemap-gamut`, `custom-tonemap-hdr`). Its parameters live in
the first **Environment-owned tonemap buffer**, `EnvironmentEffects` (std140, 256 bytes; header x = tonemap bits, the
rest reserved for later Environment post parameters such as an HDR grading mode): Forward+ set 3 binding 2, Mobile set
0 binding 7 plus spec constant 19 (ADRENO_BUG moved to bit 20), Compatibility 12 floats in `TonemapData`. Uploaded
before the render pass, so Mobile's subpass tonemap works. Worker: Forward+ within 0.64/255 of an independent numpy
model (13 cases, 768 samples each), web pixel-identical to native, Mobile 97-99.6% within 1/255 (its 2.0 cap limits
output to 0.86 with the defaults, documented), Compatibility checked for consistency only (its chart readback follows
no model for any tonemapper, pre-existing); existing tonemappers 0 px in 42 renders; matrix arm `tonemap_custom`
(delta 29.6). Lead (merged tree with 57): the same numbers reproduce; existing tonemappers 0 px against the pre-merge
editor in all three renderers; web identical to native on all 13 charts (lit scenes max 1/255); `tonemap_custom` passes
`--parity`. §3 trap 92 (gles3 includes, fixed).

### 4.71 Local exposure (October 2026) *(work task 57; survey §2.48, `pp-local-exposure`)*

A **Local Exposure** group on `CameraAttributes` (both kinds, off by default): Unreal's Bilateral method (highlight
contrast 0.6, shadow contrast 0.8, detail strength 1.0, middle gray bias, blurred luminance blend 0.6, kernel size 50%
of the frame width; `middle_gray`, not "grey", because codespell rewrites the docs) and two RS setters. A bilateral
grid (Chen, Paris and Durand 2007) of 16 square cells down the frame x 32 bins of 1 EV, so the result is the same at
every resolution: `RendererRD::CameraLocalExposure` (build + blur compute) and the Forward+ tonemapper samples it at
set 1 binding 3 right after exposure. CameraEffects `header.w` bit 0 and `local_exposure[2]` (the former
`reserved[0..1]`). Forward+ desktop and web only: Mobile (RGB10A2 caps at 2.0), Compatibility (no pre-tonemap post)
and XR do not apply it (class-doc notes, one-time editor warning). Worker: within p99 0.0015 EV of an independent
numpy model at 720p/1080p/1440p, 11 controls plus auto exposure and physical units within p99 0.0032 EV; edge halos
equal the model's; 12/12 defaults 0 px; WebGPU 0.17 ms at 1080p (build 0.04, blur 0.12, tonemap +0.013); matrix arm
`local_exposure` (delta 11.39). Lead (merged tree with 56): defaults 0 px against the pre-merge editor in all three
renderers, and on = off in Mobile and Compatibility; model within p99 0.0015 EV, max 0.0027 EV at all three
resolutions; WebGPU's effect (on minus off) equals native's (mean |delta| 11.899 vs 11.898 at the defaults, 35.306 vs
35.303 strong; away from edges p99 1/255). Questions `local-exposure-defaults`, `local-exposure-blend`,
`look-local-exposure`. Follow-ups, optional: a separable blur (0.12 ms → ~0.03), Exposure Fusion as a second method in
the grid's last slice. §3 trap 93.

### 4.72 `run_spec_ops.sh`'s invalid FConvert probe dropped (October 2026) *(was §2.44; work task 58)*

`spec_op_probes.py`'s `op_FConvert_f32` converted f32 to f32, which the vendored validator rejects (§3 trap 94). A valid
FConvert needs f16 or f64; WGSL has no f64 and no engine or user shader declares an f16 spec constant, so the probe is
gone, with the reason in the generator. `spec_op_check.mjs` finds Playwright from a worktree and refuses a fallback
adapter. `run_spec_ops.sh`: override path 42 pass / 6 expected, legacy path 47 pass / 1 expected (`op_SMod`), 0 unexpected.

### 4.73 Swizzled texture views audited; the driver warns when it drops one (October 2026) *(was §2.43; work task 58)*

Every non-identity swizzle that can reach `RenderingDeviceDriverWebGPU` was traced (table in
`results/58-small-web-correctness/for-tasks.md`). **RGBA4444 and RGB565 image textures failed outright on the web**
(no packed 16-bit formats in WebGPU; `texture_create()` errored), now converted to RGBA8. The RA-as-RG normal-map formats
always decompress on the web; zero/one swizzles that only restate a channel the image lacks are reset to identity on the
web; decompressed DXT1 gets alpha 255 as the swizzle did; an opaque viewport's texture no longer asks for alpha 1 on the
web (else every web game warned). `_warn_dropped_swizzle()` in `texture_create()` / `_shared()` / `_shared_from_slice()`
warns once per (view format, swizzle) for what is still dropped: a `Texture2DRD` over R8/RG8 and user `RDTextureView`
swizzles. New Forward+ arm `image_formats` (L8, LA8, RGBA4444, RGB565, R8, RG8 against RGBA8-expanded copies). The user
then answered both of the task's questions with the non-default option (Texture2DRD over R8/RG8 reported as R8/RG8 on
the web; opaque viewports write alpha 1 on the web): work task 60.

### 4.74 FSR2 accumulate and scene binding 24: the depth fallback is never read (October 2026) *(was §2.42 and §2.31's "4x4 depth placeholder"; work task 58)*

FSR2's accumulate pass declares `r_input_depth` but reads motion vectors through `LoadDilatedMotionVector()`, which never
calls `LoadInputDepth()`; SPIRV-Cross drops the variable and the blank fallback in its slot is never sampled. The FSR2
passes that do read depth get the real depth texture. New arm `fsr2_camera_move` (camera panning across the sky): native
diff 0.059. Scene set 1 binding 24 is `DEFAULT_RD_TEXTURE_DEPTH` (a 4x4 D16 placeholder, bound when no material reads
`DEPTH_TEXTURE`); on the web it is now the black float default, so `--verbose` is quiet for the scene shaders (FSR2
accumulate's unread binding still logs). Also fixed: `wgsl_precompile.py` lacked task 17's `REACTIVE_MASK_FROM_ALPHA`
define, so FSR2's depth-clip and TCR passes were converted at run time (correct, slower). Worker: matrix baked 101/101.
**Lead (final merged tree, tasks 55-58): dev_mode editor and web template 0 warnings, smoke 311/311 interface and 622/622
scan facts, Forward+ matrix 103/103 at parity, unbaked and baked.** §3 trap 94.

## 5. References — retired docs

These were consolidated into this file on **2026-10-14** and deleted. Nothing listed here is needed
for ordinary work; recover one only to re-read a round-by-round trail this file summarizes.

```bash
git log --diff-filter=D --name-only -- 'webgpu_notes/*'   # the deleting commit
git show <commit>^:webgpu_notes/HANDOFF.md                # read a file as it was
```

| retired doc | what it was for | where it went |
|---|---|---|
| `webgpu_notes/HANDOFF.md` | cold-start snapshot: verified state, build traps, corrections | §1, §2, §3 |
| `plan-of-attack.md` (repo root) | "remaining work only" list, Sept 2026 | §2 |
| `webgpu_notes/webrtc_web.md` | WebRTC parity evaluation + hardening plan, F1-F10 in full | §4.20, §2.7 |
| `webgpu_tests/TASKS.md` | test-suite coverage matrix and gaps | §1, §2.16 |
| `webgpu_notes/RESEARCH.md`, `DESIGN.md`, `INITIAL_PLAN.md`, `IMPLEMENTATION.md`, `BUILD_BLOCKERS.md` | pre-implementation research, the method-by-method API mapping, the original 2-week brief, the build-system change list | §4.1; the live versions are `CLAUDE.md`, `drivers/webgpu/README.md`, `webgpu_site/*.md` |
| `webgpu_notes/review_v3/` (16 files) | the code-review round that produced `webgpu_site/*.md` | superseded by `webgpu_site/` itself |
| `webgpu_notes/stubs/` (9 files) | a September 2026 snapshot of the driver sources | superseded by `drivers/webgpu/` |
| `naga_to_tint.md`, `naga_to_tint_debugging.md`, `naga_to_tint_commit_message.md`, `remove_rust.md` | the naga→Tint migration and why Rust had to go | §4.16 |
| `precompile_naga_spirv_to_wgsl.md`, `precompile_specialized_to_wgsl.md`, `finish_async_shader_comp.md` | build-time WGSL precompilation, `override` constants, the Tint Web Worker | §4.9, §4.21 |
| `IPC_OPTIMIZATION_SUMMARY.md`, `COMMAND_BUFFERING_WASM_TO_JS.md`, `COMMAND_STREAM_RECORDER_PERF_OPTIM.md`, `perf_optimization_may3_2026.md` | the IPC/per-draw optimization arc and the unimplemented recorder design | §4.21 |
| `BUFFER_UNMAP_32MB_FLUSH_ROOT_CAUSE.md`, `SLOW_WINDOW_OR_TEXTURE_CREATION.md`, `batched_images_to_vram.md` | the upload/creation stall root causes | §4.21 |
| `ASYNC_WEBGPU.md` | the ASYNCIFY experiment and why it was abandoned | §4.21 |
| `STARTUP_PROFILING.md` | May 2026 startup profile | superseded by §2.4's numbers |
| `benchmark_vs_threejs.md`, `native_vs_web_benchmarks.md`, `manualy_run_benchmark_scenes.md` | benchmark results and fairness analysis | §4.21, §2.17 |
| `emsdk-upgrade.md` | the 4.0.11 → 6.0.9 upgrade plan and verification | §4.21, §2.21 |
| `FIX_2D_PLATFORMER.md`, `FIX_CONSOLE_ERRORS.md`, `FIX_PHASE7.md`, `push_constant_ring_buffer.md` | individual early fixes and the ring-buffer design | §4.1, §4.2 |
| `SYNC_4.7.3_PLAN.md` | a 4.7.3 merge that was never started | obsolete — superseded by the 4.8 sync (§4.17) |
| `PR_DESCRIPTION.md` | draft upstream PR description | §2.24 |
| `geckos_dedicated_server.md`, `geckos_backend_schedule.md` | Node dedicated-server evaluation + week-by-week schedule | §2.25 |
| `Braced GDScript (.gdb) — Implementation Plan.md` | the `.gdb` language plan | §2.26 — **this one file was left on disk**: it is explicitly gitignored (`.gitignore:408`), so it is not in git history and deleting it would lose it |

**Still live, and not duplicated here**: `CLAUDE.md` (build/test commands, architecture, conventions),
`drivers/webgpu/README.md` (driver-local notes), `webgpu_tests/README.md` (how to run every tier),
`webgpu_tests/screenshot_comparison/LIVE_REPRO_METHODOLOGY.md` (how to test a reported bug against a
real project), `webgpu_tests/forward_plus/README.md` (the add-a-feature rule), `misc/emsdk_patches/`
(the toolchain patches and how to add a third), `thirdparty/README.md` (the vendored SPIRV-Cross and amd-fsr2 patches), and
`webgpu_site/{ARCHITECTURE_AND_DESIGN,TECHNICAL_REFERENCE,PERFORMANCE_AND_OPTIMIZATION,CORRECTNESS_AND_COMPATIBILITY,FAQ}.md`.
