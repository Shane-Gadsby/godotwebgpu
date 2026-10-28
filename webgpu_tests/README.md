# WebGPU Test Suite

Automated tests for the Godot WebGPU rendering backend. Validates the full shader pipeline from GLSL compilation through SPIR-V → WGSL conversion to in-browser execution.

## Test Categories

| Test | What it validates | Runtime | Needs engine build? |
|------|------------------|---------|---------------------|
| [Shader Corpus](shader_corpus/) | SPIR-V → WGSL conversion via wgsl_convert_cli | ~1s | No (needs wgsl_convert_cli) |
| [SPIR-V Preprocessing](preprocessing_tests/) | Each SPIR-V pass in isolation, including the IR passes (`spirv-val` on their output) — 291 assertions | ~10s | No (needs wgsl_convert_cli) |
| [Driver Unit Tests](driver_unit_tests/) | Bind-group layout, format and pipeline-state logic, the expanded-array binding layout — 379 assertions | ~2s | No (standalone) |
| [SPIR-V Validation](shader_corpus/validate_spirv_dump.mjs) | ALL engine-compiled SPIR-V through wgsl_convert_cli | ~5s | Yes (editor) |
| [Converter Smoke](converter_smoke/) (`run.sh`) | Every engine variant `wgsl_precompile.py` builds converts, keeps the bindings, kinds and override ids of the stored reference and the driver's text-scan facts, and compiles in Chrome — the only check short of rendering that bind-group layouts and pipeline constants still line up | ~2min | No (builds wgsl_convert_cli; Chrome and a real adapter) |
| [Spec-Constant Overrides](spec_constant_overrides/) | Specialization constants survive as `@id(N) override`, and WebGPU pipeline constants set them | ~5s | No (needs wgsl_convert_cli) |
| [Smoke Test](test_project/smoke_test.mjs) | Full runtime in headless Chrome — no shader errors, no device lost | ~60s | Yes (editor + web template) |
| [Scene Smoketest](scene_smoketest/) | 23 scenes (demos, benchmarks, a self-checking audio suite and a braced GDScript project) across Chrome, Firefox, and Safari | ~8min | Yes (pre-exported) |
| [Perf Guard](perf_guard/) | WebGPU calls per frame stay within budget (command encoders, submits, `writeBuffer` calls and bytes) on the post-fx/shadows/instances/PBR scenes; adapter-independent | ~90s | Yes (pre-exported scenes) |
| [Web Audio Parity](web_audio/) | Sample playback (the web default) does what `AudioServer` does — loop modes sample for sample, volume ramps/fades, buses, positions, 3D filter — 40 checks; plus native vs web end to end | ~10s (+~2min e2e) | No (e2e: editor + web templates) |
| [Resource Lifecycle](resource_lifecycle/) | Rapid create/destroy of buffers, textures, pipelines | ~30s | No (standalone) |
| [Screenshot Comparison](screenshot_comparison/) | Visual regression across Chrome and Firefox | ~60s | No (standalone) |
| [Font Rendering](scene_smoketest/test_font_visual.mjs) | Text renders in the colors it was asked to — guards the glyph-modulate regression | ~20s | Yes (pre-exported) |
| [Font Assertion Self-Test](scene_smoketest/self_test_font_visual.mjs) | That the font test's own thresholds can still fail | <1s | No (standalone) |
| [Fog Smoothness](scene_smoketest/test_fog_visual.mjs) | Volumetric fog's froxel volume is still sampled smoothly, not banded | ~25s | Yes (pre-exported, needs a real GPU) |
| [Fog Assertion Self-Test](scene_smoketest/self_test_fog_visual.mjs) | That the fog test's own thresholds can still fail | <1s | No (standalone) |
| [Forward+ Feature Matrix](forward_plus/) | Every Forward+ feature still changes the frame — 97 arms | ~12min | Yes (own export, **needs a real GPU**) |
| [Forward+ Parity](forward_plus/) (`--parity`) | Every matrix frame matches a native Vulkan render of the same configuration | ~15min | Yes (own export, a real GPU, and a native Vulkan device + display for the reference) |
| [Forward+ Matrix Self-Test](forward_plus/self_test_forward_plus.mjs) | That the matrix's own verdict logic and feature table are sound | <1s | No (standalone) |
| [DOF NaN Containment](dof_nan/) (`run.sh`) | One NaN pixel stays one pixel through every bokeh DOF mode, glow, SSR and auto exposure, natively on Forward+ and Mobile (§13) | a few minutes | Yes (native editor; a display or `xvfb-run`) |
| [Shader-Bake Isolation](bake_isolation/) (`run.sh`) | A Web export's shader bake leaves the editor's own frames alone and bakes FSR 2 for WebGPU (§14) | ~1min | Yes (native editor + web template; Xvfb + Vulkan) |
| [MetaHuman Importer](metahuman/) (`run.sh`) | A MetaHuman export imports, rigs and renders; groom strands write no NaN (`nan_check.gd`); `--shots` renders frames ([README](metahuman/README.md)) | minutes | Yes (native editor; a MetaHuman export of your own) |
| [Software Ray Tracing Probe](raytracing_probe/) | A WGSL BVH traversal agrees with a brute-force CPU trace, and how many rays per second it reaches on the adapter (primary, shadow, incoherent bounce) — the ceiling for any ray or path tracing on WebGPU, which has no hardware RT; feeds TASKS.md §2.34 | ~60s | No (standalone; `--software` for the correctness checks, a real GPU for numbers that mean anything) |
| [Android Profiling](perf_guard/android/) | What a WebGPU export does on a real phone over USB — fps/frame-time per export variant, GPU timestamp queries per pass, WGSL dump/substitute for shader bisection | ~75s/variant | No (profiles any existing export; needs an Android phone with USB debugging) |
| [Startup Phases](startup_phases/) | Where a real export's load time actually goes, phase by phase | ~60s/run | No (profiles any existing export) |
| [WebRTC Parity](webrtc_parity/) | WebRTC behaves the same in a web export as it does on desktop — 9 fixtures x 3 export variants x Chrome/Firefox | ~12min (one browser) | Yes (editor + web template) |
| [WebRTC Congestion](webrtc_congestion/) | What WebRTC does when the link is the bottleneck — two processes either side of a `netem`-shaped link. Found F9 and F10 | ~15min (full sweep) | Yes (editor + web template) |
| [Occlusion Culling Benchmark](occlusion_bench/run.mjs) | What occlusion culling costs and saves on the web: a 24x24-block city, street view (mostly occluded) and top view (nothing to cull), culling off and on — main-thread ms per frame, GPU ms, draws, objects, heap, load time. Needs a template with the raycast module, in every web template by default since 2026-10-26 (TASKS.md §4.38) | ~2min per export | Yes (`occlusion_bench/export.sh`; a real GPU) |
| [Load-Ramp Benchmark](benchmark/) (`run_ramp.sh`) | How much content each leg (native Vulkan and Compatibility, stock WebGL, WebGPU Mobile and Forward+, Firefox WebGPU) carries before falling to 20 fps, per ramp (draw calls, moving nodes, fill, shadowed PBR, particles). The source of the README's performance table | ~5min per leg and run (~1 h for all six legs x 2) | Yes (editor + WebGPU template; official 4.7.2 template for WebGL; **an idle real GPU**) |

