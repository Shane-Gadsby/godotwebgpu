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

There is **no known-broken shipped feature**. Everything in §2 is hardening, measurement, coverage or
a rare non-visual error.

### Verified tiers

| tier | result | when / where |
|---|---|---|
| native editor + web template build | 0 errors, 0 warnings, incl. `warnings=extra werror=yes` | 2026-10-05, 4.8 port (§4.15) |
| `wgsl_precompile.py` | 274 compiled, 0 glsl / 0 tint failures; **deterministic** (two runs, different `PYTHONHASHSEED`, byte-identical 1,806,893 B header) | 2026-10-14, `597d27e09` (§2.16 gap 5) |
| `shader_corpus` | 14 / 0 | 4.8 |
| `driver_unit_tests` | 370 / 0 | 2026-10-16 |
| `preprocessing_tests` | 244 / 0 / 1 skip | 2026-10-16 |
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
| Android / iOS | **never run** | §2.9 |

Reference numbers: the user's project cold-loads with a **~1.0 s** stall (was ~9.2 s),
`{baked: 360, translated: 0}`; BC1 compression is equivalent to uncompressed at RMSE 0.0006.
Over a **20 Mbit/s** link with gzip, a baked export of `3d/platformer` reaches its first frame in
**12.9 s** (was 25.7 s before §2.27's SPIR-V drop; the unbaked export takes 17.7 s), pck 65.4 MB / 11.2 MB
gzipped — see §2.27 and §4.15 before optimizing anything CPU-side for load time.

### What is built right now — snapshot 2026-10-16 (end of the render-thread day), HEAD after `2c3544102e`

**Read the 2026-10-16 additions first:** §2.5 (the separate web render thread is implemented and measured — design, traps,
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
| `godot.linuxbsd.editor.x86_64` | rebuilt 2026-10-16 with `webgpu=yes` at the tree of the §2.5 render-thread commit (baker enabled). Matches the three web templates below |
| `godot.web.template_release.wasm32.nothreads.zip` | rebuilt 2026-10-16 at the same tree (normal build, no `dev_mode`; the changed files were `-Werror`-checked as objects). Scene smoketest 21/21 Chrome + Firefox, Forward+ 64/64 on it |
| `godot.web.template_release.wasm32.nothreads.dlink.zip` | from the CI run before the late rebuilds (19:10). Compile check only — nothing loads it. **Stale** w.r.t. HEAD |
| `godot.linuxbsd.editor.dev.x86_64` | `dev_build=yes` native build from 2026-10-16 (compile check of the §2.5 server changes); not for exports |
| `godot.web.template_release.wasm32.zip` (threads) | rebuilt 2026-10-16 at the same tree; runs the Separate render thread with `thread_model=2` (§2.5). `template_debug` zips are **stale** (Oct 14-16, various trees) |
| `godot.linuxbsd.editor.double.x86_64` | §2.23's `precision=double` check; keep or delete freely |

Scratch state worth knowing about (all outside git): `/tmp/plat_baked`, `/tmp/plat_unbaked` (platformer
exports used for the §2.27 re-measurement), `/tmp/fpref*` (native-Vulkan reference frames). `webgpu_tests/
scene_smoketest/exports/*` and `forward_plus/export{,_heavy}` are regenerated by CI and gitignored.

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
- Never run two `scons` invocations in one tree at once (shared `.sconsign.dblite`).
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
TINT_DEBUG_DUMP_PREPROCESSED=/tmp/out.spv ./bin/tint_convert_cli <file.spv>
WGSL_DEBUG_DUMP=<substring> drivers/webgpu/wgsl_precompile.py   # dumps raw SPIR-V to /tmp/wgsl_debug_dump/
```

`capture_errors.mjs` and `run_scenes.mjs` share three Chrome modes: `WEBGPU_REAL_GPU=1`, `CI=1`
(bundled Chromium on swiftshader — **this is the CI adapter**, use it to reproduce a CI-only failure
locally instead of pushing), or neither (the system's headed Chrome, which is what most recorded
results were measured on). Playwright cannot drive the user's own Firefox build.

---

## 2. Outstanding work — most to least critical

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
| 2.27 | baked containers are 79% of the download | OPEN (baking wins at 20 Mbit) | variant pruning, unsafe without an opt-in design |
| 2.4 | startup fixed floor | EFFECTIVELY DONE (stage loop 391 → ~180 ms; tail attributed) | nothing tunable; lazy renderer init (FSR/GI storage) is design work; unbaked Tint (~6 s) is by design |
| 2.5 ✔ | threading decision | **IMPLEMENTED 2026-10-16**, open for tooling — separate render thread for `threads=yes` + `thread_model=2` (Chrome + Firefox, dlink too); main thread −57% (platformer) to −97% (heavy) per frame; `requestQuit()` clean | `perf_guard` tools see only the main thread under Separate; uncapped fps unmeasurable there (Chrome rAF ~60 Hz with a worker-owned canvas); Safari/mobile never run |
| 2.6 ✔ | real-GPU profile | DONE 2026-10-15 (baked heavy: `translated 0`, stall 18.5 -> 7.7 s) | `rwShadowRefreshes` 40 -> 4/frame on 2026-10-16: they were a detection bug, not inherent (§2.30) |
| 2.7 | upstream F10 default | OPEN, human | — |
| 2.8 | Safari | BLOCKED | needs a Mac |
| 2.9 | Android / iOS | BLOCKED | needs devices |
| 2.10 | storage-format-by-variant audit | OPEN (tables clean) | coverage |
| 2.11 | VoxelGI 1.6× Vulkan | CLOSED (7.743 vs 7.742) | — |
| 2.12 | dead `_depth_alias` | CLOSED | — |
| 2.13 | stale Phase 7 TODOs | CLOSED | — |
| 2.14 ✔ | base-class interface audit | OPEN (audit done; cameraSim swept clean 2026-10-15) | more real projects, dlink export, native side-by-side |
| 2.28 | compute lists share one encoder | FIXED (end hook) | optional per-bind bookkeeping cleanup (the backstop is still needed within a list) |
| 2.29 ✔ | baked export as a test tier | DONE 2026-10-15 (matrix, smoketest, WebRTC legs; deltas re-baselined) | `ambient_light` bimodal on unbaked, not chased |
| 2.30 ✔ | unbottleneck the CPU (new) | OPEN, mostly done — 2026-10-16: SSIL hoist pass, read-write storage textures, upstream shadow-atlas bug (heavy CPU −37%), prepare-for-use trait, Web Audio writes, PR #123996; heavy GPU 3.40 → 1.46 ms, 229 → 292 fps; engine wasm CPU is flat (per-function comparison, `-O2`/LTO no gain) | the JS marshaling path (~0.5 ms/frame ceiling 0.02-0.05 ms), batching a pass's commands into one JS call (driver redesign) |
| 2.15 ✔ | `generate_lods()` returns 0 on web | CLOSED 2026-10-15 (GPU readback returns zeros, §2.13; not LOD code) | — |
| 2.16 ✔ | test-suite gaps | OPEN (gap 4 partly via `perf_guard`; gaps 5, 7 closed) | regression corpus, error-path, async readback, frame-time/compile/texture perf guards, Safari |
| 2.17 ✔ | Forward+ benchmark leg / README numbers | DONE 2026-10-15 (capped + corrected uncapped matrices; F+ vs Mobile profiled; heavy scene 176 -> ~220 fps via shadow-variant batching, `firstInstance`, mesh-first order) | uncapped WebGL needs another harness |
| 2.18 | texture compression export option | BLOCKED | — |
| 2.19 ✔ | interleaved-build stale object | OPEN (not reproduced on 4.8, 2026-10-15) | wait for a recurrence; capture `--debug=explain` |
| 2.20 | `SUPPORTS_HALF_FLOAT` experiment | OPEN | — |
| 2.21 | deferred emdawnwebgpu features | OPEN | — |
| 2.22 ✔ | small perf items | DONE 2026-10-15 (INITIAL_MEMORY: 0.1 ms of grows, not a lever; serve-compressed documented) | side-module size is module-level only |
| 2.23 | Box3D remainders | OPEN (`precision=double` builds) | — |
| 2.24 | upstream the driver | OPEN, human | — |
| 2.25 / 2.26 | side projects (Node server, `.gdb`) | not started | — |

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
| 2.27 | Baked containers: multiview SPIR-V dropped (−59%); baking now beats not baking at 20 Mbit (12.9 s vs 17.7 s); pruning is an opt-in decision | `OPEN` — re-measured 2026-10-14 | ~0.5 session |
| 2.4 | Startup's fixed floor: `Servers:Rendering` + first frame | `EFFECTIVELY DONE` — stage loop 391 → ~180 ms; remainder attributed (a ~1.1 s tail of wasm CPU, no dominant term); only lazy renderer init is left, not planned | design work only (lazy renderer init), not planned |
| 2.5 | Decide whether threading is worth having | `IMPLEMENTED 2026-10-16` (open: perf tooling for the render thread, Safari/mobile) | done; tooling ~1 session |
| 2.6 ✔ | real-GPU profile | DONE 2026-10-15 (baked heavy: `translated 0`, stall 18.5 -> 7.7 s) | `rwShadowRefreshes` 40 -> 4/frame on 2026-10-16: they were a detection bug, not inherent (§2.30) |
| 2.7 | Offer F10's default change upstream | `OPEN` | **human** |
| 2.8 | Safari has never been run | `BLOCKED` | **human, needs a Mac** |
| 2.9 | Android / iOS browsers | `BLOCKED` | **human, needs devices** |
| 2.10 | Storage-format class: tables audited clean; binding half open | `OPEN` — audited 2026-10-14 | coverage, not a session |
| 2.11 | ~~VoxelGI measures 1.6× native Vulkan~~ — real 3D-upload bug, fixed | **`CLOSED` 2026-10-14** | — |
| 2.12 | ~~Delete dead `_depth_alias`~~; reverse fallback deliberately not added | **`CLOSED` 2026-10-14 → §4.12** | — |
| 2.13 | ~~Stale Phase 7 TODOs: 7.8, 7.15, 7.17~~ | **`CLOSED` 2026-10-14 → §4.2** | — |
| 2.14 ✔ | base-class interface audit | OPEN (audit done; cameraSim swept clean 2026-10-15) | more real projects, dlink export, native side-by-side |
| 2.28 | Compute lists share one encoder: now `command_end_compute_pass` ends the pass | `FIXED` 2026-10-14, Forward+ matrix 64/64; optional per-bind bookkeeping cleanup not done (backstop still needed) | — |
| 2.29 ✔ | baked export as a test tier | DONE 2026-10-15 (matrix, smoketest, WebRTC legs; deltas re-baselined) | `ambient_light` bimodal on unbaked, not chased |
| 2.15 ✔ | `generate_lods()` returns 0 on web | CLOSED 2026-10-15 (GPU readback returns zeros, §2.13; not LOD code) | — |
| 2.16 ✔ | test-suite gaps | OPEN (gap 4 partly via `perf_guard`; gaps 5, 7 closed) | regression corpus, error-path, async readback, frame-time/compile/texture perf guards, Safari |
| 2.17 ✔ | Forward+ benchmark leg / README numbers | DONE 2026-10-15 (capped + corrected uncapped matrices; F+ vs Mobile profiled; heavy scene 176 -> ~220 fps via shadow-variant batching, `firstInstance`, mesh-first order) | uncapped WebGL needs another harness |
| 2.18 | Texture compression as an export option | `BLOCKED` | needs 2.8 / 2.9 |
| 2.19 ✔ | interleaved-build stale object | OPEN (not reproduced on 4.8, 2026-10-15) | wait for a recurrence; capture `--debug=explain` |
| 2.20 | `SUPPORTS_HALF_FLOAT` experiment | `OPEN` | 1-2 sessions, needs real GPU |
| 2.21 | Deferred emdawnwebgpu feature adoption (4 items) | `OPEN` | ~1 session each |
| 2.22 ✔ | small perf items | DONE 2026-10-15 (INITIAL_MEMORY: 0.1 ms of grows, not a lever; serve-compressed documented) | side-module size is module-level only |
| 2.23 | Box3D remainders: `precision=double` checked; gaps deliberate | `OPEN` — checked 2026-10-14 | open-ended gaps only |
| 2.24 | Upstream contribution of the driver | `OPEN` | **human** |
| 2.25 | Side project: Node dedicated-server backend | not started | weeks, **human-scoped** |
| 2.26 | Side project: Braced GDScript (`.gdb`) | not started | ~1-2 weeks, **human-scoped** |

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

### 2.27 Baked shader containers are 79% of the download `OPEN` — baking now wins; pruning is optional *(found closing §2.3)*

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

### 2.5 Decide whether threading is worth having `OPEN` *(old Task 14 subtask 1.5.8.5, Task 12)*

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

### 2.9 Android / iOS browsers `BLOCKED` — needs devices *(old Task 5.2)*

Desktop is confirmed across Chrome, Firefox, Edge, Vivaldi and Safari on macOS/Windows/Linux (Linux
needs Vulkan+WebGPU flags and a native package, not Flatpak/Snap). Chrome/Android and Safari/iOS 18+
have never been tried. This is also what §2.18 exists to serve.

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

All three answered; none needed a fix beyond one warning. 7.15 and 7.17 are no-leaks, 7.8 is an
inherent property of asynchronous readback in single-threaded wasm, now stated in the code and in
§4.2.

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
4. **Frame-time guards** in `perf_guard` (it only guards call counts and bytes today) and an **uncapped WebGL leg** (needs another harness; Chrome's GL present path stays at ~165).
5. **Threading** — §2.5, after 1-2 are re-measured.
**Not worth repeating:** engine `-O2`/`optimize=speed`; global INITIAL_MEMORY; depth-list copying; assuming a slow-looking `writeBuffer` is a copy (it is usually the main thread waiting on the GPU process).

### 2.18 Texture compression as an export option `BLOCKED` *(old Task 39)*

Desktop is settled: BC formats work in both browsers, BC1 equals uncompressed at RMSE 0.0006. Safari
and mobile are exactly the platforms the option exists to serve, and both are unmeasured — blocked on
§2.8 / §2.9.

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

### 2.26 Side project: Braced GDScript (`.gdb`) — not started

Plan written 2026-09-29 against the 4.7.2 sync. GDScript with `{ }` blocks in `.gdb` files, compiled
by the same module so `.gd` and `.gdb` mix freely; the extension is the opt-in and a lossless
converter moves files either way. The core trick is translating braces into the
`COLON`/`NEWLINE`/`INDENT`/`DEDENT` tokens the parser already consumes, leaving parser, analyzer,
compiler, VM, debugger and completion untouched — which is what keeps feature parity automatic and
the upstream diff small. Plus a formatter for both file types. 21 requirements and a phased plan are
in `webgpu_notes/Braced GDScript (.gdb) — Implementation Plan.md`, which is **still on disk** — it is
gitignored, so unlike every other retired doc it is not recoverable from history. Nothing built.

---

## 3. Traps and lessons — read before debugging

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
   designing against the wrong one cost a full implementation. `TINT_DEBUG_DUMP_PREPROCESSED`.
9. **WGSL captured from the engine is not Tint's output** — it has already been through the driver's
   own text passes. `tint_convert_cli` on the same SPIR-V is. Diffing the two is a two-minute check
   and it is what finally attributed the depth-promotion bug correctly.
10. **`tint_convert_cli --batch` hides its own crash diagnostics** — it forks per shader and redirects
   the child's output to `/dev/null` so one Tint ICE cannot kill the batch. Run it single-file,
   un-forked, to see the real abort.
11. **When a new shader fails Tint conversion**, the fix is almost always a new/extended SPIR-V
    preprocessing pass or a vendored Tint patch — not a WGSL-writer change.

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
    preprocessing pass or an engine `.glsl` rebuilt `tint_convert_cli` and the runtime path, but the precompiled engine
    shaders kept the old WGSL — the header was a day old after a pass change, with nothing saying so. `drivers/webgpu/SCsub`
    now lists the pass sources and `servers/rendering/renderer_rd/shaders/**/*.glsl`. Vendored Tint is still not listed:
    after a Tint patch, delete the header by hand. Check with `ls -la drivers/webgpu/wgsl_precompiled.gen.h` and the
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

The pipeline is `GLSL → SPIR-V (glslang) → 12 binary-rewriting passes → WGSL (Tint, linked in) → GPU`,
with Tint vendored under `thirdparty/tint` (patches tracked in `thirdparty/README.md`).

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

---

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
| `Braced GDScript (.gdb) — Implementation Plan.md` | the `.gdb` language plan | §2.26 — **this one file was left on disk**: it is explicitly gitignored (`.gitignore:406`), so it is not in git history and deleting it would lose it |

**Still live, and not duplicated here**: `CLAUDE.md` (build/test commands, architecture, conventions),
`drivers/webgpu/README.md` (driver-local notes), `webgpu_tests/README.md` (how to run every tier),
`webgpu_tests/screenshot_comparison/LIVE_REPRO_METHODOLOGY.md` (how to test a reported bug against a
real project), `webgpu_tests/forward_plus/README.md` (the add-a-feature rule), `misc/emsdk_patches/`
(the toolchain patches and how to add a third), `thirdparty/README.md` (vendored Tint patches), and
`webgpu_site/{ARCHITECTURE_AND_DESIGN,TECHNICAL_REFERENCE,PERFORMANCE_AND_OPTIMIZATION,CORRECTNESS_AND_COMPATIBILITY,FAQ}.md`.
