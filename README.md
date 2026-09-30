<p align="center">
  <img src="misc/logo/logo_outlined.svg" width="100" alt="Godot Engine logo">
</p>

<h1 align="center">Godot <strong>WebGPU</strong> Forward+</h1>

<p align="center">
  <strong>🚀 Godot Web Games are now 5x faster 🚀</strong>
</p>

<p align="center">
  This is a fork of <a href="https://github.com/dwalter/godotwebgpu">dwalter/godotwebgpu</a>, extending it toward a nearly-full <strong>Forward+</strong> renderer and syncing to a newer upstream Godot release. All credit for originating this project — the WebGPU rendering driver, the SPIR-V&rarr;WGSL shader pipeline, and the original Forward Mobile implementation — goes to <a href="https://x.com/davidpwalter">David Walter</a>. Thank you, David, for doing the hard part first.
</p>

<p align="center">
  <a href="https://godotwebgpu.com"><img src="https://godotwebgpu.com/screenshots/3d_platformer_final.png" width="720" alt="Godot WebGPU — 3D Platformer Demo"></a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Beta-gray?style=flat-square" alt="Beta">
  <img src="https://img.shields.io/badge/Fork_of-dwalter%2Fgodotwebgpu-8957e5?style=flat-square" alt="Fork of dwalter/godotwebgpu">
  <img src="https://img.shields.io/badge/Godot-4.7.2-478cbf?style=flat-square" alt="Godot 4.7.2">
  <img src="https://img.shields.io/badge/Renderer-Forward%2B-478cbf?style=flat-square" alt="Forward+ Renderer">
  <img src="https://img.shields.io/badge/WebGPU-1.0-478cbf?style=flat-square" alt="WebGPU 1.0">
  <img src="https://img.shields.io/badge/Compute_Shaders-supported-d29922?style=flat-square" alt="Compute Shaders">
  <img src="https://img.shields.io/badge/3D_Physics-Box3D-d29922?style=flat-square" alt="Box3D Physics">
  <img src="https://img.shields.io/badge/Chrome-113+-478cbf?style=flat-square" alt="Chrome 113+">
  <img src="https://img.shields.io/badge/Safari-18+-478cbf?style=flat-square" alt="Safari 18+">
  <img src="https://img.shields.io/badge/Firefox-120+-478cbf?style=flat-square" alt="Firefox 120+">
  <img src="https://img.shields.io/badge/Chrome_Android-supported-478cbf?style=flat-square" alt="Chrome Android">
  <img src="https://img.shields.io/badge/Safari_iOS-supported-478cbf?style=flat-square" alt="Safari iOS">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/AI_Generated-Claude-8957e5?style=flat-square" alt="AI Generated">
  <a href="https://github.com/dwalter/godotwebgpu"><img src="https://img.shields.io/badge/Free_%26_Open_Source-MIT-478cbf?style=flat-square" alt="Free & Open Source"></a>
</p>

<p align="center">
  <a href="#documentation"><img src="https://img.shields.io/badge/Documentation-view-478cbf?style=flat-square" alt="Documentation"></a>
  <a href="https://github.com/dwalter/godotwebgpu"><img src="https://img.shields.io/badge/GitHub-Repo-888?style=flat-square" alt="GitHub Repo"></a>
  <a href="https://github.com/godotengine/godot-proposals/issues/6646#issuecomment-4362021374"><img src="https://img.shields.io/badge/Proposal-%236646-478cbf?style=flat-square" alt="Proposal #6646"></a>
</p>

<br>

<table align="center">
  <tr>
    <td align="center"><strong>5x</strong><br><sub>FPS vs WebGL</sub></td>
    <td align="center"><strong>80%</strong><br><sub>FPS vs Native</sub></td>
    <td align="center"><strong>34K+</strong><br><sub>Lines Written</sub></td>
    <td align="center"><strong>146</strong><br><sub>Shaders Converted</sub></td>
  </tr>
</table>

<br>

> **Looking for the original Godot Engine README?** See [GODOT_README.md](GODOT_README.md).

---

## Frequently Asked Questions