## How It Works

The WebGPU shader pipeline is:

```
GLSL → SPIR-V (glslang, at editor build time)
     → spirv_preprocess::run_pipeline() (C++, at build time, export time or runtime)
     → WGSL (SPIRV-Cross's WGSL backend, C++, linked into engine, at runtime)
     → GPU (browser's WebGPU implementation)
```

`run_pipeline()` is the one ordering of the SPIR-V passes every converter uses; the full list, with what
each pass is for, is in [`webgpu_site/TECHNICAL_REFERENCE.md`](../webgpu_site/TECHNICAL_REFERENCE.md) §4.1.
`preprocessing_tests/` covers the passes one at a time (291 assertions; tests 24-27 cover the three passes on
SPIRV-Tools' IR -- `lower_wgsl_unsupported_ops`, `apply_sampler_lod_bias`, `expand_binding_arrays` -- and run
`spirv-val` on each output, from `fixtures_src/wgsl_unsupported_ops.frag` and `binding_arrays*.{frag,comp}`).
`driver_unit_tests/test_binding_layout.mjs` checks the expanded-array binding layout against
`drivers/webgpu/spirv_binding_layout.h` itself, and that every engine texture array fits it.

## Running Locally

### Prerequisites

- **Node.js 20+** — All test runners
- **glslangValidator** — Shader corpus fixture compilation (`brew install glslang` / `apt install glslang-tools`)
- **Playwright** — Browser-based tests (`npm install playwright`)
- **Emscripten 4.0.11** — Web template build (`~/emsdk`)
- **SCons + Python** — Godot build system

### 1. Shader Corpus (fast, no build needed)

Tests 9 hand-crafted GLSL fixtures through wgsl_convert_cli (if available):

```bash
cd webgpu_tests/shader_corpus
./compile_fixtures.sh    # GLSL → SPIR-V (requires glslangValidator)
node run_tests.mjs       # SPIR-V → WGSL validation (skips if no wgsl_convert_cli)
```

### 1b. Converter smoke check (no engine build; a real adapter for the Chrome step)

```bash
webgpu_tests/converter_smoke/run.sh <scratch_dir>     # KEEP=0 deletes a passing run's directory
```

Builds `bin/wgsl_convert_cli` from the tree, dumps and converts every engine variant `wgsl_precompile.py` builds
(`spirv_cross_eval/dump_corpus.py`, 0 failures), compares bindings, kinds and override ids with
`spirv_cross_eval/interface_reference.json` (`compare_interface.py`), runs `wgsl_text_scan/run_tests.sh` on the run
against `scan_reference.txt`, and compiles every module in Chrome (`spirv_cross_eval/validate_wgsl.mjs`). A module that
is invalid in Chrome fails unless `converter_smoke/expected_invalid.txt` lists it, and a listed module that has become
valid fails too. Last line `SMOKE OK` / `SMOKE FAILED`; `local_ci.sh` runs it in stage 1, `--quick` included.

The references are Tint's interface from when SPIRV-Cross replaced it (TASKS.md §4.36). After a deliberate interface
change, regenerate them from a run (`compare_interface.py --write <manifest> <run>/wgsl`,
`WRITE_REFERENCE=1 wgsl_text_scan/run_tests.sh <run>`) and say why in the commit.

The Chrome step launches system Chrome headed with the Vulkan flags and refuses any adapter but nvidia/lovelace with 48
sampled textures per stage (TASKS.md §3 trap 79), exit 3 from the validator. On another machine set
`WGSL_VALIDATE_ADAPTER=<vendor>/<architecture>` (the `adapter` line it printed) or `WGSL_VALIDATE_ADAPTER=any`; the
fallback (swiftshader) adapter is never accepted. `webgpu_notes/work/smoke.sh`, where this lived while the converter swap's
work board ran, forwards here.

### Font Rendering (needs the `font_rendering` scene exported)

