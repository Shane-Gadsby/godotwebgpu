# WebGPU Test Suite

Automated tests for the Godot WebGPU rendering backend. Validates the full shader pipeline from GLSL compilation through SPIR-V → WGSL conversion to in-browser execution.

## Test Categories

| Test | What it validates | Runtime | Needs engine build? |
|------|------------------|---------|---------------------|
| [Shader Corpus](shader_corpus/) | SPIR-V → WGSL conversion via Tint CLI | ~1s | No (needs Tint CLI) |
| [SPIR-V Preprocessing](preprocessing_tests/) | Each SPIR-V pass in isolation, including the IR passes (`spirv-val` on their output) — 291 assertions | ~10s | No (needs Tint CLI) |
| [Driver Unit Tests](driver_unit_tests/) | Bind-group layout, format and pipeline-state logic, the expanded-array binding layout — 379 assertions | ~2s | No (standalone) |
| [SPIR-V Validation](shader_corpus/validate_spirv_dump.mjs) | ALL engine-compiled SPIR-V through Tint | ~5s | Yes (editor) |
| [Spec-Constant Overrides](spec_constant_overrides/) | Specialization constants survive as `@id(N) override`, and WebGPU pipeline constants set them | ~5s | No (needs Tint CLI) |
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
| [Forward+ Feature Matrix](forward_plus/) | Every Forward+ feature still changes the frame — 71 of them | ~12min | Yes (own export, **needs a real GPU**) |
| [Forward+ Parity](forward_plus/) (`--parity`) | Every matrix frame matches a native Vulkan render of the same configuration | ~15min | Yes (own export, a real GPU, and a native Vulkan device + display for the reference) |
| [Forward+ Matrix Self-Test](forward_plus/self_test_forward_plus.mjs) | That the matrix's own verdict logic and feature table are sound | <1s | No (standalone) |
| [Software Ray Tracing Probe](raytracing_probe/) | Rays per second a WGSL BVH traversal reaches on the adapter (primary, shadow, incoherent bounce) — the ceiling for any ray or path tracing on WebGPU, which has no hardware RT; feeds TASKS.md §2.34 | ~60s | No (standalone; a real GPU for numbers that mean anything) |
| [Android Profiling](perf_guard/android/) | What a WebGPU export does on a real phone over USB — fps/frame-time per export variant, GPU timestamp queries per pass, WGSL dump/substitute for shader bisection | ~75s/variant | No (profiles any existing export; needs an Android phone with USB debugging) |
| [Startup Phases](startup_phases/) | Where a real export's load time actually goes, phase by phase | ~60s/run | No (profiles any existing export) |
| [WebRTC Parity](webrtc_parity/) | WebRTC behaves the same in a web export as it does on desktop — 9 fixtures x 3 export variants x Chrome/Firefox | ~12min (one browser) | Yes (editor + web template) |
| [WebRTC Congestion](webrtc_congestion/) | What WebRTC does when the link is the bottleneck — two processes either side of a `netem`-shaped link. Found F9 and F10 | ~15min (full sweep) | Yes (editor + web template) |

## How It Works

The WebGPU shader pipeline is:

```
GLSL → SPIR-V (glslang, at editor build time)
     → spirv_preprocess::run_pipeline() (C++, at build time, export time or runtime)
     → WGSL (Tint C++ library, linked into engine, at runtime)
     → GPU (browser's WebGPU implementation)
```

`run_pipeline()` is the one ordering of the SPIR-V passes every converter uses; the full list, with what
each pass is for, is in [`webgpu_site/TECHNICAL_REFERENCE.md`](../webgpu_site/TECHNICAL_REFERENCE.md) §4.1.
`preprocessing_tests/` covers the passes one at a time (291 assertions; tests 24-27 cover the three passes on
SPIRV-Tools' IR -- `lower_tint_unsupported_ops`, `apply_sampler_lod_bias`, `expand_binding_arrays` -- and run
`spirv-val` on each output, from `fixtures_src/tint_crash_inputs.frag` and `binding_arrays*.{frag,comp}`).
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

Tests 9 hand-crafted GLSL fixtures through the Tint CLI (if available):

