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
| `wgsl_precompile.py` | 274 compiled, 0 glsl / 0 tint failures | 4.8, byte-identical to 4.7.2 |
| `shader_corpus` | 14 / 0 | 4.8 |
| `driver_unit_tests` | 370 / 0 | 4.8 |
| `preprocessing_tests` | 238 / 0 / 1 skip | 4.8 |
| `resource_lifecycle` | 6 / 0 | 4.8 |
| `screenshot_comparison` | 8 / 0 | 4.7.2 |
| scene smoketest (Chrome, container) | 20 pass / 0 fail / 0 skip | 4.8, swiftshader → **Forward Mobile** |
| scene smoketest (real GPU) | 21 / 0 on Chrome **and** Firefox | 2026-10-14, after §2.1 |
| scene smoketest (swiftshader) | 21 / 0 | 2026-10-14, after §2.12's deletion and §2.13's warning |
| Forward+ feature matrix | **64 / 64** on real GPU, Chrome; 0 errors over two reload-free passes | 2026-10-14, §2.1 closed |
| Forward+ matrix, **baked** export | **64 / 64**, deltas within 0.00% median of the unbaked run | 2026-10-14, §2.27 — first tier run ever to load a baked export |
| WebRTC parity | 9 fixtures × 3 export variants × 2 browsers = 54 identical native-vs-web diffs | 4.8 |
| WebRTC congestion | native/web agree at 91-97% on every transfer mode | 4.8, after the F10 fix |
| real-hardware Forward+ (RTX 4080 SUPER) | Chrome 0 errors, Firefox 0 real errors, both report Forward+, output matches native Vulkan | 2026-10-05 (§4.15) |
| Safari | **never run** | §2.8 — macOS only |
| Android / iOS | **never run** | §2.9 |

Reference numbers: the user's project cold-loads with a **~1.0 s** stall (was ~9.2 s),
`{baked: 360, translated: 0}`; BC1 compression is equivalent to uncompressed at RMSE 0.0006.
Over a **20 Mbit/s** link with gzip, a baked export of `3d/platformer` reaches its first frame in
**25.7 s**, of which 23.5 s is download — see §4.15 and §2.27 before optimizing anything CPU-side for
load time.

### What is built right now — snapshot 2026-10-14, HEAD `b0be6b7529`

Working tree clean. `bin/` is **not** uniformly current, which matters because of the version-hash
trap below:

| artifact | built from | state |
|---|---|---|
| `godot.linuxbsd.editor.x86_64` | `0f0b1025b` | current (HEAD is one docs-only commit ahead) |
| `godot.web.template_release.wasm32.nothreads.zip` | `0f0b1025b` | current, and **matched to the editor** — proven behaviorally: a baked export loads with `baked: 423` and no cache-mismatch warning |
| `godot.web.template_release.wasm32.nothreads.dlink.zip` | `8cdfb073e` (12:53) | **stale** — missing §2.12's deletion, §2.13's warning and §2.27's container change. Rebuild before any dlink work |
| `godot.web.template_{debug,*threads*}` | `33ec3afb4` (Oct 13) | **stale**, all of them |
| `godot.linuxbsd.editor.double.x86_64` | `fa954729c` | §2.23's `precision=double` check; keep or delete freely |

So: the editor + non-dlink release template are a matched, current pair, and that is the pair every
tier and every measurement in this document used today. Anything else needs a rebuild first, web
last.

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
- Threaded builds need COOP/COEP cross-origin-isolation headers; nine of our own test servers do not
  send them.
- The user's real test project is `~/Downloads/cameraSim_*/testing`, not `webgpu_tests/test_project`.

### Tools

