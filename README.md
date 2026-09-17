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
    <td align="center"><strong>20K+</strong><br><sub>Lines Written</sub></td>
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

---

## Browser Compatibility

| Platform | Browser | Status                                                                                                                |
|----------|---------|-----------------------------------------------------------------------------------------------------------------------|
| macOS | Chrome 113+ | 100%                                                                                                                  |
| macOS | Safari 18+ | 100%                                                                                                                  |
| macOS | Firefox | 100%                                                                                                                  |
| Android | Chrome | 100%                                                                                                                  |
| iOS | Safari | 100% (Safari 26.0+)
| Windows | Chrome, Firefox, Edge | 100% (works out of the box)                                                                                           |
| Linux | Chrome, Firefox, Vivaldi | 100% (requires enabling Vulkan + WebGPU flags; native package install only — not compatible with Flatpak/Snap builds) |

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

```bash
# WebGPU-only release template:
scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no
```

Requirements:
- Emscripten 4.0.10+ (for the emdawnwebgpu port)
- No Rust toolchain needed (Tint C++ translator is compiled directly into the engine)
- Standard Godot build dependencies (SCons, Python, C++ compiler)

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
| Total new code | ~20,000+ lines |
| Driver implementation | 7,733 lines (single `.cpp`) |
| Shaders converted | 146 (SPIR-V → WGSL via Tint) |
| Renderer | Forward+ (this fork) / Forward Mobile (original) |
| Performance vs native | ~80% of Vulkan/Metal FPS |
| Performance vs WebGL | Up to 5x faster |
| Browser support | Chrome 113+, Firefox 120+, Safari 18+, Chrome Android, Safari iOS |

---

<p align="center">
  Sponsored by <a href="https://shinygen.ai">Shiny Gen AI</a>
</p>

<p align="center">
  <sub>Godot WebGPU, Shiny Gen AI, and David Walter are not directly affiliated with or endorsed by the Godot Foundation. It uses the GODOT&reg; name and logos under a permissive license granted by the Godot Foundation. Godot WebGPU is a free and open source fork of the Godot Engine. Shiny Gen is a game maker app built with Godot. And David Walter is the developer of Shiny Gen and Godot WebGPU.</sub>
</p>
