# Godot WebGPU

<p align="center">
  <a href="https://github.com/Shane-Gadsby/godotwebgpu">
    <img src="logo_outlined.svg" width="400" alt="Godot Engine logo">
  </a>
</p>

<p align="center">
  <a href="https://github.com/Shane-Gadsby/godotwebgpu/releases/latest"><img src="https://img.shields.io/github/v/release/Shane-Gadsby/godotwebgpu?style=flat-square&label=latest%20release&color=478cbf" alt="Latest release"></a>
  <img src="https://img.shields.io/badge/Godot-4.8--dev-478cbf?style=flat-square" alt="Godot 4.8-dev">
  <img src="https://img.shields.io/badge/renderer-Forward%2B%20on%20WebGPU-478cbf?style=flat-square" alt="Forward+ on WebGPU">
  <img src="https://img.shields.io/badge/license-MIT-478cbf?style=flat-square" alt="MIT license">
</p>

## Godot's Forward+ renderer in the browser

**Godot WebGPU is a fork of [Godot Engine](https://godotengine.org) that exports 3D games to the web with the full
Forward+ (Clustered) renderer, running on WebGPU instead of WebGL.** Stock Godot can only use its Compatibility
renderer on the web. This fork adds a WebGPU rendering driver, so a web export gets the same renderer as a desktop
build: SDFGI, VoxelGI, volumetric fog, SSR, SSAO, SSIL, TAA, FSR 2, GPU particles and compute shaders. Every feature in
the Forward+ test matrix renders within a fixed tolerance of native Vulkan.

The fork also carries engine and editor additions that are not about the web. These include Box3D as the default 3D
physics engine, braced GDScript (`.gdb`), editor default folders, multi-item drag and drop onto lists, and a MetaHuman
importer.

It started as [dwalter/godotwebgpu](https://github.com/dwalter/godotwebgpu), which brought the WebGPU driver up on Godot
4.6.2 with the Mobile renderer. This fork moved it to Godot 4.8 and Forward+, brought the web up to parity with native,
and replaced the shader converter.

> Looking for the upstream Godot README? See [GODOT_README.md](GODOT_README.md).

## Getting the engine

### Binary downloads

The editors (Linux, macOS, Windows) and the export templates for every platform are on the
**[latest release](https://github.com/Shane-Gadsby/godotwebgpu/releases/latest)**. Every release is listed on the
[releases page](https://github.com/Shane-Gadsby/godotwebgpu/releases). The release also carries the braced GDScript
plugins for JetBrains Rider and VS Code.

Install the `.tpz` from the editor (**Editor → Manage Export Templates → Install from File**). Then export with a
**Web** preset as usual. A project using Forward+ is exported with Forward+.

### Compiling from source

See [Building from source](#building-from-source) below.

## How it works

The fork implements Godot's `RenderingDeviceDriver`, the same interface as the Vulkan, Metal and D3D12 backends, on
WebGPU (`drivers/webgpu/`). The renderers above it run unchanged, so Forward+ and Mobile work on the web without a
separate code path.

| Part | Uses |
| --- | --- |
| Rendering API | WebGPU, through Emscripten's [emdawnwebgpu](https://dawn.googlesource.com/dawn) port (Dawn's `webgpu.h` bound to the browser's `navigator.gpu`) |
| Toolchain | Emscripten 6.0.9 (4.0.10 minimum), with two local patches for GDExtension builds (`misc/emsdk_patches/`) |
| Shader conversion | glslang compiles the engine's GLSL to SPIR-V; 26 SPIR-V passes adapt it to WebGPU's rules; [SPIRV-Cross's WGSL backend](https://github.com/Ludicon/spirv-cross-wgsl) writes WGSL |
| Shader delivery | Engine shaders are converted at build time, and a project's shaders at export time (`<name>.shaders.pck`). Anything left over is converted at run time. |
| Engine base | Godot 4.8 (dev), merged from upstream |

```
GLSL ──glslang──> SPIR-V ──26 passes──> SPIR-V ──SPIRV-Cross──> WGSL ──> browser (Dawn in Chrome, wgpu in Firefox)
```

WebGPU lacks some things Vulkan has. The driver emulates them:

- **Push constants:** emulated with a ring buffer bound at group 3.
- **Subpasses:** each one becomes its own render pass.
- **Specialization constants:** become WGSL `override` constants.
- **Texture arrays:** expanded to one binding per element.
- **Sampler LOD bias:** applied in the shader.
- **Point size:** points are drawn as quads.
- **Buffer mapping:** returns a CPU copy, so GPU readbacks arrive one frame late.

Threaded exports render on their own thread, with a separate WebGPU device and an `OffscreenCanvas`, so the main thread
only runs the game.

Technical documentation: [Architecture and design](webgpu_site/ARCHITECTURE_AND_DESIGN.md),
[Technical reference](webgpu_site/TECHNICAL_REFERENCE.md),
[Performance](webgpu_site/PERFORMANCE_AND_OPTIMIZATION.md),
[Correctness and compatibility](webgpu_site/CORRECTNESS_AND_COMPATIBILITY.md), [FAQ](webgpu_site/FAQ.md).

## Browser support

| Browser | Status |
| --- | --- |
| Chrome / Edge, desktop | Supported and tested on every change (Linux, RTX 4080 SUPER). On Linux, start Chrome with `--enable-unsafe-webgpu --enable-features=Vulkan`. Windows was checked by hand in September 2026. |
| Firefox, desktop | Supported. The Linux build is tested; it needs `dom.webgpu.enabled` and `gfx.webgpu.ignore-blocklist` in `about:config`. 69 of 71 Forward+ features match native; SDFGI differs on Firefox only. |
| Chrome, Android | Supported and tested (Motorola Razr 60, Mali-G615). Forward+ runs; heavy scenes are GPU-bound. |
| Safari, macOS and iOS | Untested. No Mac is available to this project. |
| Firefox, Android | Not supported. |

A browser without WebGPU cannot run these exports. The web templates are built without the WebGL renderer.

## Compatibility: native, WebGL and WebGPU

"WebGL 2" is stock Godot's web export (Compatibility renderer). "WebGPU" is this fork's web export.

| | Native (Vulkan) | WebGL 2 (stock Godot) | WebGPU (this fork) |
| --- | :---: | :---: | :---: |
| Forward+ renderer | ✔ | ✘ | ✔ |
| Mobile renderer | ✔ | ✘ | ✔ |
| Compatibility renderer | ✔ | ✔ | ✘ |
| Compute shaders, `RenderingDevice` | ✔ | ✘ | ✔ |
| Shaders converted before the game runs | ✔ (pipeline cache) | ✘ (compiled at load) | ✔ (WGSL baked at export) |
| Threads | ✔ | ✔ (needs COOP/COEP) | ✔ (needs COOP/COEP) |
| Rendering on its own thread | ✔ | ✘ | ✔ |
| GDExtension | ✔ | ✔ | ✔ |
| VRAM texture compression | ✔ | ✔ | ✔ (BC and ETC2/ASTC in separate packs, one downloaded) |
| Synchronous GPU readback | ✔ | ✔ | ✘ (one frame late) |
| MSAA | 2x, 4x, 8x | 2x, 4x (3D) | 4x (2x and 8x use 4x) |
| HDR output | ✔ | ✘ | ✘ |
| XR | ✔ | ✔ (WebXR) | ✘ |
| Variable rate shading | ✔ | ✘ | ✘ |
| Recovers from a lost GPU device | ✔ | ✘ | ✘ (reload the page) |

## Rendering features

The Forward+ test matrix renders each feature on WebGPU and on native Vulkan from the same fixture, then compares the
frames. ✔ in the WebGPU column means the frames match within the matrix's tolerance (mean absolute difference 0.05 out
of 255, 0.08 for SDFGI, 0.3 for FSR 2). The latest run, on 25 October 2026, passed 71 of 71 in Chrome. The WebGL 2
column is the Compatibility renderer, from Godot's
[renderer comparison](https://docs.godotengine.org/en/latest/tutorials/rendering/renderers.html).

| Feature | Native (Vulkan) | WebGL 2 (stock Godot) | WebGPU (this fork) |
| --- | :---: | :---: | :---: |
| **Lights and shadows** | | | |
| Directional, omni and spot lights, negative lights, specular | ✔ | ✔ | ✔ |
| Directional shadows, CSM splits, shadow blur | ✔ | ✔ | ✔ |
| Omni shadows (cube and dual paraboloid), spot shadows | ✔ | ✔ | ✔ |
| Soft shadows (PCSS) | ✔ | ✘ | ✔ |
| Light projector textures | ✔ | ✘ | ✔ |
| Screen-space contact shadows | ✔ | ✘ | ✔ |
| **Global illumination** | | | |
| SDFGI, including a moving camera | ✔ | ✘ | ✔ (Chrome; differs in Firefox) |
| VoxelGI | ✔ | ✘ | ✔ |
| SSIL | ✔ | ✘ | ✔ |
| Reflection probes | ✔ | ✔ (2 per mesh) | ✔ |
| Decals | ✔ | ✔ | ✔ |
| LightmapGI | ✔ | ◐ (rendering only) | ◐ (renders; not in the matrix) |
| **Environment** | | | |
| Ambient and sky light, procedural sky | ✔ | ✔ | ✔ |
| Tonemapping, exposure, adjustments, color correction LUT | ✔ | ✔ | ✔ |
| Depth fog, height fog, aerial perspective | ✔ | ✔ | ✔ |
| Volumetric fog | ✔ | ✘ | ✔ |
| Glow, glow map | ✔ | ✔ | ✔ |
| SSAO | ✔ | ✔ | ✔ |
| SSR | ✔ | ✘ | ✔ |
| **Post-processing, antialiasing and scaling** | | | |
| Depth of field (near and far) | ✔ | ✘ | ✔ |
| Auto exposure | ✔ | ✘ | ✔ |
| TAA | ✔ | ✘ | ✔ |
| FSR 2.2 | ✔ | ✘ | ✔ |
| FXAA | ✔ | ✘ | ✔ |
| Debanding | ✔ | ✘ | ✔ |
| 3D resolution scaling | ✔ | ✔ | ✔ |
| MSAA | ✔ | ◐ | ◐ (4x) |
| SMAA | ✔ | ✘ | ◐ (not in the matrix) |
| **Materials** | | | |
| Normal maps, emission, rim, clearcoat, anisotropy, backlight | ✔ | ✔ | ✔ |
| Height maps (parallax), triplanar mapping | ✔ | ✔ | ✔ |
| Alpha blend, scissor and hash, proximity and distance fade | ✔ | ✔ | ✔ |
| Refraction, `DEPTH_TEXTURE` and screen textures | ✔ | ✔ | ✔ |
| Subsurface scattering | ✔ | ✘ | ✔ |
| Texture arrays indexed at run time | ✔ | ✔ | ✔ |
| **Meshes and particles** | | | |
| MultiMesh, blend shapes, mesh LOD, visibility ranges | ✔ | ✔ | ✔ |
| GPU particles, collision, attractors | ✔ | ✔ | ✔ |
| Particle trails | ✔ | ✘ | ◐ (not in the matrix) |
| Compositor effects | ✔ | ✘ | ◐ (compute works; not in the matrix) |
| Wireframe debug draw | ✔ | ✘ | ✘ (draws filled) |

Also not covered by the matrix: skeletal animation, occlusion culling, FogVolume nodes, stencil modes and bent normals.
None of these is known to be broken.

## Performance

<!-- perf-table:begin -->
Measured on 15 October 2026 on Linux (RTX 4080 SUPER, 165 Hz display, Chrome). Each scene ran with the frame-rate limit
and vsync lifted. The fps is averaged over at least 300 frames after a 6 s warm-up.

| Scene | Native (Vulkan) | WebGL 2 (stock Godot) | WebGPU, Mobile | WebGPU, Forward+ |
| --- | ---: | ---: | ---: | ---: |
| Sprites | 1962 | 165 * | 1043 | 1031 |
| PBR sphere | 3350 | 165 * | 996 | 901 |
| Cubes and lights | 2873 | 160 * | 849 | 825 |
| GPU particles | 3664 | 160 * | 1008 | 981 |
| Skeletal animation | 2704 | 165 * | 946 | 869 |
| SSAO and bloom | 866 | 161 * | 583 | 414 |
| Shadows | 3086 | 160 * | 883 | 851 |

\* Chrome keeps WebGL at the display's refresh rate even with the limit lifted, so these figures are a floor, not WebGL's
real speed.
<!-- perf-table:end -->

On these light scenes WebGPU reaches 25–50% of native fps. Most of the gap is wasm and the JavaScript calls into
WebGPU, not GPU time. Moving rendering to its own thread took 82% of that work off the main thread. GPU time on a heavy
Forward+ scene is 1.43 ms per frame in Chrome and 3.82 ms in Firefox.

On a mid-range phone (Razr 60, Mali-G615), a full Forward+ scene with DOF and soft shadows runs at about 20 fps at
1080p. Rendering at a lower internal resolution, with the project's own Android settings, brings it to 55 fps.

Full numbers and methods: [Performance and optimization](webgpu_site/PERFORMANCE_AND_OPTIMIZATION.md).

## Web audio

Web exports play sounds through Web Audio by default (**Sample** playback): the engine renders each sound once and the
browser plays it. Sample playback bypasses Godot's mixer, so the fork reimplements the mixer's behavior in the web
driver. Tests compare it sample by sample with ports of the native code, and end to end with a native run.

| Feature | Native | Web, Stream playback | Web, Sample playback (default) |
| --- | :---: | :---: | :---: |
| 5.1 and 7.1 volume vectors | ✔ | ✔ | ✔ |
| Loop modes (forward, ping-pong, backward) | ✔ | ✔ | ✔ |
| Volume ramps, fades on pause, resume and stop | ✔ | ✔ | ✔ |
| Bus routing (solo, mute, sends, moving buses) | ✔ | ✔ | ✔ |
| 3D attenuation and the high-shelf filter | ✔ | ✔ | ✔ |
| Pitch scale, including polyphonic streams | ✔ | ✔ | ✔ |
| Playback position after loops, seeks and pauses | ✔ | ✔ | ✔ |
| Bus effects (reverb, EQ, compressor and others) | ✔ | ✔ | ✘ (warns) |
| Capture and spectrum analysis | ✔ | ✔ | ✘ |
| Streaming long files | ✔ | ✔ | ✘ (decoded whole; warns over 30 s) |
| Generators, microphone, script streams | ✔ | ✔ | Uses Stream |

A player that needs bus effects or streaming can set `playback_type` to Stream. The fork also fixes several upstream
audio bugs found along the way, such as mono microphone input playing at double speed and stream output truncated to
16 bits.

## Engine additions

These work in every export, not only on the web. Full documentation is on the
**[Engine additions wiki page](https://github.com/Shane-Gadsby/godotwebgpu/wiki/Engine-Additions)**.

- **Editor default folders:** new scripts, scenes, materials, shaders and resources are saved to folders you choose
  per project (`editor/default_folders/*`). The create and save dialogs offer to make a folder the default.
- **Drag and drop onto lists:** drop several files, folders, resources or nodes at once onto arrays, dictionaries,
  property arrays, the Autoload and Localization lists, audio bus effects and animation libraries. Each item is
  converted to what the slot holds (a resource, a node path, a file path or `uid://`).
- **Script editor input:** editor shortcuts can be bound to mouse buttons, and the modifier for adding carets is a
  setting (Alt or Ctrl/Cmd).
- **Shader baking for the web:** shaders are converted to WGSL at export time, in parallel and cached across exports.
  They ship as a separate `<name>.shaders.pck`. Shader groups the project never uses are left out of the first download.
- **Texture packs per GPU family:** with BC and ETC2/ASTC both exported, each family gets its own pack and the browser
  downloads only the one its GPU samples.
- **Threaded renderer on the web:** threaded exports render on their own thread with an `OffscreenCanvas`
  (`variant/separate_render_thread`).
- **Web export options:**
  - `html/orientation` locks portrait or landscape.
  - `html/fullscreen_on_first_tap` switches phones to fullscreen on the first tap.
  - The project's `keep_screen_on` setting holds a screen wake lock.
  - Every adapter request asks for the high-performance GPU.
  - The loading screen text can be set for each loading stage.
- **Warnings reach the console as warnings:** `WARN_PRINT` goes to `console.warn` instead of being reported as an
  error.
- **MetaHuman importer** (experimental): importing `head.dna` brings in the whole character. The face rig is evaluated
  by OpenRigLogic, and clothing and hair grooms come with it.

## Box3D physics

[Box3D](https://github.com/erincatto/box3d), Erin Catto's 3D successor to Box2D, is the default 3D physics engine. It is
a built-in module (`modules/box3d_physics/`) implementing Godot's `PhysicsServer3D`, so `RigidBody3D`,
`CharacterBody3D`, `Area3D`, joints and space queries use it with no changes to a project. New projects are created
with `"Box3D Physics"`. Jolt and GodotPhysics3D remain available in **Project Settings → Physics → 3D**.

Box3D has no cylinder, soft body or generic 6DOF joint. The module approximates cylinders with prisms and world
boundaries with large boxes. For anything it cannot represent it prints a one-time warning naming the feature, rather
than failing silently. Web builds run Box3D single-threaded.

Documentation: **[Box3D wiki page](https://github.com/Shane-Gadsby/godotwebgpu/wiki/Box3D-Physics)**.

## Braced GDScript

Braced GDScript is GDScript written with `{ }` blocks instead of indentation, in `.gdb` files. It is the same language
on the same runtime, with a few extra spellings: `//` and `/* */` comments, `else if`, `c ? a : b` and `(x) => x * 2`
lambdas. `.gd` and `.gdb` scripts can be mixed freely in one project.

```gdscript
extends CharacterBody2D

const SPEED := 300.0

/** Moves toward the mouse. Returns the distance left. */
func chase(delta: float) -> float {
    var to_mouse := get_global_mouse_position() - global_position
    if to_mouse.length() < 4.0 {
        return 0.0
    } else if Input.is_action_pressed("walk") {
        velocity = to_mouse.normalized() * SPEED * 0.5
    } else {
        velocity = to_mouse.normalized() * SPEED
    }
    move_and_slide()
    return to_mouse.length()
}
```

```gdscript
var names := enemies.filter((e) => e.health > 0).map((e) => e.name)
var label := score > best ? "New record" : "Score"
```

Convert scripts either way from the FileSystem dock, or from the command line with
`godot --headless --gdscript-convert to-gdb --replace res_folder/`. Format with **Edit → Reformat Code** (Ctrl+Alt+L) or
`godot --headless --gdscript-format path/`. Plugins for
[JetBrains Rider](https://github.com/Shane-Gadsby/godot-gdb-rider) and
[VS Code](https://github.com/Shane-Gadsby/godot-gdb-vscode), plus a
[tree-sitter grammar](https://github.com/Shane-Gadsby/godot-gdb-tree-sitter) for Neovim, Helix and Zed, add
highlighting and formatting.

Documentation: **[Braced GDScript wiki page](https://github.com/Shane-Gadsby/godotwebgpu/wiki/Braced-GDScript)**.

## Building from source

```bash
# Native editor (also the tool that exports web builds):
scons platform=linuxbsd target=editor -j$(nproc)

# WebGPU web export template (needs Emscripten 6.0.9):
source ~/emsdk/emsdk_env.sh
scons platform=web target=template_release webgpu=yes opengl3=no threads=no -j$(nproc)

# wgsl_convert_cli, the shader converter the editor runs at export time (keep it next to the editor binary):
./drivers/webgpu/wgsl_cli/build.sh
```

`./build-linux.sh` builds everything in one go: the editor, the Linux templates, all eight web template variants and
the editor plugins. It applies the Emscripten patches GDExtension builds need. macOS and Windows have `build-macos.sh`
and `build-windows.ps1`.

The general build instructions are in
[Godot's documentation](https://docs.godotengine.org/en/latest/engine_details/development/compiling/index.html).

## Testing

The web-specific test tiers live in [`webgpu_tests/`](webgpu_tests/README.md):

- shader conversion and the SPIR-V passes;
- driver unit tests;
- a scene smoke test in Chrome and Firefox;
- the Forward+ parity matrix against native Vulkan;
- WebGPU call budgets per frame;
- web audio parity and WebRTC parity.

`./webgpu_tests/local_ci.sh` runs them as CI does.

## Documentation

| | |
| --- | --- |
| [Wiki home](https://github.com/Shane-Gadsby/godotwebgpu/wiki) | Start here |
| [Braced GDScript](https://github.com/Shane-Gadsby/godotwebgpu/wiki/Braced-GDScript) | Syntax, formatting, conversion, editor plugins |
| [Box3D physics](https://github.com/Shane-Gadsby/godotwebgpu/wiki/Box3D-Physics) | Settings, supported features, differences from Jolt |
| [Engine additions](https://github.com/Shane-Gadsby/godotwebgpu/wiki/Engine-Additions) | Editor default folders, drag and drop, web export options, shader baking |
| [WebGPU technical docs](webgpu_site/ARCHITECTURE_AND_DESIGN.md) | How the driver works |
| [Godot documentation](https://docs.godotengine.org) | Everything else, which is unchanged from Godot |

## Credits and license

The WebGPU driver was started by [David Walter](https://github.com/dwalter) in
[dwalter/godotwebgpu](https://github.com/dwalter/godotwebgpu). Shader conversion uses
[Ludicon/spirv-cross-wgsl](https://github.com/Ludicon/spirv-cross-wgsl), and physics uses
[Box3D](https://github.com/erincatto/box3d). Everything else is [Godot Engine](https://godotengine.org) and its
contributors.

Godot WebGPU is released under the [MIT license](LICENSE.txt), like Godot. Third-party licenses are listed in
[COPYRIGHT.txt](COPYRIGHT.txt).
