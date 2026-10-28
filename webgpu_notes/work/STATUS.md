# Status (lead-maintained)

## 2026-10-28 — camera realism batch (tasks 33-38; user request after grain §4.47 and LUTs §4.48)

Conventions for all six: `camera-batch.md`. Wave 1 runs in parallel; wave 2 needs task 33's CameraEffects UBO.

| task | what | wave | owner |
|---|---|---|---|
| 33 | lens optics: sensor format, vignetting, distortion, CA, anamorphic, ND/T-stops, diffraction, breathing; **CameraEffects UBO** | 1 | worker-33 (retired); **merged `779120b64e`**, verified, TASKS §4.52; questions `breathing-default`, `ca-aperture` |
| 34 | motion blur from the shutter, rolling shutter | 1 | worker-34 (retired); **merged `4a96bd2ae6`**, verified, TASKS §4.51; question `motion-blur-editor` |
| 37 | in-camera NR + sharpening (CameraAttributes); monitoring overlays + Visible in Output (Camera3D) | 1 | worker-37 (retired); **merged `ffda7ef1d3`**, verified, TASKS §4.50; questions `zebra-colors`, `zebra2-window`, `peaking-source` |
| 38 | Camera3D autofocus, stabilization / handheld shake | 1 | worker-38 (retired); **merged `c2d0987359`**, verified on the merged tree, smoke OK; TASKS §4.49; question `shake-in-editor` on the hand-offs page |
| 35 | aperture blades, cat's eye, tilt-shift, starburst, halation; Environment lens flare, veiling glare, diffusion | 2 | worker-35 (retired); **merged `ec882a87c2`**, verified, TASKS §4.54; questions `starburst-default-strength`, `halation-min-size`, look `look-bokeh-glare` |
| 36 | AE metering modes, auto white balance | 2 | worker-36 (retired); **merged**, verified, TASKS §4.53; questions `metering-statistic`, `average-dropped-edges`, `awb-neutral` |
| 39 | user answers: zebra colors user-selectable; editor tick boxes (default off) for motion blur and handheld shake; shake presets | 2 | worker-39 (retired); **merged**, verified, TASKS §4.55 |
| 40 | user answers on 35/36: fix Average's dropped edges; log-average option; sharper halation option; starburst default 20 | 3 | worker-40 |
| 41 | post-processing survey (TASKS §2.48) | 4 | worker-41 |
| 42 | web loading bar toggle + colors (TASKS §2.49) | 4 | worker-42 |
| 43 | wiki audit: pages for additions that have none | 4 | worker-43 |
| 44 | longitudinal CA; Forward+ matrix arms for camera features; Mobile physical DOF (`mobile-dof-physical`) | 4 | worker-44 |

When all are integrated: user-facing wiki pages under Engine Additions from each `for-docs.md` (user request).

## 2026-10-26 late evening — docs pass for tasks 24–30 (task 31, `work/31`)

Every task of the day is merged (24 `449f085aa6`, 25 `fd53d6d657`, 26 `0f0dfd50fe`, 27 `756b99bf6f`, 28 `287109ddfd`,
29 `add69b409d`, 30 `4b4bd00e62`), and the 87-arm Forward+ matrix is **87/87 at parity unbaked and baked** on the
merged tree (lead, 20:24). Worker-31 brought the shared docs up to that state: TASKS §1 (evening paragraph, new
Verified rows for the 87/87 run, `dof_nan`, `bake_isolation`, `metahuman` + `nan_check.gd`, the update-label unit
test), the §2 indexes, and two new open items, **§2.46** (MetaHuman skin follow-ups: cloth normal z, trap 72; pores on
the lips; the WebGPU after-capture that hung twice; `look-skin-pores`) and **§2.47** (the update check's blind spot and
the pending `update-newer-rule` / `update-repo` answers). Stale 82-arm counts fixed in README.md and
CORRECTNESS_AND_COMPATIBILITY.md; the baker's scoped capability override documented in `drivers/webgpu/README.md` and
TECHNICAL_REFERENCE §4.5. Details: `results/31-docs/SUMMARY.md`; wiki: nothing stale, two optional additions in
`results/31-docs/for-wiki.md`.

## 2026-10-26 evening — tasks 28–30 out (user decisions on the hand-offs page; 30 a user request)