Renders text in deliberately non-white colors and asserts the pixels that reach
the canvas. It exists because a previous regression made **all** text render
white, and was missed by a check done against white text — where broken and
working look identical.

```bash
cd webgpu_tests/scene_smoketest
node run_scenes.mjs --export-only --scene font_rendering   # once, after an engine build
node test_font_visual.mjs --browser chrome                 # or --browser all
```

The thresholds are themselves tested, against two committed reference images (a
correct render, and one with the regression's effect simulated). That runs
standalone, with no browser, export or GPU:

```bash
cd webgpu_tests/scene_smoketest && node self_test_font_visual.mjs
```

If you ever need to regenerate the references, render
`webgpu_tests/font_rendering/godot/font_check` and save the viewport — but do not
relax the colors: the test's whole value is that the text is not white.

### Fog Smoothness (needs the `volumetric_fog` scene exported, and a real GPU)

Guards the froxel-sampling regression from TASKS.md Task 12.1 — fog banding if
the volume is ever sampled without trilinear filtering, or its resolution drops.
It asserts on shape rather than an exact image: the share of hard edges
(`p99 |laplacian|`) and of flat plateaus in the lit fog, both measured at a
canonical 960x540 so canvas size does not move them.

```bash
cd webgpu_tests/scene_smoketest
node run_scenes.mjs --export --scene volumetric_fog    # once, after an engine build
WEBGPU_REAL_GPU=1 node test_fog_visual.mjs --browser chrome
node test_fog_visual.mjs --browser firefox             # needs a display, not headless
```

**It SKIPs rather than passes where it cannot run** (exit code 2), and there are
two such cases, both measured rather than assumed: the default Chrome launch
forces the swiftshader adapter, under which this scene renders no volumetric fog
at all; and headless Firefox cannot composite here, returning a black canvas.
Volumetric fog is also Forward+-only — `render_forward_mobile.cpp` has no fog
code whatsoever — so the test skips if the adapter fell back to Forward Mobile.

As with the font test the thresholds are themselves tested, against committed
images: a correct render, the same frame resampled through a 64-wide grid with
no filtering (the engine's default fog volume width, sampled the way the
regression would), a blatant 16-wide case, and a black frame that must be
rejected by the liveness guard rather than pass for having no edges:

```bash
cd webgpu_tests/scene_smoketest && node self_test_fog_visual.mjs
```

Do not lower the scene's fog density or light energies when regenerating — a dim
fog makes the artifact unmeasurable, which is the failure this test exists to
avoid.

### Forward+ Feature Matrix (needs its own export, and a real GPU)

The per-feature regression suite: for each of 97 Forward+ arms, render the
fixture with it off and on and assert the frame actually changed, with no driver
errors. It exists because the failure this port keeps producing is a feature
**silently doing nothing** — which logs nothing and looks fine unless compared
against the same frame without it. SSAO and SDFGI both shipped broken through a
fully green suite in the 4.8 port for exactly that reason.

```bash
cd webgpu_tests/forward_plus
./export.sh                                     # after an engine build
WEBGPU_REAL_GPU=1 node run_forward_plus.mjs     # the matrix
WEBGPU_REAL_GPU=1 node run_forward_plus.mjs --parity   # ... every frame against native Vulkan
node run_forward_plus.mjs --list                # covered and uncovered features
WEBGPU_REAL_GPU=1 node run_forward_plus.mjs --parity --feature sdfgi,lightmap_gi   # a subset
WEBGPU_REAL_GPU=1 node run_forward_plus.mjs --parity --save-dir /tmp/fp   # keep every captured web frame
./bake_lightmap.sh                              # re-bake lightmap_room for the lightmap_gi arm (Xvfb, no GUI)
```

Arms added 2026-10-26 (TASKS.md §4.39): skeletal animation, particle trails, LightmapGI,
FogVolume with and without a fog shader, stencil outline and x-ray, bent normals, SMAA,
compositor effects and occlusion culling. `occlusion_culling` is judged on render info
(objects drawn, 29 -> 8) rather than pixels; it needs the raycast module in the template
(`needsRaycast`), which every web template has by default, and against one without it
reports KNOWN. An arm marked `knownFail` reports KNOWN instead of failing, and XPASS
(which fails) once it starts passing. Before rendering the native reference, `--parity`
deletes `godot/fp_check/.godot/shader_cache`: a cache left by another engine build moved
native SDFGI by 7 points once (TASKS.md §3 trap 68).

The delta test only proves a feature *does something*; `--parity` proves it does
what native Vulkan does, within a per-feature tolerance. It was what found
`DEPTH_TEXTURE` reading zeros, volumetric fog without shadows and SDFGI corrupted
by camera movement — all of which passed the delta test (`webgpu_notes/TASKS.md`
§4.29).

**Any feature added to the renderer — including code merged from upstream Godot
— must get an entry in this matrix, or an entry in `features.mjs`'s `UNCOVERED`
list saying why not.** This is enforced: the fixture publishes its feature list
at boot and the harness fails the run, naming the offender, if that list and
`features.mjs` disagree in either direction.

**It requires a real GPU** and skips (exit 2) rather than passing without one —
a software adapter falls back to Forward Mobile, where a third of the matrix
does not exist. Thresholds are calibrated against native Vulkan, never against
WebGPU. See [`forward_plus/README.md`](forward_plus/README.md) for the full
rules, the recalibration procedure, and the triage table that separates a port
bug from a fixture weakness.

### 2. SPIR-V Dump Validation (requires editor build)

Validates all 300+ engine-compiled shaders through wgsl_convert_cli offline:

```bash
# Build the editor (macOS example)
scons platform=macos target=editor dev_build=yes -j$(sysctl -n hw.ncpu)

# Run the test scene to trigger shader compilation (needs a window)
GODOT_DUMP_SPIRV=/tmp/spirv_dump bin/godot.macos.editor.dev.arm64 \
    --path webgpu_tests/test_project --quit-after 10

# Validate all dumped SPIR-V
node webgpu_tests/shader_corpus/validate_spirv_dump.mjs /tmp/spirv_dump/
```

The validator uses an **expected failures baseline** (`shader_corpus/expected_failures.json`) — shader variants compiled by the Vulkan editor that the WebGPU runtime never uses (different code paths). CI fails only on **regressions** (new failures beyond the baseline).

To update the baseline after intentional changes:
```bash
node webgpu_tests/shader_corpus/validate_spirv_dump.mjs /tmp/spirv_dump/ --update-baseline
```

### 3. Smoke Test (requires full build + export)

End-to-end validation: exports the test project, serves it in headless Chrome, verifies no shader errors:

```bash
# Build the web template
source ~/emsdk/emsdk_env.sh
scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no -j$(sysctl -n hw.ncpu)

# Install template (macOS — adjust path for Linux)
mkdir -p ~/Library/Application\ Support/Godot/export_templates/4.6.2.stable
cp bin/godot.web.template_release.wasm32.nothreads.dlink.zip \
   ~/Library/Application\ Support/Godot/export_templates/4.6.2.stable/web_nothreads_release.zip

# Export
bin/godot.macos.editor.arm64 --headless --path webgpu_tests/test_project \
    --export-release "WebGPU" export/index.html

# Run smoke test
cd webgpu_tests/test_project
npm install playwright && npx playwright install chromium
node smoke_test.mjs ./export/
```

**Pass criteria:** Engine starts, all shaders compile (no `[SHADER]` errors), no device-lost, GDScript reports `[ShaderCoverage] PASS`.

### 4. Scene Smoketest — Multi-Browser (requires pre-exported scenes)

Runs 19 demo and benchmark scenes across Chrome, Firefox, and Safari:

```bash
cd webgpu_tests/scene_smoketest
npm install playwright && npx playwright install chromium firefox --with-deps
node run_scenes.mjs --browser all         # All 3 browsers
node run_scenes.mjs --browser chrome      # Chrome only (default)
node run_scenes.mjs --browser firefox     # Firefox only
node run_scenes.mjs --browser safari      # Safari only (macOS, requires "Allow JS from Apple Events")
node run_scenes.mjs --scene benchmark_pbr # Single scene, default browser
```

**Pass criteria:** Engine starts (canvas > 300px), no GPU validation errors, no shader failures, no device-lost.

**Safari prerequisite:** Enable Safari → Develop → "Allow JavaScript from Apple Events" (uses real Safari via AppleScript since Playwright's safaridriver disables WebGPU).

**Exporting scenes** (requires editor + web template):
```bash
node run_scenes.mjs --export --browser chrome
```

**Diagnosing a failing scene** — `run_scenes.mjs` truncates each console message to 200 characters
and prints ~100, which cuts off the `While validating … / While encoding … / While calling …` chain
where Dawn puts the actual information. `capture_errors.mjs` runs one already-exported scene and
prints every distinct message in full, deduplicated with a repeat count and ordered cause-first
(a root error is usually seen once; its cascade thousands of times):

```bash
node capture_errors.mjs exports/demo_3d_particles
node capture_errors.mjs exports/demo_3d_particles --browser firefox --wait 25000
```

It mirrors `run_scenes.mjs`'s Chrome modes (`WEBGPU_REAL_GPU=1`, `CI=1`, or neither = the system's
own Chrome) on purpose: the adapter decides the device limits and therefore which errors appear at
all, so diagnosing on a different adapter than the tier ran on can show an entirely different
failure.

### 5. Resource Lifecycle (standalone, needs Playwright)

```bash
cd webgpu_tests/resource_lifecycle
npm install playwright && npx playwright install chromium --with-deps
node run_tests.mjs
```

### 5b. Web Audio Parity (standalone; e2e needs the editor + web templates)

```bash
cd webgpu_tests/web_audio
npm install
node run_tests.mjs                 # the sample driver in an OfflineAudioContext vs native algorithms
./export.sh && node run_e2e.mjs    # the same GDScript natively and in the nothreads/threads exports
```

See [`web_audio/README.md`](web_audio/README.md) and `webgpu_notes/TASKS.md` §4.30.

### 6. WebRTC Parity (needs the editor + a web template)

```bash
cd webgpu_tests/webrtc_parity
npm install && npx playwright install chromium firefox --with-deps
./export.sh                                      # nothreads, threads and dlink
node run_tests.mjs                               # web half — the CI entry point
RTC_BROWSERS=chromium,firefox node run_tests.mjs
```

`run_tests.mjs` asserts what each fixture is supposed to *prove*. The real
parity check is `./compare.sh <fixture>`, which diffs the same GDScript run
natively against the browser — it needs the `webrtc-native` reference
extension, a separate clone a fresh checkout does not have, which is why CI
runs the web half. See [`webrtc_parity/README.md`](webrtc_parity/README.md),
and `webgpu_notes/TASKS.md` §4.20 for the eight findings it produced.

### 7. WebRTC Congestion (needs the editor, a web template, and unprivileged user namespaces)

```bash
cd webgpu_tests/webrtc_congestion
./shape.sh --selftest --rate 2mbit --delay 25ms   # prove the shaping works first
./export.sh                                        # needed for the browser-peer rows
./sweep.sh                                         # the whole matrix, tabulated
```

Shapes the link with `netem` inside an `unshare -rnm` namespace, so it needs
**no root** and touches nothing outside the test — do not use the
`sudo tc qdisc ... dev lo` approach, which rate-limits all loopback traffic on
the machine until removed. **Local only, not in CI**: it needs unprivileged
user namespaces and ~15 minutes, and its verdicts are measurements rather than
pass/fail gates. See [`webrtc_congestion/README.md`](webrtc_congestion/README.md)
and `webgpu_notes/TASKS.md` §4.20

### 8. Screenshot Comparison (standalone, needs Playwright)

```bash
cd webgpu_tests/screenshot_comparison
npm install playwright && npx playwright install chromium firefox --with-deps
node screenshot_tests.mjs --update-baselines  # first run creates baselines
node screenshot_tests.mjs                      # subsequent runs compare
```

Debugging a specific reported bug against a real project rather than running
this suite's own fixtures? See
[`screenshot_comparison/LIVE_REPRO_METHODOLOGY.md`](screenshot_comparison/LIVE_REPRO_METHODOLOGY.md)
for the scratch-copy + native-Vulkan-vs-WebGPU comparison workflow and its
reusable capture scripts.

### 9. Android On-Device Profiling (needs a phone over USB)

```bash
adb reverse tcp:8099 tcp:8099          # the phone loads http://localhost:8099/ = a secure context, no HTTPS needed
cd webgpu_tests/perf_guard/android
node probe.mjs      --dir <export>                              # adapter, limits, renderer banner, fps, console
node fps_matrix.mjs --root <dir of exports> --variants a,b,c    # cold-boot fps / p50 p90 p99 / main busy % per export
node gpu_time.mjs   --dir <export> --passes --top 40            # GPU ms per pass from timestamp queries, ranked
node gpu_time.mjs   --dir <export> --dump-wgsl 'mod:SceneForwardClusteredShaderRD:18' --dump-dir D --dump-only
node gpu_time.mjs   --dir <export> --passes --replace-pair D/<orig>.wgsl=<edited>.wgsl   # bisect a shader
```

Each script serves the export with COOP/COEP on 127.0.0.1:8099, force-stops Chrome on the phone, opens a blank
tab, closes every other tab, installs its instrumentation with `Page.addScriptToEvaluateOnNewDocument` and then
navigates — raw CDP through `adb forward tcp:9222 localabstract:chrome_devtools_remote`, no Playwright. Export
with `variant/separate_render_thread=false`: with the separate render thread the engine's WebGPU calls happen in a
pthread worker that never answers `Runtime.evaluate`, so the page-side tools see nothing. First results and the
traps are in `webgpu_notes/TASKS.md` §2.9 and §3; the directory's README has the full recipe.

### 10. Software Ray Tracing Probe (standalone; a real GPU for meaningful numbers)

```bash
cd webgpu_tests/raytracing_probe
node run_probe.mjs                     # system Chrome, headed, 1080p, 100k and 1M triangles; results/latest.json
node run_probe.mjs --software          # swiftshader at 320x180: the correctness checks only, exit 0/1
node run_probe.mjs --url-only --port 8099   # serve only: a browser you already have up
node run_probe_phone.mjs --shot phone.png   # the Razr 60 over USB: adb reverse + CDP, result read back, its tab closed
```

A CPU-built binned-SAH BVH over a procedural scene, traversed by a compute shader with a per-thread stack:
primary camera rays, any-hit shadow rays to a point light and cosine-weighted bounce rays from each hit,
each timed with GPU timestamp queries. A grid of primary rays is re-traced on the CPU by brute force and must
agree with the GPU; hit, occlusion and bounce fractions must be the fixed camera's; the BVH depth must fit the
traversal stack — exit 1 on any failure, 2 if a real-GPU run landed on the fallback adapter. Prints Mrays/s per
kernel, the adapter's limits and features, BVH build time and traversal steps, and draws the result so a wrong
traversal is visible. The directory's README explains how to read the numbers and the phone recipe; the
investigation they feed is `webgpu_notes/TASKS.md` §2.34, and §1 "Experiments in flight" says what to run next.

### 11. Load-Ramp Benchmark (needs the editor, the WebGPU template, and an idle real GPU)

```bash
cd webgpu_tests/benchmark
./run_ramp.sh                                   # every leg, every ramp, 2 runs -> results_ramp/<date>/ (~1 h)
./run_ramp.sh --legs webgpu_fp,webgl --ramps draws --repeat 1   # narrow it while iterating
python3 readme_perf_table.py results_ramp/<date>            # print the README performance table
python3 readme_perf_table.py results_ramp/<date> --write    # replace it between the perf-table markers in README.md
```

Each ramp grows one kind of content until the frame rate falls to the target (`--target-fps`, default 20), and
reports how much content that took: N. Comparing N across legs gives "WebGPU Forward+ carried 1.5x the draw-call
load of WebGL at 20 fps". The fps matrix (`run_matrix.sh`) cannot give that, because Chrome holds WebGL at the
display rate even with the frame-rate limit lifted (TASKS.md §2.17). One unit of N:

| ramp | one unit of N | what it loads |
|---|---|---|
| `draws` | a MeshInstance3D (12-triangle box) with its own StandardMaterial3D, static | per-draw CPU submission |
| `nodes` | the `draws` unit plus one GDScript `rotate_y` per frame | script + transform sync + submission |
| `fill` | a full-screen 1280x720 alpha-blended quad, 64-iteration trig loop per pixel | fragment ALU and blending |
| `lit` | a PBR sphere (~9.2k triangles) under 1 directional + 3 omni + 3 spot lights, all shadowed | vertex, shadow passes, shading |
| `particles` | a GPUParticles3D emitter of 2048 billboard particles, simulated every frame | particle simulation + vertices |

Legs: `native_vulkan_fp`, `native_compat` (OpenGL; separates "WebGL" from "the Compatibility renderer"), `webgl`
(the official 4.7.2 template exported by the official 4.7.x editor: a 4.8 pck will not load in it), `webgpu_mobile`,
`webgpu_fp`, and `firefox_webgpu_fp` (Playwright's Firefox, if installed). Every leg runs headed at 1280x720 with
the frame-rate cap lifted (`--capped` keeps it). Content is placed with a fixed seed under a fixed camera, from the
same scene (`benchmark/godot/load_ramp/`, which also parses in Godot 4.7).

Control loop (`ramp.gd`): N doubles while the frame time is far below the target, then grows by what the last
window predicts (x1.25 margin, at least x1.15) so no step lands far past it, and bisects (geometric midpoints) to
within 4% once it has a point either side. Every change of N is followed by a 1 s settle; the frame time is the
mean (elapsed / frames) over a 2 s window, and a below-target window must be confirmed by a second one after a
further 2 s, so a single hitch cannot end a ramp. The crossing is interpolated in log space. A ramp that hits its
N cap or `--max-s` (180 s) reports a lower bound (`>=`).

Fairness rules that cost a run to learn (2026-10-26):

- **Every ramp runs in its own process / browser.** Overshooting fill by 4x left Chrome's GPU process with a
  backlog that slowed every later window to ~150 ms whatever N was, and the next ramp inherited it (it read as a
  "cliff at 32 spheres" in `lit`); one run lost the device and kept "measuring" a dead one at 0.2 ms a frame. Now
  `ramp_browser.mjs` and the native loop start each ramp fresh, and a watchdog (no `[RAMP]` line for 90 s, a page
  error or crash, a lost device, an out-of-memory error, a failed screenshot, the ramp's time budget) closes it,
  logs `[BENCHMARK] ramp_failed ramp=<r> reason=<why>` and moves on: the summary reports that run as the last N
  measured above the target, marked `(failed)`.
- **WebGPU frames are paced to 2 in flight** (`GPUQueue.onSubmittedWorkDone`, injected by `ramp_browser.mjs`;
  `--no-pace` to compare), as native Vulkan is. With the frame-rate limit lifted Chrome otherwise lets the page
  queue GPU work faster than the GPU finishes it: the frame time reads too fast while the queue grows (unpaced, fill
  "carried" twice native Vulkan's layers; paced, 0.67x). WebGL needs no pacing: the browser throttles it.
- **Memory budget.** The web heap is 2 GB (Emscripten's default for a nothreads build; the official template has
  the same), and running out of it kills the page. A ramp never grows past what its measured memory predicts will
  fit in `--mem-mb` (1400 MB): on the web that is the wasm heap's size, read from the page (`OS.get_static_memory_usage()`
  is 0 in release templates); natively Godot's tracked static memory. A ramp stopped there reports `status=mem_cap`,
  shown as `(mem)`: memory-limited, not a frame-rate crossing. The draws ramp, the most memory-hungry, uses ~780 MB of
  heap at 44k units on WebGPU.

The script checks its prerequisites (binaries, the official template, Playwright, a display) and that the GPU
is idle (no other process over 1 GB, utilization at most 25%) before it starts and before every leg; it stops
rather than record a contaminated leg (`--force` overrides, and says so in the summary). It exports fresh from
this tree every run, takes and drops the `gpu` lease when `webgpu_notes/work/lease.sh` exists, and writes
`results_ramp/<date>/`: `env.txt` (machine, GPU and driver, browser and engine versions), `logs/<leg>__r<k>.log`
(one `[BENCHMARK] ramp=...` result line per ramp, `[RAMP]` lines per step), `shots/` (one frame per ramp and
leg, to check every leg draws the same thing) and `ramp_summary.md` (N per leg, ratios, each run's N and the
spread).

### 12. Occlusion Culling Benchmark (needs the editor, a web template with the raycast module, and a real GPU)

```bash
cd webgpu_tests/occlusion_bench
./export.sh <template.zip> <out dir> [threads=no|yes]   # BAKE=1 bakes shaders (under xvfb-run)
node run.mjs --dir <out dir> [--uncapped] [--json out.json]
```

A 24x24-block city with 20k props, each with its own material, viewed from the street (mostly occluded) and from
above (nothing to cull), with occlusion culling off and on. Reports main-thread ms per frame (in a nothreads export,
the whole engine frame), GPU ms, draws and objects, heap and load time; `--uncapped` paces frames to 2 in flight
(TASKS.md §3 trap 66). Results and what they mean: TASKS.md §4.38.

### 13. DOF NaN containment (native, needs a display)

```bash
webgpu_tests/dof_nan/run.sh [editor binary] [out dir]
```

One NaN pixel through every bokeh DOF mode (box, hexagon, circle; near and far; transition and physical), glow
(additive, softlight), SSR (Forward+ only; a reported SKIP on Mobile) and auto exposure, on Forward+ and Forward
Mobile; `ONLY=dof,glow,ssr,exposure` narrows it. Exit 0 when every case keeps the NaN to the quad's own pixels (glow
and SSR are also gated on changed pixels, auto exposure on mean brightness). The web side is the Forward+ matrix's
`dof_nan`, `glow_nan`, `ssr_nan` and `auto_exposure_nan` arms (`maxDelta` arms). `webgpu_tests/metahuman/run.sh` also runs `nan_check.gd`
(the groom strands with coverage forced below zero must write no NaN; lavapipe under Xvfb) and reimports whenever the
editor is newer than its last import. TASKS.md §4.41, §4.44, §3 traps 70–71.

### 14. Shader-bake isolation (native, Xvfb + Vulkan, no browser)

```bash
webgpu_tests/bake_isolation/run.sh      # EDITOR_BIN / TEMPLATE override the binaries; KEEP=1 keeps the scratch dir
```

An interactive Web export from a windowed editor whose 3D viewport runs FSR 2 + TAA. Fails on any "Mismatch uniform
type" / "Uniforms were never supplied" in the editor's own log, on an FSR 2 pass baked in the editor's flavor, or as
inconclusive when the editor drew no frame during the bake. ~1 min. TASKS.md §4.43, §3 trap 73.

## CI Pipeline

Defined in `.github/workflows/webgpu_tests.yml`. Runs on push/PR to `webgpu-4.7.2` when `drivers/webgpu/`, `servers/rendering/`, or `webgpu_tests/` are modified.

```
┌─────────────────────────────────────────────────────────────────┐
│  CI Pipeline (.github/workflows/webgpu_tests.yml)               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────┐  (parallel, no build needed)               │
│  │ shader-corpus   │  9 GLSL fixtures → SPIR-V → WGSL          │
│  └─────────────────┘                                            │
│                                                                 │
│  ┌─────────────────┐  (parallel, no build needed)               │
│  │ resource-       │  GPU resource stress test in Chrome        │
│  │ lifecycle       │                                            │
│  └─────────────────┘                                            │
│                                                                 │
│  ┌─────────────────┐  (parallel, no build needed)               │
│  │ screenshot-     │  Visual regression Chrome + Firefox        │
│  │ comparison      │                                            │
│  └─────────────────┘                                            │
│                                                                 │
│  ┌─────────────────┐                                            │
│  │ build-webgpu    │  Web template + Linux editor + export      │
│  │ (~60 min)       │  + SPIR-V dump (309 shaders), and exports  │
│  │                 │  the scene-smoketest and WebRTC fixtures   │
│  └────────┬────────┘                                            │
│           │                                                     │
│           ├──────────────┬───────────────┬──────────────┐       │
│           ▼              ▼               ▼              ▼       │
│  ┌────────────────┐ ┌────────────┐ ┌───────────┐ ┌────────────┐ │
│  │ validate-spirv │ │ smoke-test │ │ scene-    │ │ webrtc-    │ │
│  │ All SPIR-V     │ │ Headless   │ │ smoketest │ │ parity     │ │
│  │ via wgsl_cli   │ │ Chrome run │ │ 23 scenes │ │ 9 fixtures │ │
│  │                │ │            │ │ × Chrome  │ │ in Chrome  │ │
│  │                │ │            │ │ + Firefox │ │            │ │
│  └────────────────┘ └────────────┘ └───────────┘ └────────────┘ │
│                                                                 │
│  ┌─────────────────────────────────────────────────────┐        │
│  │ test-summary   │  Aggregates results, gates merge   │        │
│  └─────────────────────────────────────────────────────┘        │
└─────────────────────────────────────────────────────────────────┘
```

### CI Jobs Detail

| Job | Depends on | Timeout | Blocks merge? |
|-----|-----------|---------|---------------|
| `shader-corpus` | — | 25 min | Yes |
| `build-webgpu` | — | 90 min | Yes |
| `validate-spirv` | build-webgpu | 10 min | Yes |
| `smoke-test` | build-webgpu | 15 min | Yes |
| `scene-smoketest` | build-webgpu | 20 min | Yes |
| `resource-lifecycle` | — | 15 min | Yes |
| `webrtc-parity` | build-webgpu | 25 min | Yes |
| `screenshot-comparison` | — | 20 min | No (warning only) |
| `test-summary` | all above | — | — |

**`scene-smoketest` used to be a fake gate, and no longer is.** It once ran with the default `--skip-export` against exports that were never produced, so all 21 scenes came back `SKIP` and the job passed having tested nothing. Three things fixed it, and all three are load-bearing — removing any one of them puts the fake gate back: `build-webgpu` exports the scenes itself (`run_scenes.mjs --export-only`) and uploads them as the `scene-exports` artifact; `scene-smoketest` `needs: build-webgpu`, downloads that artifact and restores the deduped side modules; and it runs with **`--require-exports`**, which turns "no export on disk" from a skip into a failure. The old `export_presets.cfg` trap is gone too — `run_scenes.mjs` now rewrites both the template path and `variant/extensions_support` per export (deriving the latter from whether the template filename says `.dlink.`), instead of trusting a checked-in absolute path that pointed at one contributor's machine and silently resolved to the *non*-WebGPU template.

**The general lesson, worth applying to any tier added here**: a test tier that skips is indistinguishable from one that passes unless it is built not to be. Every browser-driven tier in this suite now either fails or reports a distinct exit status when its inputs are missing — `--require-exports` for the scene smoketest, exit 2 plus a `SKIP --` line for `webrtc-parity` and the tiers `local_ci.sh` drives.

### Trigger Paths

CI runs when any of these paths change:
- `drivers/webgpu/**`
- `webgpu_tests/**`
- `servers/rendering/**`

Can also be triggered manually via `workflow_dispatch`.

## SPIR-V Dump (Engine Integration)

The engine includes a `GODOT_DUMP_SPIRV` environment variable (added in `servers/rendering/rendering_device.cpp`) that causes all compiled SPIR-V to be written to disk during shader compilation:

```bash
GODOT_DUMP_SPIRV=/tmp/spirv_dump godot --path webgpu_tests/test_project --quit-after 10
node webgpu_tests/shader_corpus/validate_spirv_dump.mjs /tmp/spirv_dump/
```

**Important:** `--headless` does NOT trigger shader compilation (shaders compile lazily during rendering). You need to run with a window (`--quit-after N`) to get the full shader dump.

The dump produces ~309 `.spv` files named `<ShaderRD>:<variant>.<stage>.spv` (e.g., `SceneForwardClusteredShaderRD:5.frag.spv`).

## Test Project (Shader Coverage Scene)

`test_project/` is a Godot 4.6 project with a GDScript (`scripts/shader_coverage.gd`) that programmatically creates a scene exercising **100% of RenderingDevice shader paths**:

- **Environment:** Sky, SSAO, SSIL, SSR, volumetric fog, SDFGI, VoxelGI, glow, DOF, tonemap, TAA, FSR2
- **Materials:** 20+ StandardMaterial3D variants (normal maps, emission, clearcoat, anisotropy, SSS, refraction, parallax, rim, backlight, alpha scissor/hash/depth-prepass, unshaded, billboard, proximity/distance fade)
- **Lighting:** 7 lights (for cluster fill), directional with 4-split shadows, omni + spot shadows
- **Particles:** GPU particles with trails, turbulence, collision, attractors
- **Canvas 2D:** ColorRect, Label (MSDF), NinePatchRect, PointLight2D
- **Instancing:** MultiMesh (64 instances), Skeleton mesh
- **Post-processing:** Decals, reflection probes, fog volumes, motion vectors, luminance reduction

See [test_project/README.md](test_project/README.md) for the full shader list.

## Expected Failures

`shader_corpus/expected_failures.json` tracks shader variants that fail conversion offline but are never used at runtime. These are Vulkan-only variants the WebGPU path never uses:

| Category | Count | Reason |
|----------|-------|--------|
| ComparisonSamplingMismatch | 14 | Soft shadow variants with depth comparison pattern |
| UnsupportedStorageClass(11) | 3 | Image storage class (Vulkan-only) |
| UnsupportedBuiltIn(23) | 2 | PointSize (not in WebGPU) |
| InvalidId | 3 | Spec constant cascade in Tonemap/TAA |
| InvalidImage | 3 | SDFGI image type issues |
| InvalidBinaryOperandTypes | 2 | 64-bit integer multiply in SDFGI/Cluster |
| Other | 5 | InvalidTypeWidth (16-bit FSR), IncompleteData, BuiltinArgs |

The validator **passes** as long as no new failures appear beyond this baseline. If you add new shaders that legitimately can't convert offline, update the baseline:

```bash
node webgpu_tests/shader_corpus/validate_spirv_dump.mjs /tmp/spirv_dump/ --update-baseline
```

## Troubleshooting

**"No .spv files found"** — You ran with `--headless`. Use `--quit-after 10` instead (shaders need rendering to compile).

**"wgsl_convert_cli not found"** — The shader corpus and SPIR-V dump validator use a standalone converter (`bin/wgsl_convert_cli`, `./drivers/webgpu/wgsl_cli/build.sh`) for offline validation. If it's not available, the tests skip — the runtime converter (linked into the engine WASM) handles all conversion.

**New shader failures after engine changes** — Run validation, review the new errors. If they're Vulkan-only variants, update the baseline. If they affect WebGPU runtime, fix the GLSL or SPIR-V preprocessing.

**Smoke test timeout** — The engine has 2 minutes to start and report PASS. If it hangs, check Chrome console output with `VERBOSE=1 node smoke_test.mjs ./export/`.

**A `threads=yes` export hangs forever under one of these harnesses** — the page is almost certainly not cross-origin isolated. Threaded builds need `SharedArrayBuffer`, which needs the server to send `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`. Several harnesses here spin up their own `http.createServer` and **do not send them**: `startup_phases/features.mjs`, `resource_lifecycle/run_tests.mjs`, `spec_constant_overrides/run_tests.mjs` and all five `sdfgi_race_repro/run_*.mjs`. `benchmark/run_benchmark.sh:220-221` is the pattern to copy. Check from the page with `window.crossOriginIsolated`.

**A threaded export wedges with repeated `net::ERR_ABORTED` on the worker script** — check the export preset's `threads/emscripten_pool_size`. It must be `8` (the engine default); the link flag is `-sPTHREAD_POOL_SIZE="Module['emscriptenPoolSize']||8"` and `-1 || 8` evaluates to `-1`, which wedges every threaded build. See `webgpu_notes/TASKS.md`

**A threaded + GDExtension export never finishes loading** — `threads=yes dlink_enabled=yes` needs this fork's two Emscripten toolchain patches (`misc/emsdk_patches/apply.sh --apply`); without `0001` a worker throws `Cannot set properties of undefined (setting '<addr>')` and the module never loads. The build warns, but `emsdk install` silently reverts the patches. `--status` describes the *toolchain*, not what is in `bin/` — grep the linked output when it matters (`misc/emsdk_patches/README.md` has the commands). A booting threaded dlink export should now log **zero** errors; the ~11 `growMemViews` "Cannot read properties of undefined (reading 'buffer')" errors that used to be expected noise were `0002`, now fixed.

**Worker errors are invisible in Playwright** — `page.on('console')` does not forward worker-target output, and threaded failures are raised on workers. Use `page.on('pageerror')`, which does see them.

**Screenshots during a web startup land at the wrong time** — `page.screenshot()` stalls while the main thread is blocked, so a wall-clock capture schedule produces mislabelled images (one taken at "3 s" can show a frame from 11 s). Trigger captures on DOM state with `waitForFunction` instead.

**Export fails with "No export template found"** — Template must be installed at the path matching the editor's version string. Check `bin/godot --version` and install to the corresponding `export_templates/<version>/` directory.
