# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

This is [Godot WebGPU](https://github.com/dwalter/godotwebgpu) — a fork of Godot Engine that adds a WebGPU rendering driver (`drivers/webgpu/`) as a browser export target, implementing the same `RenderingDeviceDriver` / `RenderingContextDriver` / `RenderingShaderContainerFormat` interfaces as the built-in Vulkan, Metal, and D3D12 backends. It targets the Forward Mobile renderer via Emscripten + the `emdawnwebgpu` port (Dawn). See `README.md` for the project pitch/demos and `GODOT_README.md` for the original upstream Godot README.

`origin` is `dwalter/godotwebgpu` (this fork); `upstream` is `godotengine/godot`. Base branch for this fork's own work is `webgpu-4.7.2`; `sync/*` branches track merges of newer upstream Godot releases (see `webgpu_notes/TASKS.md` Phase 8 for the current sync status).

## Build Commands

Two build targets matter here: the **native editor** (fast, no Emscripten needed — use this to validate that changes to shared engine code compile, since it exercises everything except the actual WebGPU driver) and the **web WebGPU template** (the real target, requires Emscripten).

```bash
# Native editor — fast iteration, validates shared RenderingDevice/servers code:
scons platform=linuxbsd target=editor dev_build=yes -j$(nproc)   # Linux
scons platform=macos target=editor dev_build=yes -j$(nproc)      # macOS

# Web WebGPU export template — the real target, requires Emscripten 4.0.10+ with emdawnwebgpu:
source ~/emsdk/emsdk_env.sh
scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no -j$(nproc)
```

`webgpu=yes` is a SConstruct option (`SConstruct:201`) that only takes effect combined with `platform=web` — `drivers/webgpu/` isn't compiled into non-web builds at all, so a native build never touches WebGPU driver code.

`bin/tint_convert_cli` — the standalone host tool that does build-time SPIR-V→WGSL precompilation — is a **separate native build**, independent of Emscripten and of the two builds above:

```bash
./drivers/webgpu/tint_cli/build.sh           # incremental
./drivers/webgpu/tint_cli/build.sh --clean   # full rebuild
```

Rebuild it directly (rather than through scons) when iterating on `drivers/webgpu/spirv_preprocess.cpp`, `tint_wrapper.cpp`, or anything under `thirdparty/tint`/`thirdparty/spirv-tools` — it's much faster than a full web build and is what `webgpu_tests/shader_corpus` and `drivers/webgpu/wgsl_precompile.py` (the build-time precompiler, invoked automatically by `scons ... webgpu=yes`) both drive.

## Testing

See `webgpu_tests/README.md` for full detail. In order of speed/cost:

```bash
# Shader corpus — fastest, no engine build needed, just tint_convert_cli:
cd webgpu_tests/shader_corpus && ./compile_fixtures.sh && node run_tests.mjs

# Driver unit tests (JS, standalone):
cd webgpu_tests/driver_unit_tests && node run_tests.mjs

# SPIR-V preprocessing pass tests (the 12 C++ passes, standalone):
cd webgpu_tests/preprocessing_tests && node run_tests.mjs

# Resource lifecycle / screenshot comparison (standalone, need Playwright):
cd webgpu_tests/resource_lifecycle && node run_tests.mjs
cd webgpu_tests/screenshot_comparison && node run_tests.mjs

# Full local CI (rebuilds engine + runs everything, mirrors CI):
./webgpu_tests/local_ci.sh                 # rebuild + full suite
./webgpu_tests/local_ci.sh --quick          # shader corpus + scene smoketest only, no rebuild
./webgpu_tests/local_ci.sh --no-safari      # skip Safari (no AppleScript needed)
```

SPIR-V dump validation and the smoke/scene-smoketest tiers need a full editor + web template build first (see `webgpu_tests/README.md` for exact commands and `GODOT_DUMP_SPIRV` env var usage). `webgpu_tests/shader_corpus/expected_failures.json` is the baseline of known/accepted Tint conversion failures (Vulkan-only shader variants never used by the WebGPU runtime) — CI only fails on failures *not* in that baseline.

## Linting

Standard Godot pre-commit setup (clang-format for C/C++/GLSL, ruff for Python, codespell, etc.):

```bash
pre-commit run --all-files
pre-commit run --hook-stage manual clang-tidy   # needs compile_commands.json, not run automatically
```

## Architecture

### The RenderingDeviceDriver pattern

Godot's rendering backends are all implementations of the same abstract interfaces defined in `servers/rendering/rendering_device_driver.h`, `rendering_context_driver.h`, and `rendering_shader_container.h`. When adding or changing a driver method, check `drivers/vulkan/` and `drivers/metal/` first for the reference pattern — Metal is generally the closest architectural analog to WebGPU among the existing backends. **Any change to these three base-class headers requires auditing all pure-virtual overrides in `drivers/webgpu/` for gaps** — a missing override doesn't always show as a compile error if the base class still provides a default, but calling it can silently misbehave or (for pure virtuals) fail to link.

### `drivers/webgpu/` file roles

| File | Role |
|------|------|
| `rendering_device_driver_webgpu.{h,cpp}` | Core driver — implements all `RenderingDeviceDriver` virtual methods (buffers, textures, pipelines, command recording, bind groups, push-constant emulation) |
| `rendering_context_driver_webgpu.{h,cpp}` | Device/adapter/surface lifecycle, driven from the JS shell |
| `rendering_shader_container_webgpu.{h,cpp}` | SPIR-V storage + push-constant metadata for compiled shaders |
| `webgpu_objects.h` | Thin wrapper structs around WebGPU handles (`WGBuffer`, `WGTexture`, `WGShader`, `WGPipelineWrapper`, ...) |
| `pixel_formats_webgpu.h` | Godot `DataFormat` ↔ `WGPUTextureFormat` mapping |
| `spirv_preprocess.{h,cpp}` | 12 binary-level SPIR-V rewriting passes (see below) |
| `tint_wrapper.{h,cpp}` | Runs the preprocessing passes then calls into vendored Tint for SPIR-V→WGSL |
| `wgsl_precompile.py` | Build-time driver: compiles every engine shader variant through the same pipeline the runtime uses, ahead of time, into `wgsl_precompiled.gen.h` (generated, gitignored) |
| `tint_cli/` | `tint_convert_cli` — standalone native host tool wrapping `spirv_preprocess` + `tint_wrapper` for build-time precompilation and standalone testing/debugging (see Build Commands) |

### Shader pipeline

```
GLSL → SPIR-V (glslang, at engine/editor build time)
     → 12 binary-rewriting passes (spirv_preprocess.cpp, C++, at runtime or build-time precompile)
     → WGSL (Tint, C++, linked directly into the engine — no WASM/JS translation step)
     → GPU (browser's WebGPU implementation)
```

Tint is vendored under `thirdparty/tint` (patches tracked in `thirdparty/README.md`), needing `thirdparty/spirv-tools` and `thirdparty/spirv-headers` (the latter extracts a broader header set than upstream Godot's own minimal extraction, specifically for Tint's SPIR-V reader). Shaders are precompiled ahead-of-time by `wgsl_precompile.py` during the engine build where possible; the same 12-pass + Tint pipeline also runs at runtime (via `tint_wrapper.cpp`) as a fallback for anything not precompiled. The 12 passes exist to work around specific gaps/quirks between what glslang emits and what Tint/WGSL accepts (combined-sampler splitting, push-constant→uniform conversion, depth-texture fixups, etc.) — when a new shader fails Tint conversion, the fix is almost always either a new/extended preprocessing pass here or a Tint patch, not a WGSL-writer change.

**Debugging a Tint conversion failure**: `tint_convert_cli`'s batch mode (used by `wgsl_precompile.py`) forks a child process per shader and redirects its stdout/stderr to `/dev/null`, specifically so a Tint internal-compiler-error abort doesn't kill the whole batch — this means the real crash diagnostic is normally invisible. Run `tint_convert_cli <file.spv>` directly (single-file mode, un-forked) to see it. Two env-var-gated debug hooks exist for this: `WGSL_DEBUG_DUMP=<substring>` on `wgsl_precompile.py` dumps matching shaders' raw SPIR-V to `/tmp/wgsl_debug_dump/`, and `TINT_DEBUG_DUMP_PREPROCESSED=<path>` on `tint_convert_cli` dumps the SPIR-V after all 12 passes but before Tint sees it.

### Key architectural decisions

- **Push constants**: not supported by WebGPU — emulated via a ring buffer at group(3)/binding(120), rewritten from `OpVariable PushConstant` at the SPIR-V level.
- **Subpasses**: not supported — each Godot subpass becomes a separate `WGPURenderPassEncoder`. `render_forward_mobile.cpp` disables `using_subpass_post_process` under `WEB_ENABLED`.
- **Barriers**: `command_pipeline_barrier` is a no-op — WebGPU tracks hazards automatically.
- **Buffer mapping**: `buffer_map()` returns a CPU shadow copy (not GPU-visible memory like Vulkan/Metal/D3D12); `buffer_unmap()` flushes it via `wgpuQueueWriteBuffer`. This is why `RenderingDevice`-level code that's driver-agnostic gates WebGPU-only behavior behind `driver->api_trait_get(RDD::API_TRAIT_*)` checks rather than `#ifdef WEBGPU_ENABLED` — see the `API_TRAIT_*` block in `rendering_device_driver.h` for the full list of WebGPU-specific traits and why each exists (they're heavily commented). **Any new cross-driver optimization gated this way must also get a base-class default in `rendering_device_driver.cpp`'s `api_trait_get()`**, or every non-WebGPU driver spams `ERR_FAIL_V(0)` when the shared code queries it.
- **Device init**: the JS shell pre-initializes a `GPUDevice` and stores it on `Module["preinitializedWebGPUDevice"]`; C++ retrieves it via the emdawnwebgpu port's `WebGPU.importJsDevice()` through `EM_ASM_PTR`, not `emscripten_webgpu_get_device()`/`html5_webgpu.h` (removed in modern Emscripten).
- **Format constraints**: no 3-component texture formats (RGB8/16/32 unsupported as textures), 256-byte row alignment for buffer↔texture copies, no multi-draw-indirect (loop over individual draws), max 4 bind groups (set 3 is reserved for push-constant emulation).