| task | what | owner |
|---|---|---|
| 28 | NaN guards for glow, SSR and auto exposure (`nan-guards`: "Guard all three now") | worker-28 (retired); **merged `287109ddfd`**: `finite_inc.glsl` shared with the DOF; glow 59,791 → 4 px, SSR 212 → 4, auto exposure no longer blanks the frame; 3 new arms (87); lead: smoke OK, NaN tier 0 failures, **87/87 parity unbaked and baked**; TASKS §4.44 |
| 29 | White balance range 2000–10000 K and tint -150..150 (`wb-range`, `wb-tint-units`) | worker-29 (retired); **merged `add69b409d`**: ±150 = old ±1 (0.05 Duv), arm tint 30 renders the old frame (0 px); lead: in the 87/87 run; TASKS §4.42 |
| 30 | The editor's "new version" check looks at this fork's GitHub releases instead of godotengine.org (user request) | worker-30 (retired); **merged `4b4bd00e62`**: newer = later commit date of the release's `main-<sha>`; live check screenshots in results; lead: unit tests pass, merged editor compiles; TASKS §4.45; questions `update-newer-rule`, `update-repo` on the hand-offs page |

## 2026-10-26 — tasks 24–27 out (user requests)

| task | what | owner |
|---|---|---|
| 24 | White Balance (temperature + tint) on CameraAttributesPhysical and CameraAttributesPractical, all three renderers and the web | worker-24 (retired); **merged `449f085aa6`** (fp_scene.gd conflict with 26: both arms kept, 84); lead smoke OK, scan-reference diff checked (only override 18); TASKS §4.42; **84/84 parity unbaked and baked** |
| 25 | MetaHuman skin looks coarse, pores and follicles huge: find the cause, fix it in the importer/shader | worker-25 (retired); **merged `fd53d6d657`**: pore map tiled 16x per UV unit on every mesh, now scaled to UV density (1.2 cm); lead: merged editor (`../godotwebgpu-lead`), metahuman --shots 0 failures; TASKS §2.33, trap 72; the WebGPU after-capture (`capture_web.mjs` on worker 25's `web_after` export) hung before its first SHOT twice, even with 10 min: open; user look pending |
| 26 | MetaHuman strand shader writes a NaN (camera-sim, 4.80 m / 1.63 m), bokeh DOF spreads it into a black hexagon: fix the source and make DOF NaN-proof | worker-26 (retired); **merged `0f0dfd50fe`**: zero-width lash tips, `pow()` of -1e-9; lead smoke OK, dof_nan 0 failures, metahuman verify + nan 0 failures after a fresh import (trap 71: `run.sh` now reimports when the editor is newer); TASKS §4.41, traps 70–71; **84/84 parity unbaked and baked** (bake gate ceiling 130 → 150, `ae97d95c38`) |
| 27 | FSR 2 on a web export: "Mismatch uniform type ... Expected 'Image', supplied: 'StorageBuffer'" every frame, FSR 2 draws nothing | worker-27 (retired); **merged `756b99bf6f`**: the errors were the *editor's* frames during the bake (the baker's WebGPU capability answers leaked to them); override now scoped to the baker's calls, FSR 2 variants as groups; lead: bake_isolation PASS, FSR 2 cache misses 7 → 0, camera-sim Chrome + Firefox 0 translated, fsr2/taa parity both; TASKS §4.43, trap 73 |

## 2026-10-26 — tasks 20–23 (branches `work/20`..`work/23`, no task files)

| task | what | outcome |
|---|---|---|
| 20 | load-ramp benchmark (`benchmark/run_ramp.sh`, README perf table generator) | merged `086327c7f2`; measured on all six legs, 60/60 ramp-runs converged (`results_ramp/2026-10-26/`); traps 66–67, TASKS §2.30 |
| 21 | Forward+ matrix 71 → 82 arms (LightmapGI via `bake_lightmap.sh`, trails, skinning, FogVolume, stencil, bent normals, SMAA, compositor, occlusion culling) | merged `4f39eb0e58`; lead full run 81/82 unbaked and baked (occlusion culling KNOWN, no culler on web); TASKS §4.39 |
| 22 | occlusion culling on the web: raycast module (Embree) in web templates, nothreads culler fix, `occlusion_bench` | merged `b13c7bee24`; 82/82 unbaked and baked, smoketest 23/23 Chrome and Firefox; on by default with the export option `rendering/occlusion_culling` (`508f4ed3b2`); TASKS §4.38, trap 69 |
| 23 | the day's docs: TASKS §1/§2/§4.38–4.40, CLAUDE.md (wiki), webgpu_site, webgpu_tests/README.md, docs/braced_gdscript, ProjectSettings.xml | this commit on `work/23` |

Also today, by the lead: README rewritten (`6b82ff35aa`) and the GitHub wiki published (separate repo, local clone
`../godotwebgpu.wiki`); trap 68 (stale native shader cache in the parity fixture, `8acda4dfa9`).

