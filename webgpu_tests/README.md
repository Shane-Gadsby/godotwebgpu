# WebGPU Test Suite

Automated tests for the Godot WebGPU rendering backend. Validates the full shader pipeline from GLSL compilation through SPIR-V → WGSL conversion to in-browser execution.

## Test Categories

| Test | What it validates | Runtime | Needs engine build? |
|------|------------------|---------|---------------------|
| [Shader Corpus](shader_corpus/) | SPIR-V → WGSL conversion via Tint CLI | ~1s | No (needs Tint CLI) |
| [SPIR-V Validation](shader_corpus/validate_spirv_dump.mjs) | ALL engine-compiled SPIR-V through Tint | ~5s | Yes (editor) |
| [Spec-Constant Overrides](spec_constant_overrides/) | Specialization constants survive as `@id(N) override`, and WebGPU pipeline constants set them | ~5s | No (needs Tint CLI) |
| [Smoke Test](test_project/smoke_test.mjs) | Full runtime in headless Chrome — no shader errors, no device lost | ~60s | Yes (editor + web template) |
| [Scene Smoketest](scene_smoketest/) | 21 demo/benchmark scenes across Chrome, Firefox, and Safari | ~8min | Yes (pre-exported) |
| [Resource Lifecycle](resource_lifecycle/) | Rapid create/destroy of buffers, textures, pipelines | ~30s | No (standalone) |
| [Screenshot Comparison](screenshot_comparison/) | Visual regression across Chrome and Firefox | ~60s | No (standalone) |
| [Font Rendering](scene_smoketest/test_font_visual.mjs) | Text renders in the colors it was asked to — guards the glyph-modulate regression | ~20s | Yes (pre-exported) |
| [Font Assertion Self-Test](scene_smoketest/self_test_font_visual.mjs) | That the font test's own thresholds can still fail | <1s | No (standalone) |
| [Fog Smoothness](scene_smoketest/test_fog_visual.mjs) | Volumetric fog's froxel volume is still sampled smoothly, not banded | ~25s | Yes (pre-exported, needs a real GPU) |
| [Fog Assertion Self-Test](scene_smoketest/self_test_fog_visual.mjs) | That the fog test's own thresholds can still fail | <1s | No (standalone) |
| [Forward+ Feature Matrix](forward_plus/) | Every Forward+ feature still changes the frame — 64 of them | ~12min | Yes (own export, **needs a real GPU**) |
| [Forward+ Matrix Self-Test](forward_plus/self_test_forward_plus.mjs) | That the matrix's own verdict logic and feature table are sound | <1s | No (standalone) |
| [Startup Phases](startup_phases/) | Where a real export's load time actually goes, phase by phase | ~60s/run | No (profiles any existing export) |

## How It Works

The WebGPU shader pipeline is:

```
GLSL → SPIR-V (glslang, at editor build time)
     → 7 binary rewriting passes (C++ spirv_preprocess, at runtime)
     → WGSL (Tint C++ library, linked into engine, at runtime)
     → GPU (browser's WebGPU implementation)
```

The SPIR-V preprocessing passes (C++):
1. **freeze_spec_constant_ops** — Evaluates `OpSpecConstantOp` into plain constants
2. **rewrite_copy_logical** — `OpCopyLogical` → `OpCopyObject` (SPIR-V 1.4+ struct copy)
3. **rewrite_terminate_invocation** — `OpTerminateInvocation` → `OpKill` (modern discard)
4. **infer_readonly_storage** — Adds `NonWritable` to read-only SSBOs
5. **convert_push_constants_to_uniforms** — PushConstant → StorageBuffer at group(3)/binding(120)
6. **split_combined_samplers** — Combined image samplers → separate texture + sampler
7. **fix_depth2_images** — depth=2 (unknown) → depth=1 for comparison sampling

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
node run_forward_plus.mjs --list                # covered and uncovered features
```

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

### 6. Screenshot Comparison (standalone, needs Playwright)

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
│  ┌─────────────────┐  (parallel, no build needed)               │
│  │ scene-smoketest │  18 scenes × Chrome + Firefox              │
│  │                 │  (needs pre-exported scenes — see below;   │
│  │                 │  currently a no-op in CI, see * below)     │
│  └─────────────────┘                                            │
│                                                                 │
│  ┌─────────────────┐                                            │
│  │ build-webgpu    │  Web template + Linux editor + export      │
│  │ (~60 min)       │  + SPIR-V dump (309 shaders)               │
│  └────────┬────────┘                                            │
│           │                                                     │
│           ├──────────────┐                                      │
│           ▼              ▼                                      │
│  ┌────────────────┐  ┌──────────────┐                           │
│  │ validate-spirv │  │ smoke-test   │                           │
│  │ All SPIR-V     │  │ Headless     │                           │
│  │ through Tint   │  │ Chrome run   │                           │
│  └────────────────┘  └──────────────┘                           │
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
| `scene-smoketest` | — | 20 min | Yes* |
| `resource-lifecycle` | — | 15 min | Yes |
| `screenshot-comparison` | — | 20 min | No (warning only) |
| `test-summary` | all above | — | — |

\* **Known gap**: `scene-smoketest` runs `run_scenes.mjs` with the default `--skip-export`, but no exported scenes are checked into the repo and nothing exports them first in CI. Every scene comes back `SKIP` and the job passes having tested nothing — it isn't a real gate today despite the table above. Making it real requires: exporting the 18 scenes from a Linux editor + WebGPU template in CI (`run_scenes.mjs --export`), and fixing each scene project's `export_presets.cfg` first — they currently hardcode another contributor's local absolute template path (`/Users/dwalter/...`) and have `variant/extensions_support=false`, which silently resolves to the *non*-WebGPU template (the exact trap documented in `webgpu_notes/TASKS.md`'s Round 23 notes). `shader-corpus`, `validate-spirv`, `smoke-test`, `resource-lifecycle`, and `screenshot-comparison` do not have this problem — they build/use their dependencies for real.

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

**Export fails with "No export template found"** — Template must be installed at the path matching the editor's version string. Check `bin/godot --version` and install to the corresponding `export_templates/<version>/` directory.