Common questions about the WebGPU backend — architecture, performance, compatibility, and more.

**[Read the FAQ &rarr;](webgpu_site/FAQ.md)**

---

## About This Fork

This repository is a fork of **[dwalter/godotwebgpu](https://github.com/dwalter/godotwebgpu)**, the original Godot WebGPU project created by **[David Walter](https://x.com/davidpwalter)** of [Shiny Gen](https://shinygen.ai). David did the foundational work: the entire `drivers/webgpu/` `RenderingDeviceDriver` implementation, the 12-pass SPIR-V-to-WGSL shader pipeline built on Tint, and the original **Forward Mobile** renderer target. None of this fork would exist without that work — thank you, David.

Building on that base, this fork has two main goals:

- **Forward+ renderer support.** The upstream project targets Godot's Forward Mobile renderer. This fork is working toward running the **Forward+** renderer (Godot's default desktop-class renderer, with clustered lighting, SDFGI, and other features Forward Mobile omits) over WebGPU — currently a full Forward+ pipeline, including SDFGI.
- **Newer upstream Godot.** The original project forked from Godot 4.6.2. This fork has been synced forward and currently tracks **Godot 4.7.2** (see `webgpu_notes/TASKS.md` Phase 8 for sync history and status).
- **Box3D as the default 3D physics engine.** A new `PhysicsServer3D` implementation on top of Erin Catto's [Box3D](https://github.com/erincatto/box3d) — see [3D Physics](#3d-physics-box3d) below. Independent of the renderer work; it happens to matter most on the web, where Godot's other 3D physics options are the heaviest part of the build.
- **Editor quality-of-life.** Small in-editor additions on top of upstream: mouse events in the script editor, and a per-project default directory for newly created scripts.

---

## 3D Physics: Box3D