```bash
# Startup / perf profiling (no engine rebuild; works on any existing export)
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

**Numbers are stable.** A closed item keeps its number, is marked `CLOSED` here and points into §4,
so references from source comments and from this file stay valid. Re-order by moving rows, not by
renumbering.

**Status reconciled 2026-10-14**, end of a long session. Every row below was checked against the tree
on that date; anything with no dated note is untouched since it was first written.

| what happened | items | commits |
|---|---|---|
| **Closed** | §2.1 bind-group layout mismatch (fixed), §2.2 non-dlink abort (gone), §2.3 dlink under throttling (measured, premise refuted), §2.12 dead `_depth_alias` (deleted), §2.13 stale Phase 7 TODOs (all three answered) | `8cdfb073e`, `b62681b3e`, `67d3b53bf`, `9622f9c12`, `fa954729c` |
| **Landed, still open** | §2.27 — multiview SPIR-V no longer baked: pck 150.76 → 62.36 MB (−59%) | `9aabb1e73`, `6c5d83a57`, `b0be6b752` |
| **Opened** | §2.27 (from §2.3's measurement), §2.28 (from §2.14's audit) | `325b8d297`, `d5206c11c` |
| **Audited, narrowed on evidence** | §2.6 (half measured), §2.10 (tables clean; reframed as coverage), §2.14 (interface audit done, baseline `9973b23ec7`), §2.23 (`precision=double` builds and runs) | `9973b23ec`, `d5206c11c`, `0f0b1025b` |
| **Description updated with new numbers** | §2.4, §2.16 (a seventh gap), §2.22 | `325b8d297`, `67d3b53bf` |

Nothing was pushed; every commit above is local on `webgpu-4.8`.

**Where to pick up.** The GPU is free again (see the VRAM note in §3), so the items that were blocked
on it are the ones to take: §2.28 (~0.5 session, a structural fix with a measurable before/after),
§2.6's remainder (frameStats under load, <0.5 session), §2.11 (VoxelGI's unexplained 12.24 vs 7.74).
§2.27's next step (prune unreachable variants) is the largest remaining win but needs analysis
first, and §2.4 is now known to be 10× smaller than §2.27 on a real link, so it should wait.

| # | item | status | effort (Opus 5, medium) |
|---|---|---|---|
| 2.1 | ~~Bind-group layout mismatch — rare residual~~ | **`CLOSED` 2026-10-14 → §4.19** | — |
| 2.2 | ~~Confirm the non-dlink export abort is gone on 6.0.9~~ | **`CLOSED` 2026-10-14 → §4.5** | — |
| 2.3 | ~~Profile a dlink export under network throttling~~ | **`CLOSED` 2026-10-14 → §4.15** | — |
| 2.27 | Baked containers: multiview SPIR-V dropped (−59%); baking now beats not baking at 20 Mbit (12.9 s vs 17.7 s); pruning is an opt-in decision | `OPEN` — re-measured 2026-10-14 | ~0.5 session |
| 2.4 | Startup's fixed floor: `Servers:Rendering` + first frame | `OPEN` | 2-3 sessions |
| 2.5 | Decide whether threading is worth having | `OPEN` | 2-3 sessions, needs real GPU |
| 2.6 | Real-GPU profile of a baked Forward+ export | `OPEN` — half measured 2026-10-14 | <0.5 session, needs real GPU |
| 2.7 | Offer F10's default change upstream | `OPEN` | **human** |
| 2.8 | Safari has never been run | `BLOCKED` | **human, needs a Mac** |
| 2.9 | Android / iOS browsers | `BLOCKED` | **human, needs devices** |
| 2.10 | Storage-format class: tables audited clean; binding half open | `OPEN` — audited 2026-10-14 | coverage, not a session |
| 2.11 | ~~VoxelGI measures 1.6× native Vulkan~~ — real 3D-upload bug, fixed | **`CLOSED` 2026-10-14** | — |
| 2.12 | ~~Delete dead `_depth_alias`~~; reverse fallback deliberately not added | **`CLOSED` 2026-10-14 → §4.12** | — |
| 2.13 | ~~Stale Phase 7 TODOs: 7.8, 7.15, 7.17~~ | **`CLOSED` 2026-10-14 → §4.2** | — |
| 2.14 | ~~Interface audit~~ done; live project sweep still open | `OPEN` — audited 2026-10-14 | ~1 session (sweep) |
| 2.28 | Compute lists share one encoder: now `command_end_compute_pass` ends the pass | `FIXED` 2026-10-14, Forward+ matrix 64/64 | — |
| 2.15 | `generate_lods()` returns zero LODs on web | `OPEN` | ~1 session |
| 2.16 | Test-suite gaps (7 named) | `OPEN` | ~1 session each |
| 2.17 | Forward+ benchmark leg + README numbers | `OPEN` | 1-2 sessions, needs real GPU |
| 2.18 | Texture compression as an export option | `BLOCKED` | needs 2.8 / 2.9 |
| 2.19 | Interleaved-build stale object — real fix | `OPEN` | ~1 session |
| 2.20 | `SUPPORTS_HALF_FLOAT` experiment | `OPEN` | 1-2 sessions, needs real GPU |
| 2.21 | Deferred emdawnwebgpu feature adoption (4 items) | `OPEN` | ~1 session each |
| 2.22 | Small perf items (INITIAL_MEMORY, side-module size) | `OPEN` | ~0.5 session |
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

### 2.6 Real-GPU profile of a baked Forward+ export `OPEN` *(old Task 46 §6)*

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

### 2.14 Base-class interface audit, and a live sweep `OPEN`

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

### 2.15 `generate_lods()` returns zero LODs on web `OPEN` *(old Task 17.1, bug 3)*

`ImporterMesh.generate_lods()` produces **0** LODs in a web template and 9 in a native one (confirmed
by exporting the same fixture both ways). Runtime LOD generation therefore silently does nothing on
web. It is an engine-level gap, not a renderer bug, and not on the path a shipped project takes
(real projects get LODs from the importer, as `webgpu_tests/forward_plus`'s `lod_sphere.res` now
does), so it has never been chased.

### 2.16 Test-suite gaps `OPEN`

Six named gaps, each roughly a session, in rough value order:

1. **Named regression corpus** — no `regressions/` directory exists. Collect the SPIR-V inputs behind
   the bugs this project already fixed (`webgpu_tests/shader_corpus` is the natural home) with
   expected outcomes, so none can silently come back.
2. **Error-path tests** — nothing covers device-lost and recovery, OOM, invalid API use, context loss.
3. **Async readback tests** — nothing covers viewport capture correctness, GPU→CPU readback callbacks,
   or fence correctness under rapid submit cycles.
4. **Performance benchmarks in CI** — nothing guards frame time, shader-compile throughput or buffer
   upload against regression (warning-only is fine).
5. **WGSL determinism check** — nothing proves `wgsl_precompile.py`'s output is byte-for-byte
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

### 2.17 Forward+ benchmark leg and the README's numbers `OPEN` *(old Task 9.16)*

`webgpu_tests/benchmark/run_benchmark.sh` runs on Linux and macOS, and the WebGL and WebGPU/Forward
Mobile legs are live-verified. **No Forward+ leg exists anywhere**, and the README's `Key Stats` and
`Performance Benchmarks` tables are still inherited from upstream `dwalter/godotwebgpu`, covering
only WebGL vs WebGPU/Forward Mobile — which is not what this fork is for. Add the third leg
(`rendering_method="forward_plus"` per scene), run 7 scenes × 3 renderers on real hardware, and
replace the inherited tables. Legs 1-2 are mechanical; leg 3 is unproven and could surface a real
Forward+ bug, which is why this was left scoped-but-unstarted.

### 2.18 Texture compression as an export option `BLOCKED` *(old Task 39)*

Desktop is settled: BC formats work in both browsers, BC1 equals uncompressed at RMSE 0.0006. Safari
and mobile are exactly the platforms the option exists to serve, and both are unmeasured — blocked on
§2.8 / §2.9.

### 2.19 Interleaved-build stale object — the real fix `OPEN` *(old Task 40)*

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

### 2.22 Small perf items `OPEN`

- **`INITIAL_MEMORY` is 32 MB with `ALLOW_MEMORY_GROWTH=1`.** For a project with a known peak heap,
  raising it avoids the growth steps during load outright.
- **The 45 MB side module's levers are module-level** (`module_*_enabled=no`) and transport
  compression — `-Os` + thin LTO is already production, and `EXPORT_ALL=1` applies to the 1.3 MB
  *main* module, so neither is the cause. Compression is now measured: 45.3 MB → 11.9 MB gzip → 9.5 MB
  brotli, and end to end it is worth 3.5× on the whole load (§4.15). **Nothing in this repo says to
  serve compressed** — not the README, not `webgpu_site`, and none of the local servers did until
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

---

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
