# Copilot Instructions — Godot WebGPU Backend

## Project Goal

Add a WebGPU rendering driver to Godot 4.6, targeting browser export via Emscripten. The driver must implement the `RenderingDeviceDriver` / `RenderingContextDriver` / `RenderingShaderContainerFormat` interfaces (same as the Vulkan, D3D12, and Metal backends). Deadline: March 24, 2026.

## File System Rules

**Always save files inside this workspace.** Paths outside the workspace (e.g. `/tmp/`, `../`) require human approval for each write, which blocks automation. Use workspace-relative equivalents instead:

- Use `./tmp/` instead of `/tmp/` — already in `.gitignore`, safe for scratch files
- Use `./foo/` instead of `../foo/`, and add `./foo/` to `.gitignore` if it should not be committed

This applies to commit message files, build logs, temp scripts, and any other scratch output.

## Read These First

All design decisions, API mappings, and tasks are documented in `webgpu_notes/`:

| File | Read When |
|------|-----------|
| `webgpu_notes/TASKS.md` | Starting any session — gives current phase and next task |
| `webgpu_notes/TASKS.md` §4.1 | Phase 0: wiring the build system (Day 1) |
| `webgpu_notes/TASKS.md` §4.1 | Implementing any RDD method — has exact WebGPU API calls |
| `webgpu_notes/TASKS.md` §4.1 | Background on architecture, prior art, WebGPU constraints |
| `webgpu_notes/stubs/` | Starting point files to copy into `drivers/webgpu/` |

**Do not re-research what is already in these files.** Read them first.

## Repository Layout (relevant paths)

```
drivers/webgpu/           ← target location for the new driver (copy from stubs/)
servers/rendering/        ← RenderingDeviceDriver, RenderingContextDriver interfaces
drivers/metal/            ← best reference driver (closest to WebGPU)
platform/web/             ← Emscripten platform, DisplayServerWeb
modules/glslang/          ← GLSL→SPIR-V (needs webgpu added to can_build())
```

## Godot Coding Conventions

- **C++ style:** snake_case everywhere. Classes: `PascalCase`. Member variables: `_prefixed` or plain snake_case.
- **Memory:** use `memnew(T)` / `memdelete(ptr)` instead of `new`/`delete`. Vectors use `Vector<T>`, hashmaps use `HashMap<K,V>`.
- **Error handling:** return `ERR_*` constants; use `ERR_FAIL_COND_V(cond, val)` / `ERR_FAIL_NULL_V(ptr, val)` macros.
- **Printing:** `print_verbose(...)`, `WARN_PRINT(...)`, `ERR_PRINT(...)`. Never `printf` or `std::cout`.
- **IDs:** RDD uses opaque integer IDs cast to/from pointers via the `ID` helper. See `webgpu_notes/TASKS.md` §4.1 Section 1 for the exact pattern.
- **Includes:** use `"..."` for project headers, `<...>` for system/third-party. No `.h` extension for standard library headers.

## Key Architectural Decisions (already made)

1. **Push constants:** Emulated via a 256KB ring buffer (group 3, binding 0) with 256-byte aligned slots. See `DESIGN.md` Section 5.
2. **Subpasses:** Flattened — each Godot subpass becomes a separate `WGPURenderPassEncoder`. See `DESIGN.md` Section 6.
3. **Barriers:** All no-ops — WebGPU tracks hazards automatically.
4. **Buffer mapping:** Shadow CPU buffer pattern — maintain a CPU copy, flush via `wgpuQueueWriteBuffer` on unmap.
5. **Device init:** JS shell stores a pre-initialized `GPUDevice` in `Module["preinitializedWebGPUDevice"]`; C++ retrieves it via `EM_ASM_PTR({ return WebGPU["importJsDevice"](Module["preinitializedWebGPUDevice"]); })`. (`emscripten_webgpu_get_device()` and `html5_webgpu.h` were removed in Emscripten 5.x.)
6. **Shader translation:** GLSL → SPIR-V (glslang) → passed directly as `WGPUShaderSourceSPIRV` (Dawn's `webgpu.h` supports SPIR-V natively; no WGSL/Tint translation step needed).

## WebGPU Constraints to Keep in Mind

- Max 4 bind groups (Godot uses sets 0–3 → fits exactly, but set 3 is used for push constant emulation)
- No push constants → ring buffer emulation
- No subpasses → flatten each subpass into a separate render pass
- No synchronous buffer map → shadow buffer approach
- 256-byte row alignment for buffer↔texture copies
- No 3-component texture formats (RGB8, RGB16, RGB32 unsupported as textures)
- WGSL has no `gl_FragDepth` → use `@builtin(frag_depth)`; no `discard` keyword → use `discard` statement in WGSL (same word, different syntax)
- No multi-draw-indirect → loop over individual draws

## Phase Status

Check `webgpu_notes/TASKS.md` for current phase. Current state as of March 12, 2026:
- **Phase 0 (Day 1):** ✅ DONE — Build system wiring + driver registration committed
- **Phase 1 (Days 2–4):** ✅ DONE — Core driver skeleton, WebGPU bootstrapping, context driver, swap chain
- **Phase 2 (Days 5–7):** ✅ DONE — Shaders, buffers, textures, samplers, uniform sets, pipelines, 2D rendering
- **Phase 3 (Days 8–10):** 🔄 IN PROGRESS — 3D rendering, compute shaders, timestamps
- **Phase 4–5 (Days 11–14):** TODO — Export, HTML shell, testing

## Build Commands

```bash
# Build web template with WebGPU (after Phase 0 changes):
scons platform=web target=template_debug webgpu=yes opengl3=no -j$(nproc)

# Build macOS editor (for testing non-web code paths):
scons platform=macos target=editor -j$(nproc)

# Run static checks before committing:
python misc/scripts/check_style.py
```

## Emscripten Notes

- **Installed version: Emscripten 5.0.0** (at `/Users/dwalter/Documents/projects/godot/emsdk`)
- WebGPU is provided via the emdawnwebgpu port (Dawn). `-sUSE_WEBGPU=1` was **removed** in Emscripten 5.x.
- Build flag (both CCFLAGS and LINKFLAGS): `--use-port=emdawnwebgpu`
- Dawn webgpu.h is at: `emsdk/upstream/emscripten/cache/ports/emdawnwebgpu/emdawnwebgpu_pkg/webgpu/include/webgpu/webgpu.h`
- `html5_webgpu.h` and `emscripten_webgpu_get_device()` **do not exist** in Emscripten 5.x — use `EM_ASM_PTR` (see Decision #5)
- Surface from canvas: `WGPUEmscriptenSurfaceSourceCanvasHTMLSelector` (renamed from `WGPUSurfaceDescriptorFromCanvasHTMLSelector`), sType: `WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector`
- All string parameters use `WGPUStringView{const char* data, size_t length}`. Use `WGPU_STRLEN = SIZE_MAX` for null-terminated strings.
- `WGPUBufferUsage` / `WGPUTextureUsage` (the `Flags` suffix was dropped in Dawn)
- `WGPUTexelCopyBufferInfo = {WGPUTexelCopyBufferLayout layout; WGPUBuffer buffer;}` — copy functions take this combined struct
- `WGPUVertexFormat_Undefined` was removed — use `(WGPUVertexFormat)0`
- Source Emscripten before building: `source /Users/dwalter/Documents/projects/godot/emsdk/emsdk_env.sh`
