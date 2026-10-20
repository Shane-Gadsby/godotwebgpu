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
  <img src="https://img.shields.io/badge/Godot-4.8--dev-478cbf?style=flat-square" alt="Godot 4.8-dev">
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

- **Forward+ renderer support.** The upstream project targets Godot's Forward Mobile renderer. This fork is working toward running the **Forward+** renderer (Godot's default desktop-class renderer, with clustered lighting, SDFGI, and other features Forward Mobile omits) over WebGPU — currently a full Forward+ pipeline, including SDFGI. Verified on real hardware against a real project with Ambient/Reflected Light, Tonemap, SSR, SSAO, SSIL, SDFGI, Glow, Fog, Volumetric Fog and Adjustments all enabled at once, in both Chrome and Firefox: mean absolute difference **0.57 / 255** against a native-Vulkan Forward+ capture of the same frame, with no pixel differing by more than 10. Since 2026-10-17 every Forward+ feature is also checked one at a time against native Vulkan (`webgpu_tests/forward_plus --parity`, 71 features, all within 0.05 / 255 mean except SDFGI's and FSR2's documented differences); what WebGPU itself cannot do is listed in `webgpu_notes/TASKS.md` §2.31.
- **Newer upstream Godot.** The original project forked from Godot 4.6.2. This fork has been synced forward and currently tracks **Godot 4.8** (code freeze; `webgpu_notes/TASKS.md` §4.17 for the port, Phase 8 for the earlier 4.7.2 sync).
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