`modules/box3d_physics/` implements Godot's `PhysicsServer3D` on top of the official
[Box3D](https://github.com/erincatto/box3d) rigid-body engine (vendored in `thirdparty/box3d`, plain C17, MIT).
It registers as **"Box3D Physics"** with default priority, so `physics/3d/physics_engine = DEFAULT`
resolves to it and new projects created by the editor are written with it. **"Jolt Physics"** and
**"GodotPhysics3D"** remain selectable in the project settings, so nothing here is one-way.

It is a real physics server, not a GDExtension exposing its own node types: it backs `RigidBody3D`,
`CharacterBody3D`, `StaticBody3D`, `Area3D` and the `PhysicsDirectSpaceState3D` queries like any other
backend. The module mirrors `modules/jolt_physics/` in structure (`objects/`, `shapes/`, `spaces/`, `joints/`).

**Validated** against headless GDScript scenes run under Box3D, Jolt and GodotPhysics3D with matching
results: rigid/kinematic/static bodies, every shape type, areas (monitoring, gravity/damping overrides,
area-area, ray picking), contact reporting, `CharacterBody3D` (`body_test_motion`, floor detection, moving
platforms), ray/point/shape queries, `cast_motion`, `collide_shape`, `get_rest_info`, hinge/slider/pin/cone-twist
joints with limits and motors, constant surface velocity, CCD, custom integrators, axis locks, the separate
physics-thread mode, and a randomized create/destroy/modify stress test.

**Known gaps**, each reported once through `Box3DDiagnostics::report_unsupported()` with the requested
values and the reason, so they can be diagnosed from a log alone: soft bodies (`SoftBody3D` is inert),
generic 6DOF joints (approximated as weld/ball/prismatic where the locked axes allow, otherwise not
built), joint motors/springs beyond hinge motors, joint solver priority/iteration overrides, custom shape
solver bias, non-uniform scale on spheres/capsules, concave/heightmap shapes on non-static bodies, back-face
collision on concave shapes, angular constant velocity on static bodies, debug contact visualization, and
`cast_motion` with rest info.

**Approximations worth remembering**: cylinders are 32-sided prisms, `WorldBoundaryShape3D` is a 2000 m
box, height maps are triangle meshes, and damping uses Box3D's formula rather than Godot's, so heavily
damped bodies differ slightly. Web builds always run Box3D single-threaded (`workerCount = 1`) — its own
thread creation cannot work from the web export's fixed pthread pool.

See `webgpu_notes/TASKS.md` Phase 11 for the full status, and `CLAUDE.md` for the architectural notes
that are easy to trip over (Box3D shapes belong to one body with their transform baked in; Godot's
one-directional layer/mask rules are enforced by a custom filter rather than Box3D's symmetric bits).

---

## Browser Compatibility

| Platform | Browser | Verified by | Result |
|----------|---------|-------------|--------|
| Linux | Chrome | Scene smoketest, 19 scenes, 2026-09-27 | **19 pass, 0 fail, 0 skip**. Needs Vulkan + WebGPU browser flags, and a native package install — Flatpak and Snap sandboxing blocks the required GPU access |
| Linux | Firefox | Scene smoketest, 19 scenes, 2026-09-27 | **19 pass, 0 fail, 0 skip**. Same flag and native-package requirements |
| Linux | Vivaldi | Manual, 2026-09-16 | Loads and renders. Same flag and native-package requirements |
| Windows | Chrome, Firefox, Edge | Manual, 2026-09-16 | Loads and renders, out of the box, no flags |
| macOS | Chrome 113+, Firefox, Safari 18+ | Manual, upstream | Loads and renders. Never run against the scene smoketest (it needs AppleScript to drive Safari, and no macOS machine is in this fork's loop) |
| Android | Chrome | Not measured in this fork | Reported working upstream. The Adreno float32-filterable fallbacks exist because of real Adreno behavior, but no scene run is recorded here — `TASKS.md` Task 5.2 still lists Android as outstanding |
| iOS | Safari 26.0+ | Not measured in this fork | Reported working upstream; Task 5.2 lists iOS as outstanding |

The smoketest is the only automated per-scene measurement, and it covers 19 scenes (8 benchmarks, 10 demos, 1 stress test) drawn from `godot-demo-projects` and this repo's own fixtures — see [`webgpu_tests/scene_smoketest`](webgpu_tests/scene_smoketest). Where a row says *manual*, it means a human loaded exports and looked at them; treat it as "no known problems" rather than a coverage figure. Every scene passes, and nothing is skipped.

### Known Issues

Outstanding problems as of **2026-09-30**. Everything here is reproduced and diagnosed; items marked *workaround* have a known way around them, items marked *open* do not. Task numbers index into [`webgpu_notes/TASKS.md`](webgpu_notes/TASKS.md); [`webgpu_notes/HANDOFF.md`](webgpu_notes/HANDOFF.md) carries the current state and what to pick up next.

**Rendering**

| Issue | Status | Notes |
|-------|--------|-------|
| Volumetric fog looks blockier than native (froxel sampling) | open, uninvestigated (Task 12.1) | Not projector- or shadow-specific; noticed while verifying light projectors, which are otherwise pixel-equivalent to native Vulkan |
| `command_render_clear_attachments` is a no-op | won't fix | WebGPU has no mid-pass attachment clear. Confirmed dead code on every Godot backend today |
| `draw_indexed_indirect_count` / `draw_indirect_count` ignore the count buffer | won't fix | WebGPU has no multi-draw-indirect-count. No current renderer uses count-buffer indirect draws |
| Explicit `command_resolve_texture()` is a stub | won't fix for now | MSAA goes through render-pass `resolveTarget`, which is implemented. Only out-of-pass resolves are missing |
| No hardware multiview, VRS, or subgroups; subpass post-processing disabled; `binding_array` flattened to one element (no multi-lightmap); omni shadows forced to dual-paraboloid | by design | WebGPU feature gaps — see [Correctness & Compatibility](webgpu_site/CORRECTNESS_AND_COMPATIBILITY.md) |

**Readback and formats**

| Issue | Status | Notes |
|-------|--------|-------|
| `buffer_map()` returns a CPU shadow copy, so readback is a frame behind | open (Task 7.8) | Synchronous GPU readback is impossible on single-threaded WASM. Some paths load from disk instead |
| `RenderingDevice.texture_get_data()` returns nothing on the first call | open (Task 45) | Consequence of the above: the first call starts the copy-and-map and the data lands a frame later, and `rd.sync()` cannot wait for it. Web GDScript that needs a readback must retry or use `texture_get_data_async()`. `GradientTexture1D/2D.get_image()` no longer round-trips through the GPU and works normally |
| 16-bit unorm/snorm texture formats are reported unsupported and converted to 32-bit float | workaround (Task 7.10) | emdawnwebgpu has no `R16Unorm`/`Snorm` family at all. Costs memory; correctness is fine. Vertex attributes are unaffected |
| Canvas SDF uses `R16_SFLOAT` instead of `R16_SNORM` | workaround (Task 44) | Dawn reports `R16Snorm`'s sample type as `UnfilterableFloat` while the SDF samples it with a filtering sampler |
| Storage textures need format promotion (`R8`→`R32Float`, `rgb10a2unorm`→`rgba16float` on Firefox); no 3-component formats; no component swizzle; sRGB `viewFormats` excluded for storage textures | by design | CPU-side expansion happens once per texture at load |
| Float32 textures downgraded to float16 on Adreno | vendor workaround | Precision loss on affected Android GPUs |

**Build and export**

| Issue | Status | Notes |
|-------|--------|-------|
| `threads=yes` with `dlink_enabled=yes` (threads *and* GDExtension support) fails on startup | unsupported (Task 12) | An `ASM_CONSTS` initialization-order race inside Emscripten's own dylink+pthread glue, not this fork's code. The other three combinations all work |
| Exports with `variant/extensions_support` off can abort on load with `Aborted(native code called abort())` | open, upstream (Task 11) | Root-caused to a 4-byte heap-buffer-overflow in the pinned emdawnwebgpu port's `WGPUInstanceImpl` constructor. Not fixable here without vendoring a port patch or a toolchain bump; enabling extension support is the workaround |
| Interleaving editor and web builds in one tree ships a template that dies in `callMain()` | workaround (Task 40) | Stale `register_module_types.gen` object. Delete it plus `libmodules.a` and rebuild; verify with `grep -ac initialize_betsy_module bin/godot.side.web.*.wasm` (want 0) |
| The editor and the export template must be built from the same commit | by design (Task 36) | Baked shader caches are keyed to the engine version hash. A mismatch silently discards the whole cache (`{baked: 0, translated: N}` and a ~5× slower load); the engine now reports it |
| An export made with `--headless` silently skips the shader baker | workaround | 15 MB `.pck` instead of ~135 MB. Use `xvfb-run` with a real rendering driver |
| `tint_convert_cli` is compiled without `-DNDEBUG` | open (Task 13.1) | SPIRV-Tools/Tint asserts are live in the host tool; they surface as isolated "Tint crashed" bake entries |
| Box3D's web template link and browser smoke test are unverified | open (Task 11.1) | Native editor builds and passes its GDScript validation scenes; the module compiles for `wasm32` with `-msimd128 -msse2`, but a full web link and in-browser physics run have not been done. `precision=double` (`BOX3D_DOUBLE_PRECISION`) is wired but never built |

**Performance and lifetime**

| Issue | Status | Notes |
|-------|--------|-------|
| A fixed ~500 ms `Servers:Rendering` engine-init cost on every project, ~180 ms of it our own per-stage WGSL text scanning | open (Task 14) | Baking binding metadata into the shader container at export time is the remaining win. Project-dependent load time is already down from ~9.2 s to ~1.0 s |
| Temporary texture views in `WGUniformSet::temp_views` may not be released | open (Task 7.15) | Suspected leak; not yet characterized as per-frame or unbounded |
| Specialized shader modules may not be released by `pipeline_free()` | open (Task 7.17) | Narrowed by Task 25's move to WGSL `@id(N) override`s, which removes most re-specialization |
| WGSL format-name remapping patches strings in place, assuming equal lengths | open (Task 7.18) | Fragile if Tint's output names change. Task 9.15 moved some of these patches to Tint IR transforms and made the rest `memcpy`-safe |
| No device-loss recovery | open | Logged only; the page must be reloaded |

**Verification gaps**

- Safari has never been run against the scene smoketest (macOS only); the 100% figures above come from manual testing.
- Texture compression is settled on desktop (BC, both browsers); Safari and mobile formats are unmeasured (Task 39).
- Box3D is validated natively against Jolt and GodotPhysics3D, but not in a browser; no scene in the smoketest exercises 3D physics under Box3D on the web.

---

## Test Suite

Every tier below was green at the last full run, **2026-09-27**, with nothing skipped except Safari
(macOS only, and no macOS machine is in this fork's loop). See [`webgpu_tests/README.md`](webgpu_tests/README.md)
for what each tier covers and how to run it.

| Tier | Result | Needs |
|------|--------|-------|
| `shader_corpus` | **14 / 14** | `tint_convert_cli` only — no engine build |
| `driver_unit_tests` | **332 pass, 0 fail** | Node |
| `preprocessing_tests` | **205 pass, 0 fail, 1 skipped** | Node |
| `resource_lifecycle` | all pass | Playwright |
| `screenshot_comparison` | **8 pass, 0 fail** | Playwright |
| Scene smoketest — Chrome | **19 pass, 0 fail, 0 skip** | Editor + web template build |
| Scene smoketest — Firefox | **19 pass, 0 fail, 0 skip** | Editor + web template build |
| Scene smoketest — Safari | not run | macOS + AppleScript |
| `./webgpu_tests/local_ci.sh --no-safari` (all of the above plus `spec_constant_overrides` and `wgsl_cache`) | **15 passed, 0 failed, 1 skipped**, exit 0 | Full rebuild + re-export |

The 19 scenes are 8 benchmarks, 10 demos and 1 stress test, drawn from `godot-demo-projects` and this
repo's own fixtures. `local_ci.sh` rebuilds the editor, the dlink template (compile check only) and the
non-dlink nothreads template, then re-exports every scene from them before testing — so a green run
reflects the working tree rather than whatever exports happened to be on disk.

One trap worth knowing up front: **CI builds with `dev_mode=yes`, which implies `warnings=extra werror=yes`,
and none of the individual tiers do.** A warning that is fatal in CI is invisible locally; run
`./webgpu_tests/local_ci.sh --dev-mode` before assuming a local green means CI will agree.

---

## Live Demos

Try each demo in your browser at **[godotwebgpu.com](https://godotwebgpu.com)**. WebGPU requires Chrome 113+ or Safari 18+.

<table>
<tr>
<td align="center" width="33%">
<a href="https://godotwebgpu.com/#demos">
<img src="https://godotwebgpu.com/screenshots/2d_particles_final.png" width="300" alt="GPU Particles 2D"><br>
<strong>GPU Particles 2D</strong>
</a><br>
<sub>2D &bull; Particle trails, collision, multiple emitters &bull; <strong>NEW on Web</strong></sub>
</td>
<td align="center" width="33%">
<a href="https://godotwebgpu.com/#demos">
<img src="https://godotwebgpu.com/screenshots/compute_texture_final.png" width="300" alt="Compute Texture"><br>
<strong>Compute Texture</strong>
</a><br>
<sub>Compute &bull; Compute shader populating textures in real-time &bull; <strong>NEW on Web</strong></sub>
</td>
<td align="center" width="33%">
<a href="https://godotwebgpu.com/#demos">
<img src="https://godotwebgpu.com/screenshots/3d_particles_final.png" width="300" alt="GPU Particles 3D"><br>
<strong>GPU Particles 3D</strong>
</a><br>
<sub>3D &bull; Compute-driven particles: fire, burst effects &bull; <strong>NEW on Web</strong></sub>
</td>
</tr>
<tr>
<td align="center">
<a href="https://godotwebgpu.com/#demos">
<img src="https://godotwebgpu.com/screenshots/compute_heightmap_final.png" width="300" alt="Compute Heightmap"><br>
<strong>Compute Heightmap</strong>
</a><br>
<sub>Compute &bull; GPU compute shader generating terrain heightmap &bull; <strong>NEW on Web</strong></sub>
</td>
<td align="center">
<a href="https://godotwebgpu.com/#demos">
<img src="https://godotwebgpu.com/screenshots/2d_platformer_final.png" width="300" alt="2D Platformer"><br>
<strong>2D Platformer</strong>
</a><br>
<sub>2D &bull; Sprites, parallax backgrounds, physics</sub>
</td>
<td align="center">
<a href="https://godotwebgpu.com/#demos">
<img src="https://godotwebgpu.com/screenshots/3d_platformer_final.png" width="300" alt="3D Platformer"><br>
<strong>3D Platformer</strong>
</a><br>
<sub>3D &bull; PBR materials, directional shadows, CharacterBody3D</sub>
</td>
</tr>
<tr>
<td align="center">
<a href="https://godotwebgpu.com/#demos">
<img src="https://godotwebgpu.com/screenshots/3d_lights_and_shadows_final.png" width="300" alt="Lights & Shadows"><br>
<strong>Lights & Shadows</strong>
</a><br>
<sub>3D &bull; Directional, omni, spot lights with PCSS shadows</sub>
</td>
<td align="center">
<a href="https://godotwebgpu.com/#demos">
<img src="https://godotwebgpu.com/screenshots/2d_sprite_shaders_final.png" width="300" alt="Sprite Shaders"><br>
<strong>Sprite Shaders</strong>
</a><br>
<sub>2D &bull; Outline, blur, shadow, silhouette effects</sub>
</td>
<td align="center">
<a href="https://godotwebgpu.com/#demos">
<img src="https://godotwebgpu.com/screenshots/viewport_gui_in_3d_final.png" width="300" alt="GUI in 3D"><br>
<strong>GUI in 3D</strong>
</a><br>
<sub>Viewport &bull; SubViewport rendering 2D GUI on a 3D surface</sub>
</td>
</tr>
<tr>
<td align="center">
<a href="https://godotwebgpu.com/#demos">
<img src="https://godotwebgpu.com/screenshots/gui_control_gallery_final.png" width="300" alt="Control Gallery"><br>
<strong>Control Gallery</strong>
</a><br>
<sub>UI &bull; Full showcase of all Godot UI controls</sub>
</td>
<td></td>
<td></td>
</tr>
</table>

---

## Performance Benchmarks

Stress tests comparing WebGPU (Forward Mobile) vs WebGL (Compatibility) at high object counts. FPS measured on Mac Studio M3 Ultra, Chrome 134.

Try all benchmarks live at **[godotwebgpu.com/#benchmarks](https://godotwebgpu.com/#benchmarks)**.

| | Benchmark | Description | WebGPU vs WebGL |
|---|-----------|-------------|:---:|
| <img src="https://godotwebgpu.com/screenshots/webgpu_scene_a_final.png" width="120"> | **Sprites** | Bouncing sprites with random colors | 5x faster |
| <img src="https://godotwebgpu.com/screenshots/webgpu_scene_b_final.png" width="120"> | **PBR Sphere** | High-poly PBR sphere with directional shadow | 4x faster |
| <img src="https://godotwebgpu.com/screenshots/webgpu_scene_c_final.png" width="120"> | **Cubes + Lights** | Rotating cubes with shadow-casting lights | 4x faster |
| <img src="https://godotwebgpu.com/screenshots/webgpu_scene_d_final.png" width="120"> | **GPU Particles** | GPU particles with gradient colors | 5x faster |
| <img src="https://godotwebgpu.com/screenshots/webgpu_scene_e_final.png" width="120"> | **Skeletal Animation** | GPU-skinned cylinders with bone animations | New |
| <img src="https://godotwebgpu.com/screenshots/webgpu_scene_f_final.png" width="120"> | **Other Effects** | SSAO + Bloom + SubViewport with PBR cubes | New |

---

## Downloads

### [Build from Source](#building-from-source)

Clone the repo and build the engine yourself.

### [Download the GodotWebGPU Editor](https://github.com/dwalter/godotwebgpu/releases) *(Coming Soon)*

Pre-built macOS editor with WebGPU export support.

---

## Building from Source

Three things get built, and they are independent of each other:

```bash
# 1. The native editor — fast, no Emscripten needed. This is what exports web builds.
scons platform=linuxbsd target=editor dev_build=yes -j$(nproc)   # or platform=macos / windows

# 2. The WebGPU export template — the real target, needs Emscripten:
source ~/emsdk/emsdk_env.sh
scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no -j$(nproc)

# 3. tint_convert_cli — the host tool that precompiles SPIR-V to WGSL at build time:
./drivers/webgpu/tint_cli/build.sh          # --clean for a full rebuild
```

`webgpu=yes` only takes effect with `platform=web`; `drivers/webgpu/` is not compiled into native builds
at all. An editor built with `webgpu=yes` additionally gets the export-time WGSL shader baker, and runs
`tint_convert_cli` from its own executable's directory — so the two must sit next to each other.

Requirements:
- Emscripten 4.0.10+ (for the emdawnwebgpu port; this fork pins 6.0.9)
- No Rust toolchain needed (Tint C++ translator is compiled directly into the engine)
- Standard Godot build dependencies (SCons, Python, C++ compiler)

Two build traps that have each cost a day:
- **The editor and the export template must be built from the same commit.** Baked shader caches are
  keyed to the engine version hash; a mismatch silently discards the whole cache and loads ~5× slower.
- **An export made with `--headless` skips the shader baker entirely** — a 15 MB `.pck` instead of
  ~135 MB. Use `xvfb-run` with a real rendering driver.

---

## Documentation

In-depth technical documentation for the WebGPU backend.

### [Architecture & Design](webgpu_site/ARCHITECTURE_AND_DESIGN.md)

High-level architecture, key design decisions, shader pipeline, and how the WebGPU backend compares to Vulkan/Metal.

### [Technical Reference](webgpu_site/TECHNICAL_REFERENCE.md)

Driver core, command recording, bind groups, shader pipeline details, texture/buffer operations, and build system.

### [Performance & Optimization](webgpu_site/PERFORMANCE_AND_OPTIMIZATION.md)

The journey from 3.25x slower to parity — staging buffers, shadow pass merging, instance batching, and IPC reduction.

### [Correctness & Compatibility](webgpu_site/CORRECTNESS_AND_COMPATIBILITY.md)

Resource lifecycle guarantees, cross-browser compatibility, error handling, testing coverage, and known limitations.

### [Frequently Asked Questions](webgpu_site/FAQ.md)

Common questions about the WebGPU backend — architecture, performance, compatibility, and more.

---

## Key Stats

| Metric | Value |
|--------|-------|
| WebGPU driver | 23,369 lines across `drivers/webgpu/`, 11,968 of them in the core `.cpp` |
| Box3D physics module | 11,141 lines in `modules/box3d_physics/` |
| Shaders converted | 146 engine shaders (SPIR-V → WGSL via Tint), 189 WGSL variants baked into the current build |
| SPIR-V rewriting passes | 12 (combined-sampler splitting, push-constant emulation, depth fixups, subgroup lowering, …) |
| Renderer | Forward+ (this fork) / Forward Mobile (original) |
| 3D physics | Box3D by default; Jolt and GodotPhysics3D still selectable |
| Performance vs native | ~80% of Vulkan/Metal FPS |
| Performance vs WebGL | Up to 5x faster |
| Cold load time | ~1.0 s on the reference project, down from ~9.2 s |
| Browser support | Chrome 113+, Firefox 120+, Safari 18+, Chrome Android, Safari iOS |

---

<p align="center">
  Sponsored by <a href="https://shinygen.ai">Shiny Gen AI</a>
</p>

<p align="center">
  <sub>Godot WebGPU, Shiny Gen AI, and David Walter are not directly affiliated with or endorsed by the Godot Foundation. It uses the GODOT&reg; name and logos under a permissive license granted by the Godot Foundation. Godot WebGPU is a free and open source fork of the Godot Engine. Shiny Gen is a game maker app built with Godot. And David Walter is the developer of Shiny Gen and Godot WebGPU.</sub>
</p>