## 2026-10-25 20:45 — 09 and 10 integrated; Tint is gone

- **09 merged** (d3cf5ff893): thirdparty/tint and every Tint path deleted, `tint_convert_cli` → `wgsl_convert_cli`.
  Lead rebuild (editor + wgsl_convert_cli + release nothreads template, 2:00) clean; smoke OK (276/276 interface vs the
  stored reference, 552/552 scan facts, 0 invalid in Chrome); `BAKE=1 ./export.sh` + `--parity --baked` on the 4080:
  **71/71**, RESULT FAIL only from the pre-existing bake gate (translated 97 > 80, now TASKS §2.39). Old
  `bin/tint_convert_cli` moved out of bin/ so a missed reference fails loudly.
- **10 merged**: CLAUDE.md, READMEs, webgpu_site, thirdparty/README.md describe SPIRV-Cross; swap archived as TASKS
  §4.36, web wake/orientation §4.37, new §2.38-2.44, traps 55-65.
- Open with the user: rename `lower_tint_unsupported_ops` / `tint_crash_inputs.frag` (board, default keep). Board
  retirement waits on §2.38 (smoke.sh lives here and CLAUDE.md points at it).

## 2026-10-25 18:50 — 19 integrated; build-time measurement

- **19 integrated**: merged tree builds (editor + web release template, 1:30), smoke OK, driver unit 379/0, fixture export
  clean with `fullscreenOnFirstTap: true` in the shell config.
- **Build time, Tint vs SPIRV-Cross** (lead, -j12, ccache off for the measured objects; `scratchpad/buildtime/`):
  converter CLI clean 63 s vs 44 s (368 vs 7 converter objects; both compile SPIRV-Tools' 199). Web template, converter
  + driver objects cold plus the precompile step, no link: 41.9 s (380 TUs) vs 10.7 s (20 TUs), ~31 s per web variant,
  ~4 min across build-linux.sh's 8. With a warm ccache the difference is small (task 03: 121 vs 102 s relink).

## 2026-10-25 18:45 — 11 and 19 merged; the bar holds; 09 (remove Tint) out

- **11 integrated** (harness only): Firefox desktop (Linux, Playwright Firefox 155, wgpu pinned to Vulkan) works on
  SPIRV-Cross exactly as on Tint: Forward+ `--parity` 69/71 on both (SDFGI 0.277 in Firefox for both: Firefox-only,
  new item), smoketest 23/23, 0 naga/pipeline errors, naga 276/276 and 2564/2566 project stages on both. Heavy GPU 3.82
  vs 3.88 ms (SC+0019 vs Tint); postfx +10% fps; shadows/pbr ~1.8% fewer fps at ~1000 fps, CPU-side, GPU equal.
  SUMMARY saved by the lead from the worker's report (the harness refused the worker's write).
- **19 merged**: wake lock, `html/orientation` (Project Setting default), `html/fullscreen_on_first_tap` (default on),
  high-performance on the probe adapter; phone and desktop checked by the worker. Lead build + smoke pending (the
  build-time measurement holds the build lease).
- **The user's bar holds**: Forward+ parity 71/71 (Chrome, with 0019), no tier worse, cameraSim matches by eye (user),
  Firefox desktop works and is as fast, Chrome for Android works and is as fast. **Task 09 (remove Tint, rename the CLI)
  started.** Task 10 (docs) after it.

## 2026-10-25 17:45 — user answers (board)