See `webgpu_notes/TASKS.md` §4.6 for the full status, and `CLAUDE.md` for the architectural notes
that are easy to trip over (Box3D shapes belong to one body with their transform baked in; Godot's
one-directional layer/mask rules are enforced by a custom filter rather than Box3D's symmetric bits).

---

**Serve it compressed.** The dlink side module is 45 MB raw but 11.9 MB gzip / 9.5 MB brotli, and
end to end compression is worth about 3.5x on the whole load. Enable gzip or brotli for `.wasm`, `.js`
and `.pck` on whatever serves the export; it costs no engine work and is the cheapest load-time win
available. Threaded builds also need COOP/COEP cross-origin-isolation headers.

## Browser Compatibility

| Platform | Browser | Verified by | Result |
|----------|---------|-------------|--------|
| Linux | Chrome | Scene smoketest, 21 scenes, 2026-10-06 | **21 pass, 0 fail, 0 skip**. Needs Vulkan + WebGPU browser flags, and a native package install — Flatpak and Snap sandboxing blocks the required GPU access |
| Linux | Firefox | Scene smoketest, 21 scenes, 2026-10-06 | **21 pass, 0 fail, 0 skip**. Same flag and native-package requirements |
| Linux | Vivaldi | Manual, 2026-09-16 | Loads and renders. Same flag and native-package requirements |
| Windows | Chrome, Firefox, Edge | Manual, 2026-09-16 | Loads and renders, out of the box, no flags |
| macOS | Chrome 113+, Firefox, Safari 18+ | Manual, upstream | Loads and renders. Never run against the scene smoketest (it needs AppleScript to drive Safari, and no macOS machine is in this fork's loop) |
| Android | Chrome 154 | Measured, 2026-10-17 (Motorola Razr 60, MediaTek MT6878 / Mali-G615 MC2) | Loads and renders correctly out of the box; the adapter reports 48 sampled textures per stage, so a `forward_plus` project runs **Forward+**. GPU-bound at 1920x1080 (20 fps on a real project; 56-60 fps with 3D resolution scaling) — see [Performance Benchmarks](#performance-benchmarks) and `webgpu_notes/TASKS.md` §2.9. Tooling: `webgpu_tests/perf_guard/android/` |
| iOS | Safari 26.0+ | Not measured in this fork | Reported working upstream; Task 5.2 lists iOS as outstanding |

The smoketest is the only automated per-scene measurement, and it covers 21 scenes (8 benchmarks, 10 demos, 1 stress test, and 2 that exist to be asserted on — font rendering and volumetric fog) drawn from `godot-demo-projects` and this repo's own fixtures — see [`webgpu_tests/scene_smoketest`](webgpu_tests/scene_smoketest). Where a row says *manual*, it means a human loaded exports and looked at them; treat it as "no known problems" rather than a coverage figure. Every scene passes, and nothing is skipped.

### Known Issues

Outstanding problems as of **2026-10-06**. Everything here is reproduced and diagnosed; items marked *workaround* have a known way around them, items marked *open* do not. Task numbers index into [`webgpu_notes/TASKS.md`](`webgpu_notes/TASKS.md`); [`webgpu_notes/TASKS.md`](`webgpu_notes/TASKS.md`) carries the current state and what to pick up next.

**Rendering**

| Issue | Status | Notes |
|-------|--------|-------|
| `command_render_clear_attachments` is a no-op | won't fix | WebGPU has no mid-pass attachment clear. Confirmed dead code on every Godot backend today |
| `draw_indexed_indirect_count` / `draw_indirect_count` ignore the count buffer | won't fix | WebGPU has no multi-draw-indirect-count. No current renderer uses count-buffer indirect draws |
| Explicit `command_resolve_texture()` is a stub | won't fix for now | MSAA goes through render-pass `resolveTarget`, which is implemented. Only out-of-pass resolves are missing |
| No image/texture atomics (`SUPPORTS_IMAGE_ATOMIC_32_BIT` is false) | by design | WGSL has no image-atomics concept. Engine code that wants them must select its `NO_IMAGE_ATOMICS` shader variant *and* leave `TEXTURE_USAGE_STORAGE_ATOMIC_BIT` off the texture, or `texture_create()` hard-fails — see `fog.cpp` and `gi.cpp` for the gating idiom |
| No hardware multiview, VRS, or subgroups; subpass post-processing disabled; MSAA 2x/8x run as 4x; no wireframe, sampler border colour or HDR canvas | by design | WebGPU feature gaps — see [Correctness & Compatibility](webgpu_site/CORRECTNESS_AND_COMPATIBILITY.md) and `webgpu_notes/TASKS.md` §2.31. Texture arrays are expanded per element (multi-lightmap works) and collapse only past a stage's texture budget; omni lights use their own shadow mode (dual-paraboloid was forced until 2026-10-17) |

**Readback and formats**

| Issue | Status | Notes |
|-------|--------|-------|
| `buffer_map()` returns a CPU shadow copy, so readback is a frame behind | open (Task 7.8) | Synchronous GPU readback is impossible on single-threaded WASM. Some paths load from disk instead |
| `RenderingDevice.texture_get_data()` returns nothing on the first call | open (Task 45) | Consequence of the above: the first call starts the copy-and-map and the data lands a frame later, and `rd.sync()` cannot wait for it. Web GDScript that needs a readback must retry or use `texture_get_data_async()`. `GradientTexture1D/2D.get_image()` no longer round-trips through the GPU and works normally |
| 16-bit unorm/snorm texture formats are reported unsupported and converted to 32-bit float | workaround (Task 7.10) | Dawn's `texture-formats-tier1` exposes them but only as unfilterable, which breaks every linear sampler on them (tried and reverted 2026-10-17, TASKS.md §3 trap 31). Costs memory; correctness is fine. Vertex attributes are unaffected |
| Canvas SDF uses `R16_SFLOAT` instead of `R16_SNORM` | workaround (Task 44) | Dawn reports `R16Snorm`'s sample type as `UnfilterableFloat` while the SDF samples it with a filtering sampler |
| A storage image's `layout(<format>, ...)` in GLSL must match the bound texture's `DataFormat` exactly | by design | Vulkan/Metal/D3D12 tolerate a mismatch; WebGPU rejects it at bind time, and one invalid bind group invalidates the whole command buffer — so `Queue.Submit` silently drops the entire frame. A shared-engine-code change that moves one side must move the other; this is what blacked out 3D after the 4.8 merge (Task 15.5) |
| Storage textures need format promotion (`R8`→`R32Float`, `rgb10a2unorm`→`rgba16float` on Firefox); no 3-component formats; no component swizzle; sRGB `viewFormats` excluded for storage textures | by design | CPU-side expansion happens once per texture at load. Promotion is applied to the texture and the WGSL declaration together, so the rule above still holds |
| Float32 textures downgraded to float16 on Adreno | vendor workaround | Precision loss on affected Android GPUs |

**Build and export**

| Issue | Status | Notes |
|-------|--------|-------|
| `threads=yes` with `dlink_enabled=yes` (threads *and* GDExtension support) needs a patched Emscripten toolchain | workaround (Task 12) | Two bugs in Emscripten's own dylink/`EXPORT_ALL` glue, not this fork's code: an `ASM_CONSTS` initialization-order race that stopped it booting at all, and an eager `HEAP*` re-export that threw on every pthread worker. Both are patched by `misc/emsdk_patches/apply.sh --apply`, which every build helper and both web CI workflows run for you and the combination then boots clean. The other three combinations never needed it |
| Exports with `variant/extensions_support` off can abort on load with `Aborted(native code called abort())` | open, upstream (Task 11) | Root-caused to a 4-byte heap-buffer-overflow in the pinned emdawnwebgpu port's `WGPUInstanceImpl` constructor. Not fixable here without vendoring a port patch or a toolchain bump; enabling extension support is the workaround |
| Interleaving editor and web builds in one tree ships a template that dies in `callMain()` | workaround (Task 40); not reproduced on 4.8 (2026-10-15) | Stale `register_module_types.gen` object. Delete it plus `libmodules.a` and rebuild; verify with `grep -ac initialize_betsy_module bin/godot.side.web.*.wasm` (want 0) |
| The editor and the export template must be built from the same commit | by design (Task 36) | Baked shader caches are keyed to the engine version hash. A mismatch silently discards the whole cache (`{baked: 0, translated: N}` and a ~5× slower load); the engine now reports it |
| An export made with `--headless` silently skips the shader baker | workaround | 15 MB `.pck` instead of ~135 MB. Use `xvfb-run` with a real rendering driver |
| `tint_convert_cli` is compiled without `-DNDEBUG` | open (Task 13.1) | SPIRV-Tools/Tint asserts are live in the host tool; they surface as isolated "Tint crashed" bake entries |
| Box3D has never run physics in a browser | open (Task 11.1) | The web link is no longer in doubt — the module compiles for `wasm32` with `-msimd128 -msse2` and links into both shipped templates (symbols present in `godot.side.web.*.wasm`). What is still unverified is an actual in-browser physics run: no smoketest scene exercises 3D physics on the web. `precision=double` (`BOX3D_DOUBLE_PRECISION`) is wired but never built |

**Performance and lifetime**

| Issue | Status | Notes |
|-------|--------|-------|
| A fixed ~500 ms `Servers:Rendering` engine-init cost on every project, ~180 ms of it our own per-stage WGSL text scanning | open (Task 14) | Baking binding metadata into the shader container at export time is the remaining win. Project-dependent load time is already down from ~9.2 s to ~1.0 s |
| Temporary texture views in `WGUniformSet::temp_views` may not be released | open (Task 7.15) | Suspected leak; not yet characterized as per-frame or unbounded |
| Specialized shader modules may not be released by `pipeline_free()` | open (Task 7.17) | Narrowed by Task 25's move to WGSL `@id(N) override`s, which removes most re-specialization |
| WGSL format-name remapping patches strings in place, assuming equal lengths | open (Task 7.18) | Fragile if Tint's output names change. Task 9.15 moved some of these patches to Tint IR transforms and made the rest `memcpy`-safe |
| No device-loss recovery | open | Logged only; the page must be reloaded |

**Audio**

| Issue | Status | Notes |
|-------|--------|-------|
| Bus effects (reverb, EQ, capture, spectrum...) do not apply to Sample playback, the web default | deferred (§2.32) | Sample playback plays sounds through Web Audio, outside `AudioServer`. Set `playback_type` to Stream on players that need effects (native mixer; on its own thread in thread-enabled exports). Warned about at export and once per bus at runtime |
| Sample playback decodes a sound whole on its first `play()` | deferred (§2.32) | A hitch and the whole track in memory for long music; Stream playback streams. Warned about once per stream over 30 s |

Everything else Sample playback does matches native, tested sample for sample (`webgpu_tests/web_audio`, §4.30).

**Verification gaps**

- **A green test suite does not mean Forward+ renders.** In a container the suite runs on a software adapter, which reports fewer than 48 textures per shader stage — and `RendererCompositorRD::initialize()` silently falls back to **Forward Mobile** when it sees that. SSAO and SSIL are Forward+-only, and SDFGI's voxelization lives in the clustered shader, so none of them execute there. The 4.8 port had every tier green while 3D rendered entirely black on real hardware (Task 15.5). Treat a container run as "the engine boots and loads every scene", and use a real-GPU run against a real project for anything renderer-dependent — `webgpu_tests/screenshot_comparison/LIVE_REPRO_METHODOLOGY.md` is the procedure.
- **Pixel assertions are unreliable headless.** Under headless swiftshader nothing presents to a capturable canvas, so every pixel-based check fails identically whatever the engine drew. Headless Firefox additionally cannot initialize a compositor here at all (`RenderCompositorSWGL failed mapping default framebuffer`) and screenshots come back fully black *including the 2D UI*, while the same build headed renders correctly. This is why a headless `local_ci.sh` reports one failure — "Font rendering colors — Firefox", claiming "glyph modulate is being dropped" — which is specific, plausible and wrong. Chrome's equivalent passes, and so does Firefox on a real GPU.
- Safari has never been run against the scene smoketest (macOS only); the 100% figures above come from manual testing.
- Texture compression is settled on desktop (BC, both browsers); Safari and mobile formats are unmeasured (Task 39).
- Box3D is validated natively against Jolt and GodotPhysics3D, but not in a browser; no scene in the smoketest exercises 3D physics under Box3D on the web.

---

## Test Suite

Last full run **2026-10-17**, on Godot 4.8, real GPU (RTX 4080 SUPER, Chrome + Firefox), with
`dev_mode=yes` (werror) as CI builds: **29 passed, 0 failed, 2 skipped** -- the two skips are Safari
(macOS only, and no macOS machine is in this fork's loop). See
[`webgpu_tests/README.md`](webgpu_tests/README.md) for what each tier covers and how to run it.

| Tier | Result | Needs |
|------|--------|-------|
| `shader_corpus` | **14 / 14** | `tint_convert_cli` only — no engine build |
| `driver_unit_tests` | **379 pass, 0 fail** | Node |
| `preprocessing_tests` | **291 pass, 0 fail, 1 skipped** | Node |
| `resource_lifecycle` | all pass | Playwright |
| `screenshot_comparison` | all pass | Playwright |
| Scene smoketest — Chrome | **21 pass, 0 fail, 0 skip**; **22 / 22** with the `audio_parity` scene on 2026-10-18 | Editor + web template build |
| Scene smoketest — Firefox | **21 pass, 0 fail, 0 skip** | Editor + web template build |
| Font rendering colors — Chrome | pass | Editor + web template build |
| Font rendering colors — Firefox | pass on a real GPU (a known false positive headless, see Verification gaps) | Editor + web template build |
| Fog smoothness — Chrome / Firefox | pass on a real GPU; **skips** under a software adapter, which renders no fog | Editor + web template build, real GPU |
| Fog assertion self-test | pass | Node |
| Forward+ feature matrix (71 features) | **71 / 71**; 10 features documented as uncovered | Own export, **real GPU** |
| Forward+ parity vs native Vulkan (71 features) | **71 / 71** within tolerance | Own export + native editor with Vulkan, **real GPU** |
| Forward+ matrix self-test | pass | Node |
| Web audio parity, offline (`web_audio/run_tests.mjs`, 2026-10-18) | **40 / 40**, loop modes sample-identical to native | Playwright, no build |
| Web audio end to end, native vs nothreads and threads exports (2026-10-18) | `[AUDIO-CHECK] PASS` on both, booleans equal to native | Editor + web templates |
| Editor drag-and-drop onto every list (`tests/editor_drop`, 2026-10-18) | **32 / 32** real drags | Editor, Xvfb |
| Scene smoketest — Safari | not run | macOS + AppleScript |
| `./webgpu_tests/local_ci.sh --dev-mode --no-safari` (all of the above plus `spec_constant_overrides`, `wgsl_cache`, `perf_guard` and WebRTC parity) | **29 passed, 0 failed, 2 skipped** | Full rebuild + re-export |

Run headless on a software adapter, "Font rendering colors — Firefox" still fails as described under
[Verification gaps](#known-issues): headless Firefox cannot composite in this environment, so the
canvas reads back black and the test blames glyph modulate. On a real GPU it passes.

The 21 scenes of that run are 8 benchmarks, 10 demos, 1 stress test and 2 assertion fixtures (font rendering and
volumetric fog), drawn from `godot-demo-projects` and this repo's own fixtures; since 2026-10-18 a 22nd,
`audio_parity`, runs the self-checking audio suite of `webgpu_tests/web_audio` (clicked once to unlock Web Audio). `local_ci.sh` rebuilds the editor, the dlink
template (compile check only) and the non-dlink nothreads template, then re-exports every scene from
them before testing — so a green run reflects the working tree rather than whatever exports happened
to be on disk.

**Every Forward+ feature has a regression test, and new ones must get one.**
[`webgpu_tests/forward_plus`](webgpu_tests/forward_plus) renders each of 71
Forward+ features with it off and on and asserts the frame changed — the failure
this port keeps producing is a feature silently doing nothing, which logs
nothing and looks correct in isolation. Any feature added to the renderer,
including code merged from upstream, must get an entry there or a documented
reason in its `UNCOVERED` list; the suite fails and names the offender if the
fixture and the feature table disagree. It needs a real GPU and skips rather
than passing without one, and its thresholds are calibrated against native
Vulkan rather than against WebGPU.

**"The frame changed" is not "the frame is right"**, so `--parity` also renders
the same fixture natively on Vulkan and fails any feature whose WebGPU frame
differs from native by more than 0.05 mean per channel. That check found five
features that had passed the delta test for weeks while rendering wrongly --
SDFGI with a moving camera, volumetric fog's shadows, omni cube shadows,
`DEPTH_TEXTURE` and the mipmap bias -- all fixed on 2026-10-17
(`webgpu_notes/TASKS.md` §4.29). It is a `local_ci.sh` stage.

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

**Measured 2026-10-15 on this fork** (RTX 4080 SUPER, 165 Hz display, Chrome, `webgpu_tests/benchmark/run_matrix.sh`; fps, 300 frames after warm-up). Web runs are display-capped, so the renderers only separate where a scene is heavy enough:

| Scene | Native Vulkan | WebGL | WebGPU Mobile | WebGPU Forward+ |
|---|---:|---:|---:|---:|
| Sprites | 165 | 164 | 165 | 165 |
| PBR Sphere | 165 | 161 | 165 | 165 |
| Cubes + Lights | 2520 | 160 | 165 | 165 |
| GPU Particles | 165 | 165 | 165 | 165 |
| Skeletal Animation | 2809 | 154 | 165 | 165 |
| Other Effects (SSAO + Bloom) | 164 | 165 | 165 | 165 |
| Shadows | 2701 | 165 | 165 | 165 |

**Uncapped** (`BENCH_UNCAPPED=1`: Chrome's frame-rate limit and vsync lifted, native `--disable-vsync`; fps, 6 s warm-up then 4 s measured):

| Scene | Native Vulkan | WebGL* | WebGPU Mobile | WebGPU Forward+ |
|---|---:|---:|---:|---:|
| Sprites | 1962 | 165 | 1043 | 1031 |
| PBR Sphere | 3350 | 165 | 996 | 901 |
| Cubes + Lights | 2873 | 160 | 849 | 825 |
| GPU Particles | 3664 | 160 | 1008 | 981 |
| Skeletal Animation | 2704 | 165 | 946 | 869 |
| Other Effects (SSAO + Bloom) | 866 | 161 | 583 | 414 |
| Shadows | 3086 | 160 | 883 | 851 |

\* **WebGL stays at ~165 fps even with the uncapping flags** (the browser's GL present path is not lifted by them), so the WebGL column is *not* an uncapped measurement and no WebGPU-vs-WebGL ratio can be read from this table. WebGPU runs at roughly 25-50% of native Vulkan on these scenes. Forward+ is within about 10% of Forward Mobile except on post-processing (SSAO + bloom, ~30% slower); see `webgpu_notes/TASKS.md` §2.17.

On this hardware WebGPU matches WebGL (all display-capped). Post-processing was 96 / 91 fps (Mobile / Forward+) until two driver fixes the same day (one encoder + submit per render pass, and whole-slice uploads of the instance buffer) brought it to the cap; see `webgpu_notes/TASKS.md` §2.17. The table below is inherited from the upstream project (Mac Studio M3 Ultra, lighter GPU headroom) and has not been reproduced here.

**One day of optimization, 2026-10-16** (same RTX 4080 SUPER / 165 Hz / Chrome; before = the tree at the end of 2026-10-15, after = `db437ca832`). What landed: a SPIR-V pass that hoists SSIL's constant local arrays (its GPU pass 0.6 → <0.05 ms), read-write storage textures bound as such (heavy scene GPU 2.6 → 1.5 ms), an upstream Godot shadow-atlas bug that redrew every positional shadow every frame (heavy scene CPU −37%), no-op uniform-set prepares skipped on WebGPU, Web Audio writes skipped when unchanged (platformer audio 0.148 → 0.029 ms/frame), and a **separate render thread**, on by default for thread-enabled exports (export option `variant/separate_render_thread`; rendering runs on its own pthread with its own WebGPU device and the canvas as an OffscreenCanvas; `webgpu_notes/TASKS.md` §2.5). *Main* is the main thread's ms per frame at the 165 fps cap (`Performance.getMetrics`), *render* the render thread's; *uncapped* is engine fps with Chrome's frame-rate limit and vsync lifted; *GPU* is GPU busy ms/frame. Run-to-run noise is about ±5%.

| Scene | Main, before → after (nothreads) | → after, Separate (main + render) | Uncapped fps, before → after | GPU ms, before → after |
|---|---:|---:|---:|---:|
| cameraSim (real project) | 0.51 → 0.49 ms (−4%) | **0.09** + 0.44 ms (**−82%**) | 837 → 832 (−1%) | 0.64 → 0.58 (−9%) |
| Heavy Forward+ (scene_i) | 1.93 → 1.15 ms (**−40%**) | **0.06** + 1.18 ms (**−97%**) | 229 → 292 (**+28%**) | 3.40 → 1.46 (**−57%**) |
| 3D platformer demo | 0.84 → 0.79 ms (−6%) | **0.36** + 0.50 ms (**−57%**) | 833 → 821 (−1%) | 0.40 → 0.42 (+5%) |
| PBR Sphere | 0.28 → 0.27 ms (−4%) | 0.05 + 0.22 ms (−82%) | 1012 → 1022 (+1%) | 0.28 → 0.28 (0%) |
| GPU Particles | 0.26 → 0.27 ms (+4%) | 0.05 + 0.19 ms (−81%) | 982 → 974 (−1%) | 0.13 → 0.14 (+8%) |
| Other Effects (SSAO + Bloom) | 0.96 → 0.99 ms (+3%) | 0.09 + 0.99 ms (−91%) | 633 → 619 (−2%) | 0.50 → 0.49 (−2%) |
| Shadows | 0.44 → 0.40 ms (−9%) | 0.06 + 0.36 ms (−86%) | 846 → 867 (+2%) | 0.34 → 0.34 (0%) |

Totals, averaged over the seven scenes: the driver/engine fixes alone cut main-thread time **−8%** and GPU time **−8%** and raised uncapped fps **+4%** — all of it on the scenes that had something to fix (the heavy scene: main −40%, GPU −57%, +28% fps; cameraSim: GPU −9%), the rest were already at their floor and moved within noise. With the separate render thread on top, the main thread's per-frame cost falls **−82% on average** (−57% on the platformer, whose remaining 0.36 ms is its own physics, scripts and audio; −97% on the heavy scene), the render thread picking up the same work in parallel. Uncapped fps is not measurable in Separate mode (Chrome's uncapped mode drives the rAF of a page whose canvas is worker-owned at ~60 Hz); with vsync every configuration runs at the display rate, and the threads+GDExtension (dlink) build measures the same as threads. The Safe thread model measures the same as nothreads, which is also why there is no "before, threaded" column: until today the thread model did not change where rendering ran.

**First phone, 2026-10-17** — a Motorola Razr 60 (MediaTek MT6878, 2-core Mali-G615 MC2, Android 16, Chrome 154), over USB with the tools in `webgpu_tests/perf_guard/android/`. The cameraSim project (Forward+, depth of field, a directional light and three shadow-casting spots, seven opaque draws) at its 1920x1080 backbuffer runs at **20.5 fps and is entirely GPU-bound**: 48.7 ms of GPU time per frame (90% of the GPU timeline) with the main thread 29% busy — 28 ms in the opaque lighting pass and 14.5 ms in full-resolution bokeh DOF. Nothing on the CPU side matters there, and the frame scales with 3D pixel count; a bisection of the lighting shader (edited WGSL substituted on the device) found BRDF + 8-tap PCF for four lights per pixel, with an f16 build of the Forward+ shader a null result on this GPU. Forward Mobile is no faster (21.6 fps). Release-template exports, main-thread rendering, one tab, 10 s windows after a cold boot, all quantised to the 120 Hz panel:

| Variant (Forward+ unless noted) | fps | p50 / p90 frame ms |
|---|---:|---:|
| as exported (also: debug template, no dlink, nothreads, ASTC texture) | 20.5 (19.2–20.8) | 41.5 / 83 |
| 2048 shadow maps + soft filter quality 1 | 22.6 | 41.5 / 41.5 |
| bokeh Very Low + Box | 21.4 | 41.5 / 83 |
| `scaling_3d/scale` 0.75 | 30.1 | 33.2 / 33.2 |
| `scaling_3d/scale` 0.66 + cheap DOF + cheap shadows | 50.1 | 16.6 / 33.2 |
| the same with FSR 1.0 upscaling | 30.1 | 33.2 / 33.2 |
| `scaling_3d/scale` 0.5 | 59.6 | 16.6 / 16.6 |
| the project's `.web_android` overrides (scale 0.6, bilinear, cheap shadows, cheap DOF) | **55.6** | 16.6 / 16.6 |
| Forward Mobile (diagnostic) | 21.6 | — |

What to set for phones, without touching desktop: suffix the settings with `.web_android` (and `.web_ios`) — the web platform exposes those feature tags, never `mobile` — e.g. `rendering/scaling_3d/scale.web_android=0.6`, `scaling_3d/mode.web_android=0` (bilinear; FSR 1.0 costs ~7 ms at 1080p output on this GPU), `lights_and_shadows/directional_shadow/size.web_android=2048`, `positional_shadow/atlas_size.web_android=2048`, both `soft_shadow_filter_quality.web_android=1`.

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

### [Download the GodotWebGPU Editor](https://github.com/Shane-Gadsby/godotwebgpu/releases)

Pre-built editors for Linux, macOS (universal: Apple Silicon and Intel) and Windows, with WebGPU export
support, plus the export templates as one `.tpz` (Editor → Manage Export Templates → Install from File).

- **macOS:** the editor is ad-hoc signed, not notarized, so macOS blocks its first launch. Open it once,
  then allow it in System Settings → Privacy & Security → *Open Anyway* (macOS 15 and later no longer offer
  right-click → Open for this). Or clear the download quarantine from a terminal:
  `xattr -dr com.apple.quarantine /path/to/godot_macos_editor.app`.
- **Linux:** unzip and run `godot.linuxbsd.editor.x86_64`. Keep `tint_convert_cli` next to it: the export-time
  shader baker runs it from the editor's own directory.
- **Windows:** unzip and run the `.exe`, keeping `tint_convert_cli.exe` beside it for the same reason.

Releases up to and including v1.5 shipped the Linux and macOS editors without their executable bit; macOS
refused to open those at all. On Linux, `chmod +x godot.linuxbsd.editor.x86_64 tint_convert_cli` fixes one.

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