```bash
cd webgpu_tests/shader_corpus
./compile_fixtures.sh    # GLSL → SPIR-V (requires glslangValidator)
node run_tests.mjs       # SPIR-V → WGSL validation (skips gracefully if no Tint CLI)
```

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

The per-feature regression suite: for each of 64 Forward+ features, render the
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
```

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

Validates all 300+ engine-compiled shaders through Tint offline:

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
node run_probe.mjs --software          # swiftshader at 320x180: checks the kernels still run
node run_probe.mjs --url-only          # serve only, open the URL in a browser you already have up
```

A CPU-built binned-SAH BVH over a procedural scene, traversed by a compute shader with a per-thread stack:
primary camera rays, any-hit shadow rays to a point light and cosine-weighted bounce rays from each hit,
each timed with GPU timestamp queries. Prints Mrays/s per kernel, the adapter's limits and features, BVH
build time and traversal steps, and draws the result so a wrong traversal is visible. Exits 2 if a real-GPU
run landed on the fallback adapter. The directory's README explains how to read the numbers; the
investigation they feed is `webgpu_notes/TASKS.md` §2.34.

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
│  │ through Tint   │ │ Chrome run │ │ 23 scenes │ │ 9 fixtures │ │
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

`shader_corpus/expected_failures.json` tracks shader variants that fail Tint validation offline but work at runtime. These are Vulkan-only variants the WebGPU path never uses:

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

**"tint_convert_cli not found"** — The shader corpus and SPIR-V dump validator use a standalone Tint CLI for offline validation. If it's not available, the tests skip gracefully — runtime Tint (linked into the engine WASM) handles all conversion.

**New shader failures after engine changes** — Run validation, review the new errors. If they're Vulkan-only variants, update the baseline. If they affect WebGPU runtime, fix the GLSL or SPIR-V preprocessing.

**Smoke test timeout** — The engine has 2 minutes to start and report PASS. If it hangs, check Chrome console output with `VERBOSE=1 node smoke_test.mjs ./export/`.

**A `threads=yes` export hangs forever under one of these harnesses** — the page is almost certainly not cross-origin isolated. Threaded builds need `SharedArrayBuffer`, which needs the server to send `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`. Several harnesses here spin up their own `http.createServer` and **do not send them**: `startup_phases/features.mjs`, `resource_lifecycle/run_tests.mjs`, `spec_constant_overrides/run_tests.mjs` and all five `sdfgi_race_repro/run_*.mjs`. `benchmark/run_benchmark.sh:220-221` is the pattern to copy. Check from the page with `window.crossOriginIsolated`.

**A threaded export wedges with repeated `net::ERR_ABORTED` on the worker script** — check the export preset's `threads/emscripten_pool_size`. It must be `8` (the engine default); the link flag is `-sPTHREAD_POOL_SIZE="Module['emscriptenPoolSize']||8"` and `-1 || 8` evaluates to `-1`, which wedges every threaded build. See `webgpu_notes/TASKS.md`

**A threaded + GDExtension export never finishes loading** — `threads=yes dlink_enabled=yes` needs this fork's two Emscripten toolchain patches (`misc/emsdk_patches/apply.sh --apply`); without `0001` a worker throws `Cannot set properties of undefined (setting '<addr>')` and the module never loads. The build warns, but `emsdk install` silently reverts the patches. `--status` describes the *toolchain*, not what is in `bin/` — grep the linked output when it matters (`misc/emsdk_patches/README.md` has the commands). A booting threaded dlink export should now log **zero** errors; the ~11 `growMemViews` "Cannot read properties of undefined (reading 'buffer')" errors that used to be expected noise were `0002`, now fixed.

**Worker errors are invisible in Playwright** — `page.on('console')` does not forward worker-target output, and threaded failures are raised on workers. Use `page.on('pageerror')`, which does see them.

**Screenshots during a web startup land at the wrong time** — `page.screenshot()` stalls while the main thread is blocked, so a wall-clock capture schedule produces mislabelled images (one taken at "3 s" can show a frame from 11 s). Trigger captures on DOM state with `waitForFunction` instead.

**Export fails with "No export template found"** — Template must be installed at the path matching the editor's version string. Check `bin/godot --version` and install to the corresponding `export_templates/<version>/` directory.