- cameraSim look: **Matches** (user). The Tint-deletion bar now waits only on task 11 (Firefox timing with 0019).
- cameraSim preset: both VRAM compression options on (user); the lead set `for_desktop`/`for_mobile` = true in the user's
  `export_presets.cfg` (backup in the lead's scratchpad), verifying with a scratch re-export.
- Task 19: orientation default follows the project setting; the shell enters fullscreen on the first tap on phones (export
  option to turn it off). worker-19 told.

## 2026-10-25 17:45 — lead verification of 0019; 18 merged; 19 blocked on the phone lock

- **0019 verified on the merged tree**: editor + web release template built (2:07, table re-baked), smoke OK (276/0, no
  `for` left), desktop `--parity --baked` **71/71** (bake gate pre-existing). **12 integrated.**
- **18 merged**: cameraSim (Ava) 5 views 0.00-0.03 from native, same as Tint and run-to-run noise; look posted on the
  board. Found: the user's web preset has both VRAM compression options off, so Ava exports untextured (board question).
- **19 blocked**: code done on work/19 (wake lock, `html/orientation`, manifest clamp fix, high-performance on the probe
  adapter); phone checks wait on the PIN lock. Two design questions on the board (orientation default, fullscreen on
  first tap).

## 2026-10-25 17:10 — task 12 merged; 19 out; 11 re-times with 0019

- **12 merged** (worker-12, finished by worker-12b): Chrome for Android on the Razr 60 works with SPIRV-Cross and is as
  fast or faster than Tint with **SPIRV-Cross patch 0019** (never emit WGSL `for`: Mali-G615 / Chrome 155 ran
  `for (var i = -3; i <= 3; i++)` zero times, volumetric fog lost its density, 12.6 from native). GPU ms/frame, Tint vs
  SC+0019, 3 rotated runs: fp 16.9 / 16.8, heavy 73.8 / 71.3, postfx 23.3 / 23.2, platformer 29.6 / 28.7, stress
  28.8 / 28.1. Phone Look set identical to Tint (34 frames). Desktop: smoke OK, 2566/2566 project stages valid and
  interface-identical with 0019. thirdparty/README.md line added. Phone setting restored, phone lease dropped.
- **Lead verification still owed**: build + smoke + desktop `--parity --baked` on the merged tree with 0019 (it changes
  every module's text). Deferred: worker-18 is running captures with the lead's `bin/` editor, so `bin/` must not be
  rebuilt until 18 hands back.
- Lease incident: worker-11's gpu lease expired at 16:50 mid timing run; 12b and then 18 took it. 11's runs since 16:33
  are contaminated; 11 is told to stop, wait for the lease (3 h next time), and re-time SPIRV-Cross with 0019.
- **19 out** (TASKS §2.36: wake lock, orientation option, performance hint), on the phone now that 12 is done.
- For TASKS (task 10): SSS on the phone is 11.1 from desktop Vulkan with both converters (pre-existing, §2.9);
  `fetch` keepalive 64 KB cap trap; phone_bench.mjs README note.

## 2026-10-25 14:55 — user answers; 12 resumed on the phone

- User: Firefox for Android is out of scope ("I only care about Firefox for desktop"; Linux Firefox needs the Vulkan /
  WebGPU prefs). Task 12 is Chrome for Android only; nothing installed on the phone.
- User: FSR2 after the reactive-mask fix matches Vulkan by eye.
- Razr 60 attached (ZY22LHCBJ6). worker-12 resumed: merge webgpu-4.8, re-export both sets (pre-fix exports), run.

## 2026-10-25 14:25 — task 17 integrated

- **17 merged**: FSR2's softness on WebGPU was not the capture phase. FSR2 read its reactive mask through
  `get_internal_texture_reactive()`, an alpha-swizzled view; WebGPU views cannot swizzle and the driver drops it
  silently, so reactivity was the scene's red channel. amd-fsr2 patch 0005 (`FFX_FSR2_OPTION_GODOT_REACTIVE_MASK_FROM_ALPHA`)
  reads `.a` and forward clustered passes the plain color view; native byte-identical. fsr2_1 Laplacian 13.35 vs 13.34
  (was 11.03), mean diff 0.142. FSR2 jitter at scale 0.5 has 32 phases: `JITTER_PERIOD` 32, fsr2 parityTol 0.3.
  Worker's full baked matrix 71/71.
- Lead on top: `drivers/webgpu/SCsub` now makes the precompiled table depend on `thirdparty/amd-fsr{,2}` shader sources
  (the gap 17 found); the merged build re-baked on its own. Editor + web release template build, smoke OK (276/0).
- **Lead verification (14:50): full `--parity --baked` on the merged tree 71/71** (fsr2 0.142, volumetric_fog 0.001,
  taa 0.006); RESULT FAIL only on the pre-existing bake gate (translated 97 > 80). **17 integrated.**
- Lead trap, fixed: the first lead run failed fsr2 (0.578) and volumetric_fog (0.071) because `--baked` reads
  `export_baked/`, which only `BAKE=1 ./export.sh` refreshes; it was from 11:28. `run_forward_plus.mjs` now refuses an
  export older than the template, the editor or `fp_scene.gd` (`--allow-stale-export` to override). §3 trap for task 10.
- Follow-ups for TASKS (task 10): trap "texture view swizzles silently dropped on WebGPU" — audit other swizzled
  views (`texture_storage.cpp`); FSR2 accumulate and scene set 1 binding 24 get the blank depth fallback (check with a
  moving camera and sky).
- FSR2 before/after look posted on the board.
- **12 blocked (14:30)**: phone never attached. work/12 has `webgpu_tests/perf_guard/android/phone_bench.mjs` (no CDP, so
  Firefox for Android too), `results/12-android/run_phone.sh` (~1.5 h), 17 desktop Vulkan references. Its exports are
  from e99b18555e, before 17's FSR2 fix: the relaunch re-exports from the then-current tree with `export_all.sh` first.
  Board: phone-attach, and whether to install Firefox Nightly if release Firefox for Android has no WebGPU.

## 2026-10-25 14:10 — task 08 integrated; 11 and 12 out

- **08 integrated**: desktop Chrome (4080 SUPER) GPU time, SPIRV-Cross everywhere vs Tint everywhere (a Tint-runtime
  template built at 62a570d31f): heavy Forward+ 1.43 vs 1.47 ms/frame median, baked and unbaked; perf_guard scenes
  (Forward Mobile) equal except benchmark_postfx +2% (0.014 ms/frame) — watch on Android. SSIL const-array: a
  `var<private>` patch measured no effect, kept unapplied as `results/08-gpu-timing/lut-var-private.candidate.patch`.
  Method for 11/12: `results/08-gpu-timing/compare.sh` + `summarize.mjs` (rotated order, GPU-free check per launch).
- Trap candidates for TASKS §3 (task 10): a `webgpu_wgsl_backend=tint` template also needs `WGSL_BACKEND=tint` in the
  environment, or it ships a SPIRV-Cross precompiled table; A/B templates must be built at the editor's exact commit.
- The phone now has its own lease (`phone`), separate from `gpu`, so 12 can run beside 11 and 17.
- Out: 11 (Firefox desktop, gpu), 12 (Razr 60, phone). The phone was not attached at 14:10 (`adb devices` empty).

## 2026-10-25 13:00 — task 13 integrated

- **13 integrated**: `run_pipeline()` validates its input (SPIRV-Tools, Vulkan 1.2) and rejects invalid modules cleanly;
  every optimizer pass checks `check_ir_preconditions()` first; `rewrite_copy_logical` capped (a valid 1e8-element
  OpCopyLogical took 6.2 GB / 51 s, now 8 MB); fuzzers instrument the passes (~5000 edges, all 27 passes), 10 min each
  clean. Cost of validation: ~2.3 ms a module, CLI corpus +15%.
- **Lead change on top (e99b18555e):** the in-engine runtime (`spirv_to_wgsl.cpp`) skips full validation — its input is
  the engine's own glslang output, and on the web it would be pure load time against the user's performance bar. The
  converter tool and fuzzers keep validating; size and per-pass checks stay everywhere.
- Merged tree: editor + web release template build (1:50), smoke OK, preprocessing 291/0/1.
- Open: `spec_op_probes.py`'s `op_FConvert_f32` probe is invalid SPIR-V, so its run_pipeline path is now rejected;
  run `run_spec_ops.sh` (gpu) at the next lead GPU slot and fix the probe or its expected list.
- `bin/` pair is now from e99b18555e.

## 2026-10-25 12:45 — task 14 integrated

- **14 integrated**: `freeze_spec_constant_ops()` evaluates CompositeExtract / CompositeInsert / VectorShuffle and
  vector ops on constant composites per component, and SRem/SMod with Vulkan's values; an op it cannot evaluate warns and
  becomes OpConstantNull instead of a silent 0. Frozen-path probes 40/49 → 48/49 (SPIRV-Cross), 39 → 47 (Tint). No engine
  shader is frozen, so engine output is unchanged (smoke identical). Merged tree: smoke OK, preprocessing 291/0/1,
  spec_constant_overrides 8/8, driver unit 379/0.

## 2026-10-25 12:40 — task 16: the fog "race" was the capture phase

- **16 integrated**: volumetric fog (and TAA/FSR2) jitter on `frame % 16`; the fixture captured on phase 0 or 1 depending
  on which boot frame the harness's first hash write landed in. Vulkan against itself gives the same 0.045 / 0.082.
  `fp_scene.gd` now starts every config so its capture lands on phase 0, web and native (`--fp-phase=N` to triage,
  `[FP] FRAME n (phase p)` logged). Fog: 0.001 from native, 10/10, SPIRV-Cross-baked and Tint-baked. **Full matrix
  SPIRV-Cross-baked: 71/71**; RESULT still FAIL only on the pre-existing bake gate (translated 97 > 80).
- Saved native `--reference` directories from before this are invalid.
- Task 17 (FSR2 softness) may be partly the same artifact: the measured frames were on different phases; re-measure
  first.

## 2026-10-25 12:20 — second wave out: 13, 14, 16, 08

- Workers now run with `model: "opus"` (Opus 5.5) on each Agent call, effort medium: the user's settings set
  `CLAUDE_CODE_SUBAGENT_MODEL=sonnet`, so workers 01-06 ran on Sonnet. Tasks 13/14/16/17 had been held waiting for a
  /config change that the per-call parameter makes unnecessary.
- Out: 13 (harden passes), 14 (spec composite fold), 16 (baked fog race, gpu), 08 (desktop timing, gpu). 08 and 16 share
  the gpu lease. Next: 11 (Firefox), 12 (Android), 17 (FSR2 sharpness) when the GPU frees up.

## 2026-10-25 12:10 — the user's looks

FSR2, SSIL/SSAO, contact shadows/SSR: match. Fog: "can't tell them apart" but wants the race fixed (task 16 stays).
FSR2: "a little more pixelated" on web → measured: identical with Tint before the swap (Laplacian 11.04 vs native
12.82), so not the swap → task 17.

## 2026-10-25 12:05 — Forward+ parity: 71/71 with every shader converted by SPIRV-Cross

- Pair rebuilt from 62a570d31f (editor + web release nothreads). **Unbaked export (every stage converted at runtime by
  SPIRV-Cross): 71/71 at parity with native Vulkan.** That is also task 03's real-GPU boot check → 02, 03 integrated.
- Baked export: 70/71. Volumetric fog flips between two exact states per run (0.046 pass / 0.082 fail) in baked
  exports, with Tint-baked exports from before the swap too (3/9) → existing race, task 16. Bake gate (translated 97 > 80)
  fails for every converter, pre-existing. Full table: results/07-forward-plus-parity/SUMMARY.md.
- Frames (Vulkan beside SPIRV-Cross WebGPU) for FSR2, SSIL, SSAO, contact shadows, SSR and fog are on the hand-offs page.
- GPU lease dropped.

## 2026-10-25 11:25 — task 07 first reading; task 15 fixed by the lead

- **Task 07, old template (2026-10-23 pair), exports baked by each converter:** Tint 71/71 at parity. SPIRV-Cross 0/71,
  because the contact-shadow module (line 241, barrier uniformity) errors on every page load and the harness fails a
  feature on any driver error. Frame by frame, 133 of 142 frames match Tint's within 0.1/255; ambient_light, fxaa and
  contact_shadows render black; SSR loses its reflections (the old template lacks task 02's depth fix); light projector
  (0.5) and FSR2 (0.3) differ slightly. ~97 stages per run were converted at runtime by the old template's Tint, so this
  reading is partial either way.
- **Task 15 done by the lead** (critical path; new workers wait for the user's /config change): patch 0015, uniform
  compute builtins as parameters. Both reproducers valid; smoke 276/276; the real-project corpus (2566 unique stages)
  2566/2566 valid and interface-identical (was 2562).
- Next: re-export fp_check baked + unbaked with a pair rebuilt from this commit, `--parity` again.

## 2026-10-25 11:12 — user: ccache 50 GB; MetaHuman = cameraSim's Ava

- ccache config set to 50 GB, matching build-linux.sh's `CCACHE_MAXSIZE=50G` (base_dir shared across worktrees).
- MetaHuman: cameraSim's main scene has Ava (`character/Ava/head.dna`); task 05 exported it with all_resources, so its
  materials are already in the corpus: 189 of cameraSim's 1318 stages are hair/strand/groom/skin, all convert,
  interface-identical, valid (the 4 invalid stages are all contact shadows, task 15). No separate MetaHuman export.
- Task 07 first reading: Tint baseline 71/71 at parity; the bake gate fails for both (translated 97-99 > 80: the old
  template converts 8 Forward+ scene groups at runtime), so the SPIRV-Cross reading on the old template is partial.
  Next: re-export with the merged pair (2c2ebe96a1, runtime converter = SPIRV-Cross), baked and unbaked.

## 2026-10-25 11:10 — 03 and 02 merged; merged tree builds and smokes

- **03 merged** (e05c777b8d): in-engine converter on SPIRV-Cross (`webgpu_wgsl_backend=spirv-cross|tint`,
  `-fwasm-exceptions` on its TUs + link), all 8 web variants linked on its branch; release nothreads wasm 1.6% smaller
  raw / 2.0% gzip than Tint (SPIRV-Tools stays). Booted on swiftshader only (translated=502).
- **02 merged** (2c2ebe96a1, include-order conflict with 03 resolved): `wgsl_text_scan.{h,cpp}` replaces ~1,500 lines of
  `strstr` scanning; found and fixed SSR downsample's `source_depth` (SPIRV-Cross's `let _N = textureLoad(...)` shape)
  that would have got the blank depth fallback.
- Merged tree (2c2ebe96a1): `build-linux.sh` editor + web release nothreads, 4 min, no warnings in the touched files;
  smoke OK (276/276 valid, interface identical); `wgsl_text_scan/run_tests.sh` 276/276 both modes.
- `bin/` now holds the editor + template from 2c2ebe96a1 (the parity run in flight uses copies of the 2026-10-23 pair).
- ccache: build-linux.sh already exports `CCACHE_MAXSIZE=50G`; the config limit only bit SCons runs outside it (now 20 GB,
  before 5 GB — why the cache sat full). Question on the page: match 50 GB.

## 2026-10-25 11:05 — task 05 merged

- **05 merged** (5ec31c3aa4; smoke after the parity run frees the GPU). Real projects through both converters: fp_check
  882 stages, sewer-shift 1224, cameraSim 1318, smoketest projects 1936 (2566 unique): SPIRV-Cross converts every stage
  Tint does, interfaces identical, 2562/2566 valid in Chrome, output 1.09-1.14x Tint's size. Harness:
  `spirv_cross_eval/project_export.sh` + `project_eval.py`. Its patch 0005 duplicated 0006 (same 16-bit norm formats,
  written in parallel) and was dropped at merge.
- **New task 15**: the 4 screen-space contact shadow variants fail barrier uniformity (builtins copied to
  `var<private>`); a Forward+ feature, so expect parity to flag contact shadows.
- Question for the user (on the page): a MetaHuman export to add to the corpus, or skip.

## 2026-10-25 10:50 — the user's answers (hand-offs page, all twelve)

- GPU: game closed, go ahead → task 07 parity running (gpu lease: lead), Tint baseline then SPIRV-Cross.
- Acceptance bar widened (README goal): + Firefox, + Razr 60, + cameraSim by eye; Firefox and Android Chrome must be
  as fast as Tint or faster → tasks 08 (desktop Chrome), 11 (Firefox), 12 (Android) measure against Tint-baked exports.
- Rename `tint_convert_cli` → `wgsl_convert_cli`; delete `thirdparty/tint` once the bar holds → task 09.
- Harden the passes against malformed SPIR-V now → task 13. Fix the spec-composite fold inside the swap → task 14.
- Workers: Opus 5, effort medium. The Agent tool cannot select Opus 5; the user sets the default sub-agent model in
  /config. New workers (13, 14, then 08, 11, 12) wait for that.
- ccache: 20 GB (`ccache -M 20G`; the user asked whether 20 is enough instead of 40 — start at 20, watch evictions) and
  `base_dir=/mnt/109313D2109313D2/godot-editors` so worktrees share it. Before: 5.6 GB used, 72% hits.
- Worktrees removed after merge (branches kept): -01, -04, -06.
- sewer-shift: `/mnt/109313D2109313D2/godot-game-dev/sewer-shift`; cameraSim: `~/Downloads/cameraSim_2026-09-09_10-26-06/testing`
  → sent to worker-05.

## 2026-10-25 10:15 — task 06 integrated

- **06 integrated** (merge bead486f41). On the merged tree: smoke OK (276/276 valid, interface identical);
  preprocessing_tests 291 passed / 1 skipped with both converters (32 assertions had matched Tint's spelling); driver
  unit tests 379/0; spec_constant_overrides 8/8; shader_corpus 14/14. Every offline tier takes `TINT_CONVERT_CLI=<path>`.
  SPIRV-Cross patch 0006 (16-bit norm storage formats; CanvasSdf had regressed). Fuzzers fixed (they were broken before
  the swap) and ported.
- Findings: fuzz asserts in SPIRV-Tools on malformed SPIR-V in the shared passes (both backends, low priority,
  reproducers in results/06/fuzz_repro) → TASKS.md §2 at task 10. SPIRV-Cross's `const` arrays vs Tint's `var<private>`
  (SSIL hoist, §2.30) → task 08 checks it first.
- Not run anywhere yet: every render tier (GPU busy with the game).

## 2026-10-25 10:00 — task 04 integrated

- **04 integrated** (merge f62806e2e3): `spec_constants_overridable()` unchanged on evidence; engine: 183 variants
  without spec constants, 93 overridable (216 overrides), 0 frozen, same under both backends. 44 probe modules mapped
  what SPIRV-Cross lowers (SUMMARY in results/04). New: `tint_convert_cli --spec-info` / `--raw`,
  `spirv_cross_eval/run_spec_ops.sh`. Docs text waits in results/04/for-docs.md for task 10.
- **Follow-up found, not swap-caused:** `freeze_spec_constant_ops()` folds CompositeExtract / CompositeInsert /
  VectorShuffle of a spec composite to 0 (same with Tint). No engine shader takes that path; a user shader could.
  Goes into TASKS.md §2 at task 10 unless a worker picks it up first.

## 2026-10-25 09:58 — task 01 integrated

- **01 integrated** (merge 6bfc543fd2): SPIRV-Cross patch 0001, barrier/load/barrier on a workgroup variable becomes
  `workgroupUniformLoad`; smoke on the merged tree 276/276 valid in Chrome, interface identical, expected-invalid list
  empty. Only the FSR2 `no_image_atomics` module's WGSL changed. FSR2 still has to be seen rendering (task 07).
- Worktree traps worker-01 hit, both fixed on webgpu-4.8: `validate_wgsl.mjs` finds Playwright in the main checkout
  when a worktree has no node_modules; `build.sh` throws away SPIRV-Cross objects after any SPIRV-Cross header edit
  (stale objects gave `free(): invalid pointer`). Workers on older bases: merge webgpu-4.8 into your branch for these.

## 2026-10-25 09:50 — workers 01-06 out; first exports

- Workers spawned 09:41 for tasks 01-06 (one worktree each, `../godotwebgpu-0N`, branch `work/N`).
- Their six first `build.sh` runs at once peaked at ~250 compilers, 26 GB, load 147 (available memory fell to ~10 GB
  with the game + Ghidra up); it recovered within two minutes. `build.sh` now takes `JOBS=<n>`; worktrees building at
  once should use `JOBS=6`.
- Task 07 exports made (fp_check, baked, existing editor/templates from 2026-10-23, bake cache off): Tint
  `index.shaders.pck` 29.6 MB raw / 4.43 MB gzip -9; SPIRV-Cross 34.8 MB raw / **3.70 MB gzip** (−17% compressed).
  Parity runs wait for the GPU (the game holds ~8-10 GB VRAM).

## 2026-10-25 09:40 — board opened

Foundation integrated (task 00, see its file): `thirdparty/spirv-cross-wgsl` vendored, `spirv_cross_wrapper`,
`tint_convert_cli` builds on SPIRV-Cross by default (`WGSL_BACKEND=tint` for the reference), `smoke.sh` OK on the lead
tree (276/276 converted, interface identical, 275/276 valid — FSR2, task 01).

| task | what | state | owner |
|---|---|---|---|
| 00 | foundation | integrated | lead |
| 01 | FSR2 barrier uniformity (SPIRV-Cross patch) | integrated (render check in 07) | worker-01 (retired) |
| 02 | driver WGSL text scans | integrated | worker-02 (retired) |
| 03 | runtime converter in wasm, exceptions, SCsub | integrated | worker-03 (retired) |
| 04 | spec constants | integrated | worker-04 (retired) |
| 05 | real projects' shaders | integrated | worker-05 (retired) |
| 06 | offline test tiers | integrated | worker-06 (retired) |
| 07 | Forward+ parity vs Vulkan | unbaked 71/71; baked 71/71 after 16; repeat at 09 | lead |
| 08 | GPU timing (desktop Chrome) | integrated: equal or faster | worker-08 (retired) |
| 09 | remove Tint, rename to wgsl_convert_cli | integrated | worker-09 | d3cf5ff893 |
| 10 | docs | integrated | worker-10 |
| 11 | Firefox desktop: works, as fast as Tint | integrated: equal correctness and speed | worker-11 (retired) |
| 12 | Android Chrome (Razr 60): works, as fast as Tint (Firefox Android out of scope, user) | integrated (patch 0019; lead parity 71/71) | worker-12, 12b (retired) |
| 13 | harden passes against malformed SPIR-V | integrated | worker-13 (retired) |
| 14 | spec-composite fold fix | integrated | worker-14 (retired) |
| 15 | contact shadows barrier uniformity (SPIRV-Cross patch) | integrated (patch 0015) | lead |
| 16 | baked volumetric fog "race" (capture phase) | integrated | worker-16 (retired) |
| 19 | web export: wake lock, orientation, performance hint (TASKS §2.36, user) | integrated (lead build, smoke, unit 379/0, export) | worker-19 (retired) |
| 18 | cameraSim by eye (user look, bar for 09) | merged: 5 views 0.00-0.03 from Vulkan; look posted | worker-18 (retired) |
| 17 | FSR2 softer on WebGPU than native (reactive mask read red) | integrated (lead parity 71/71) | worker-17 (retired) |

**GPU:** the Oblivion Remastered game was running at 09:35 (8.4 GB of 16 GB VRAM); real-GPU runs wait for it or for the
user's go-ahead.

Kept outside git: `bin/tint_convert_cli.tint-pre-swap` (the Tint converter exactly as of the tag `pre-SPIR-V-Cross`).