### Where to look first

- `webgpu_notes/TASKS.md` — the living task/status doc, organized by phase; check it before starting work to see what's known-broken or in-progress. Update it (status, completion notes) when you finish or discover something significant, following the existing per-task format (Status/Severity/Lines/Issue/Investigation).
- `webgpu_site/ARCHITECTURE_AND_DESIGN.md`, `TECHNICAL_REFERENCE.md`, `PERFORMANCE_AND_OPTIMIZATION.md`, `CORRECTNESS_AND_COMPATIBILITY.md` — in-depth docs on the areas their names suggest.
- `drivers/webgpu/README.md` — driver-local notes.
- `webgpu_tests/screenshot_comparison/LIVE_REPRO_METHODOLOGY.md` — how to test a specific reported bug against a real Godot project (not this repo's own fixtures): scratch-copy setup, native-Vulkan screen-recording capture, WebGPU screenshot/console capture via Playwright, export-freshness verification, and reading numeric data back out of the engine for non-visual comparisons. Use this before improvising a one-off repro script.

## Godot Coding Conventions

- C++ style: `snake_case` everywhere; classes `PascalCase`; member variables `_prefixed` or plain `snake_case`.
- Memory: `memnew(T)` / `memdelete(ptr)`, not `new`/`delete`. `Vector<T>` / `HashMap<K,V>`, not STL containers, in engine code.
- Error handling: return `ERR_*` constants; use `ERR_FAIL_COND_V(cond, val)` / `ERR_FAIL_NULL_V(ptr, val)` macros rather than manual `if` + return.
- Logging: `print_verbose(...)`, `WARN_PRINT(...)`, `ERR_PRINT(...)` — never `printf`/`std::cout` in engine code (the native `tint_cli` host tool is an exception, since it's a standalone binary outside the engine runtime).
- Opaque driver IDs are integers cast to/from pointers via the `ID` helper — see existing RDD methods in `drivers/webgpu/` for the pattern.
- Includes: `"..."` for project headers, `<...>` for system/third-party; no `.h` extension for standard library headers.
