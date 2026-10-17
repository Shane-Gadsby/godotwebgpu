# Godot WebGPU Backend — Technical Reference

## 1. Driver Core: Object Model & Resource Management

### 1.1 Object Lifecycle Pattern

All GPU resources follow a uniform pattern: heap-allocate a wrapper struct, configure it, return the pointer cast to an opaque ID. The pattern mirrors Vulkan and Metal backends exactly:

```
WGFoo *foo = new WGFoo();
foo->handle = wgpuDeviceCreateFoo(device, &desc);
if (foo->handle == nullptr) { delete foo; return FooID(); }
// ... configure additional fields ...
return FooID(foo);
```

Destruction is the reverse: cast ID back to pointer, release GPU handles, delete struct.

### 1.2 Resource Types

**WGBuffer**
- GPU handle (`WGPUBuffer`)
- CPU shadow buffer (`shadow_map`) for staging writes
- Dirty range tracking (`dirty_offset`, `dirty_end`)
- Async lifecycle flags (`map_pending`, `map_complete`, `freed`)
- Dynamic buffer support (`frame_idx`, `per_frame_size`) for multi-frame uniform rotation

**WGTexture**
- GPU handle and default view
- `view_source` pointer (always the owning `WGPUTexture`, even for shared/sliced views)
- Format metadata (original vs promoted format)
- Usage flags (propagated from creation)
- `gpu_handle()` helper resolves shared/sliced views to their parent texture

**WGShader**
- Per-stage shader modules (6 slots: vertex, fragment, compute, etc.)
- Per-stage raw SPIR-V bytes (for deferred specialization constant patching)
- Pipeline layout and per-set bind group layouts
- Push constant emulation metadata (bind group index, binding, stage mask)
- Merged PC group layout (when push constants share a group with material uniforms)
- Reflection data (`bind_group_infos`) for uniform set creation
- Depth alias binding map (for depth textures split during SPIR-V preprocessing)

**WGPipelineWrapper**
- Union of render/compute pipeline handles
- For strip topologies: both Uint16 and Uint32 pipeline variants
- Owns specialized shader modules (released with pipeline)
- Stored stencil reference (applied dynamically at bind time)
- `blend_constant_alpha`: the pipeline blends with `BLEND_FACTOR_(ONE_MINUS_)CONSTANT_ALPHA`, which WebGPU lacks; `_apply_blend_constants()` then sets the single RGBA blend constant to the alpha broadcast (a pipeline using both constant colour and constant alpha warns once and reads the colour)
- `primitive.unclippedDepth` when the rasterization state asks for depth clamp and the device has `depth-clip-control`; the multisample mask comes from `sample_mask`; every MSAA count other than 1 is clamped to 4 (WebGPU accepts 1 and 4 only)

**WGUniformSet**
- Bind group handle
- Temporary views (created for dimension/format fixups, released with set)
- Rebind cache (`HashMap<WGPUBindGroupLayout, WGPUBindGroup>`) for cross-shader compatibility
- Dynamic buffer tracking for offset calculation

**WGCommandBuffer**
- Command encoder (created per `command_buffer_begin`)
- Active encoder state machine (NONE / RENDER / COMPUTE)
- Render state cache (current pipeline, index buffer, vertex buffers per slot, bind groups per slot)
- Push constant data (128 bytes) and dirty flag

### 1.3 Ownership & Leak Prevention

Ownership is simple: the struct holding a `WGPUFoo` handle owns it and calls `wgpuFooRelease()` on destruction. Shared textures explicitly null their handle (don't own the underlying GPU texture).

Every resource creation function follows a delete-on-failure pattern:
- `buffer_create`: deletes WGBuffer on `wgpuDeviceCreateBuffer` failure
- `texture_create`: releases texture AND deletes struct on view creation failure
- `shader_create_from_container`: goto-based cleanup releases all partially-created modules and layouts
- `render_pipeline_create`: releases specialized modules on failure
- `uniform_set_create`: deletes WGUniformSet on `wgpuDeviceCreateBindGroup` failure

### 1.4 Async Lifetime (Freed-While-Pending Pattern)

For objects with async callbacks (buffers, fences, query pools, readback entries):

```cpp
void buffer_free(BufferID p_buffer) {
    WGBuffer *buf = ...;
    if (buf->map_pending) {
        buf->freed = true;  // Callback will handle cleanup
        return;
    }
    // Normal cleanup
    wgpuBufferRelease(buf->handle);
    memfree(buf->shadow_map);
    delete buf;
}

static void _buffer_deferred_map_cb(WGPUMapAsyncStatus status, void *userdata) {
    WGBuffer *buf = (WGBuffer *)userdata;
    buf->map_pending = false;
    if (buf->freed) {
        if (buf->handle) { wgpuBufferUnmap(buf->handle); wgpuBufferRelease(buf->handle); }
        if (buf->shadow_map) memfree(buf->shadow_map);
        delete buf;
        return;
    }
    // Normal handling
}
```

This pattern is consistently applied across all 4 async callback sites, preventing use-after-free in the single-threaded WASM environment.

---

## 2. Command Recording & Submission

### 2.1 Command Buffer Model

Each `command_buffer_begin()` creates a fresh `WGPUCommandEncoder`. The command buffer tracks which encoder type is active (render pass, compute pass, or none) and automatically ends the active encoder when switching.

### 2.2 Render Pass Lifecycle

1. `command_begin_render_pass`: Ends any active encoder, builds `WGPURenderPassDescriptor` from `WGRenderPass` metadata + `WGFramebuffer` attachment views, begins render pass encoder
2. `command_next_render_subpass`: Ends current render pass, begins new one for next subpass (WebGPU has no native subpass concept)
3. `command_end_render_pass`: Ends encoder, releases it, resets state

### 2.3 Compute Pass Lifecycle

Compute passes are lazily begun in `command_bind_compute_pipeline` (only creates the encoder when actually needed). This avoids creating empty compute passes.

### 2.4 Draw Call Encoding

- `command_render_draw`: Flushes push constants, issues `wgpuRenderPassEncoderDraw`
- `command_render_draw_indexed`: Same, plus selects strip-topology pipeline variant based on current index format
- Indirect draws: uses the native `multi-draw-indirect` feature in one call when available and the buffer stride matches WebGPU's implicit draw-struct layout; otherwise loops over `p_draw_count`
- `draw_indexed_indirect_count`: uses `p_max_draw_count` (no async count readback implemented)

### 2.5 Redundant State Elimination

The `WGCommandBuffer::RenderState` tracks currently-bound state to skip redundant WebGPU calls:
- Pipeline bind (skip if same `WGPipelineWrapper*`)
- Index buffer (skip if same handle + offset + format)
- Vertex buffers (per-slot tracking, 8 slots)
- Bind groups (per-slot tracking, 4 slots; invalidated on shader change)

State is reset at render pass begin and subpass transitions.

### 2.6 Encoder Splitting for Sync Isolation

When a render pass attachment has both `TextureBinding` and `RenderAttachment` usage flags, the driver:
1. Flushes push constant shadow buffer
2. Finishes and submits current command encoder
3. Creates a fresh encoder

This prevents WebGPU validation errors for intra-pass sync scope conflicts. It's conservative but safe.

### 2.7 Submission

`command_queue_execute_and_present`:
1. Flushes batched push constant data via single `wgpuQueueWriteBuffer`
2. Submits all command buffers via `wgpuQueueSubmit`
3. Registers work-done callback on fence
4. Presents swap chain (releases texture/view for browser compositing)
5. Initiates async readback for timestamp query pools

---

## 3. Bind Groups, Uniforms & Push Constants

### 3.1 Bind Group Layout Creation

BGLs are derived from two sources:
1. **SPIR-V reflection** (Godot's ShaderReflection): binding types, counts
2. **WGSL text scanning**: texture dimensions, sampler types, storage access modes, depth alias bindings, per-stage SSBO metadata

The WGSL scanning handles information lost in the SPIR-V→WGSL conversion (WebGPU's type system is stricter than Vulkan's).

**Binding numbering**: All bindings are doubled because the combined-sampler split places the texture at `binding*2` and its sampler at `binding*2+1`. Elements 1..16 of an expanded texture array get bindings of their own from 400 up (`spirv_binding_layout.h`: `400 + 16*binding + element - 1`), above every doubled binding and clear of the push-constant ring's 120; the BGL lists them after the per-uniform entries, and the merged push-constant layout includes them.

### 3.2 Uniform Set Creation

`uniform_set_create` builds bind group entries with extensive fixup logic:
- **Depth/Float mismatch**: Substitutes fallback float textures for depth textures bound to float sample-type entries. Whatever the shader then reads is a constant, which is right for Godot's placeholder depth textures and silently wrong for a real depth buffer -- volumetric fog's shadows, `DEPTH_TEXTURE` and omni cube shadows were all lost that way until 2026-10-17. Every substitution is `print_verbose`d with the shader name, set and GLSL binding; whether a binding is a depth texture at all is decided by `_reclassify_single_component_depth_textures()` from its name and usage, so a depth binding in a renderer shader should carry `depth` in its name
- **Expanded texture arrays**: element k of an array uniform is bound at its own binding (above); a uniform set whose array was collapsed by the shader (over the stage's texture budget) but holds more than filler warns once per (shader, set, binding)
- **Float32 filterability**: Substitutes fallback when float32-filterable unavailable
- **Dimension mismatch**: Creates corrected texture views (2DArray → 2D) using slice base offsets
- **MSAA mismatch**: Uses fallback multisampled texture
- **Buffer aliasing**: Redirects duplicate writable storage buffer bindings to a stub buffer (particle system workaround)
- **Dynamic buffers**: Records per-frame offset for dynamic offset calculation

### 3.3 BGL Rebinding Cache

When a uniform set created with shader A's BGL needs to be used with shader B's pipeline:
1. Check if BGLs are pointer-equal (fast path)
2. Check rebind cache
3. On miss: adapt entries (sampler type, texture sample type, view dimension), filter to target BGL bindings, create new bind group, cache it

### 3.4 Push Constant Emulation (Detailed)

```
1. command_bind_push_constants():
   -> Copy data into WGCommandBuffer::push_constant_data[128]
   -> Set push_constants_dirty = true

2. _flush_push_constants (before every draw/dispatch):
   -> Check dirty flag (skip if clean)
   -> Copy data to shadow buffer at push_constant_ring_offset
   -> Track dirty range [start, end]
   -> SetBindGroup(group3, pc_bind_group, dynamic_offset=ring_offset)
   -> Advance ring offset by 256
   -> Clear dirty flag

3. Submit time:
   -> Single wgpuQueueWriteBuffer for entire dirty range of shadow buffer
   -> Reset ring offset for next frame
```

**Merged PC Group**: When shader group 3 has both material uniforms AND push constants, a merged BGL contains both. The flush path preserves material dynamic offsets while patching in the PC ring offset.

---

## 4. Shader Pipeline Details

### 4.1 SPIR-V Pre-Processing (`spirv_preprocess::run_pipeline()`)

Every path that converts SPIR-V to WGSL -- the runtime fallback (`spirv_to_wgsl.cpp`), the build-time
precompiler and export-time baker (`tint_convert_cli`), and the fuzzer -- calls `run_pipeline()`, so this
order exists in exactly one place. It is the source of truth; this list was written from it on 2026-10-17
(the long-standing "twelve passes" had grown well past twelve). Raw word-rewriting passes live in
`spirv_preprocess.cpp`; the three that restructure control flow or add types are written on SPIRV-Tools'
optimizer IR in `spirv_opt_passes.cpp` (marked *IR*).

1. **inline_opaque_functions**: inlines functions that take textures/samplers as parameters, so every later pass sees handles only at their use sites.
2. **freeze_spec_constant_ops** -- only when `spec_constants_overridable()` says no: evaluates `OpSpecConstantOp` and fixes every spec constant to its default. Otherwise spec constants stay and become WGSL `@id(N) override`s.
3. **rewrite_copy_logical**: `OpCopyLogical` → `OpCopyObject` (SPIR-V 1.4+).
4. **rewrite_terminate_invocation**: `OpTerminateInvocation` → `OpKill` (SPIR-V 1.6).
5. **convert_push_constants_to_uniforms**: push constants → a storage buffer at `@group(3) @binding(120)` (the push-constant ring).
6. **split_combined_samplers**: splits combined image-samplers and doubles every binding (texture at 2b, sampler at 2b + 1).
7. **fix_depth2_images**: `depth=2` (unknown) images → `depth=1`.
8. **negate_position_y**: flips `gl_Position.y` for WebGPU's NDC.
9. **strip_unsupported_decorations**: drops `Restrict` and similar hints Tint's reader rejects.
10. **strip_memory_barrier**: `OpMemoryBarrier` → `OpNop`.
11. **strip_image_write_operands** / 12. **strip_image_fetch_read_flag_only_operands**: drop sign-extension-only image operand masks Tint's reader rejects.
13. **hoist_constant_local_arrays**: constant-initialized local arrays become module-scope constants.
14. **split_initialized_local_arrays**: the remaining initialized local arrays get explicit stores instead of an initializer.
15. **strip_helper_invocation_builtin**: `HelperInvocation` → false.
16. **fold_ballot_bit_count** then 17. **lower_subgroup_ops**: subgroup ballots and reductions folded to single-lane equivalents (WebGPU has no subgroups here).
18. **fix_nonfinite_literals**: Inf/NaN literals → FLT_MAX / FLT_MIN (WGSL has no such literals).
19. **lower_tint_unsupported_ops** (*IR*): `OpIsNan`/`OpIsInf` → bit tests, `OpImageQueryLod` → derivative/size math, `PointCoord` → `vec2(0.5)`, `EarlyFragmentTests` removed. Each of these aborted Tint's reader, which took the whole page down.
20. **apply_sampler_lod_bias** (*IR*): WebGPU samplers carry no mip bias, so in a module that declares the `SamplerLodBias` uniform (GLSL binding 40 of the scene shaders' render-pass set) every implicit-LOD sample through a sampler of that set gets the value added as a Bias operand.
21. **expand_binding_arrays** (*IR*): arrays of textures/samplers/storage images become one variable per element, bound per `spirv_binding_layout.h` (element 0 at 2b, element k at 400 + 16b + k - 1); dynamic indexing becomes a switch. Budgeted against the stage's texture limit (16 sampled by default, 48 for a module already past 16; storage 4/8). Read-write storage-image arrays and arrays that do not fit are left for the next pass.
22. **flatten_binding_arrays**: whatever is still an array is collapsed to element 0 (the driver warns once if its uniform set really holds more).
23. **infer_readonly_storage**: adds `NonWritable` to SSBOs never written, so Tint emits `var<storage, read>`.
24. **strip_writeonly_storage_decoration**: write-only SSBOs become read-write (WGSL has no write-only buffers).
25. **eliminate_dead_resources**: drops resource variables the entry point never uses.
26. **eliminate_local_single_block_vars** (SPIRV-Tools optimizer): removes the load/store idiom that otherwise fails Tint's uniformity analysis.

### 4.2 Tint Conversion

After preprocessing, the SPIR-V is passed to Tint (Google's Dawn/Chrome shader compiler) via `tint_wrapper_spirv_to_wgsl()`. Tint performs SPIR-V parsing, IR validation, and WGSL code generation. Six patches to Tint handle Godot-specific requirements (UBO layout, spec constant size mismatches, point_size tolerance).

### 4.3 WGSL Post-Processing

- Replace f32::MAX decimal representations with hex float `0x1.fffffep+127f`
- Strip `enable f16;` (Chrome doesn't support shader-f16)
- Prepend `diagnostic(off, derivative_uniformity);` (suppress non-uniform control flow errors)
- Prepend SSBO_USED metadata comments for per-stage BGL visibility

### 4.4 Specialization Constants

Implemented via SPIR-V binary patching:
1. Scan `OpDecorate` for SpecId → result_id mapping
2. Patch `OpSpecConstant`/`OpSpecConstantTrue`/`OpSpecConstantFalse` values in place
3. Re-convert patched SPIR-V through full pipeline (different hash = separate cache entry)
4. Specialized modules owned by WGPipelineWrapper, released with it

### 4.5 Caching

- Static process-lifetime `HashMap<uint64_t, String>`
- 64-bit key: two MurmurHash3 passes with different seeds
- ~1k entries typical, never evicted (process lifetime = page session)
- Hit/miss counters for diagnostics

---

## 5. Texture & Buffer Operations

### 5.1 Format Promotion/Demotion

**Storage format promotion** (when `StorageBinding` usage present):
- R8/RG8 → R32Float/RG32Float (or native with texture-formats-tier1)
- R16/RG16 → R32/RG32 equivalents
- Applied at texture creation + matching promotion in WGSL text

**Float32 demotion** (when `float32-filterable` unavailable):
- RGBA32Float → RGBA16Float, RG32Float → RG16Float, R32Float → R16Float
- Only for sampling-only textures (not storage/render/MSAA)
- Bidirectional conversion in upload and readback paths

**Texture swizzle emulation** (WebGPU has no swizzle):
- L8 (Luminance) → RGBA8 with luminance baked into RGB channels
- LA8 (Luminance-Alpha) → RGBA8 with (L,L,L,A) pattern

This conversion runs in `_validate_texture_format()`, which
`_texture_2d_update()` calls on **every** update — not just at creation. For a
texture that is re-uploaded as it grows, that is a full re-expansion each time.

Monochrome **font atlases** are the case where that mattered: an atlas is
re-uploaded whenever a glyph is added to it, so a text-heavy UI paid a
full-atlas LA8→RGBA8 expansion per new glyph (~256 KB per 256×256 atlas). The
text servers therefore rasterize monochrome glyph atlases **directly as RGBA8**
under `WEBGPU_ENABLED` (`MONO_GLYPH_COLOR_SIZE` /
`_write_mono_glyph_texel()` in `text_server_adv` and `text_server_fb`), which
removes the conversion rather than repeating it. GPU memory is identical; the
CPU-side atlas doubles. Colour, LCD and MSDF glyph paths were already RGBA8 and
are unchanged. See `webgpu_notes/TASKS.md` Task 35.

### 5.2 Buffer Operations

**Upload (non-readback)**:
- `buffer_map`: Returns CPU shadow buffer pointer, marks dirty
- `buffer_unmap`/`buffer_flush`: Flushes dirty range via `wgpuQueueWriteBuffer`
- `buffer_create_with_data`: Uses `mappedAtCreation` (zero-copy initial data)

**Readback (async)**:
- `buffer_get_data_direct`: Maintains `_readback_cache` of per-source staging buffers
- First call: create staging buffer, copy GPU→staging, initiate async map, return false
- Subsequent: return previous frame's data, initiate fresh copy
- 1-frame latency pattern

**Direct write** (bypasses staging):
- `buffer_write_direct`: Single `wgpuQueueWriteBuffer` call
- Used by skeleton atlas (single buffer covers all bone data)

### 5.3 Texture Copy Operations

- `command_copy_buffer_to_texture`: always an encoder `copyBufferToTexture`. A staging buffer's CPU shadow is first queued for the pre-submit flush (`pending_lo`/`pending_hi`). It used to be `wgpuQueueWriteTexture`, which executes at submit *before* the encoder's own earlier commands, so an upload could land under a clear or copy recorded before it -- SDFGI's cascades scrolled into garbage while the camera moved
- `command_clear_color_texture`: clears every z-slice of a 3D texture (only slice 0 was cleared before). Formats that cannot be a render attachment are cleared by an encoder copy from a cached zero buffer (or a per-clear pattern buffer for non-zero colors)
- `command_copy_texture` / `command_copy_texture_to_buffer`: a 3D texture's z offset is `texture_offset.z`, not the array layer
- `command_copy_buffer_to_texture_layered`: Collapses N per-layer copies into one multi-layer call (saves N-1 IPC crossings)
- `texture_initialize_direct_layered`: Direct CPU→GPU write from malloc'd buffer (no transfer worker, no staging VRAM)
- All copies respect WebGPU's 256-byte row alignment requirement

### 5.4 Texture Views & Shared Textures

- `texture_create_shared`: New WGTexture with `handle=nullptr` (doesn't own GPU texture), inherits `view_source`, creates potentially re-formatted view
- `texture_create_shared_from_slice`: Same but with mipmap/layer subset, updates `view_dimension` for downstream fixups

---

## 6. Surface & Presentation

### 6.1 Swap Chain Configuration

- Format: always `BGRA8Unorm` (browser canvas standard)
- Alpha mode: `Opaque` (with alpha strip workaround for Chrome)
- Present mode: `Fifo` (browser always vsyncs via requestAnimationFrame)
- Canvas selector: `#canvas` (hardcoded, standard for Godot web exports)

### 6.2 Frame Acquisition

1. Check if surface needs resize
2. Release previous frame's framebuffer/view/texture
3. `wgpuSurfaceGetCurrentTexture`
4. Handle status (Lost → unrecoverable, Outdated/SubOptimal → request resize)
5. Create texture view, wrap in WGFramebuffer

### 6.3 Alpha Handling (Chrome Workaround)

Chrome ignores `CompositeAlphaMode_Opaque` and composites alpha against background. The driver:
- Strips `WGPUColorWriteMask_Alpha` from ALL pipelines targeting BGRA8Unorm
- Forces `LoadOp_Clear` with alpha=1.0 for swap chain render passes
- Blit shader forces `color.a = 1.0`

---

## 7. Rendering Server Integration

### 7.1 Forward Mobile Renderer Modifications

**Shadow pass optimization**:
- Omni lights use the shadow mode the light asks for. Until 2026-10-17 `API_TRAIT_FORCE_OMNI_DUAL_PARABOLOID` forced dual-paraboloid (2 passes instead of 6) for speed; it is now 0 for parity with native, and a project that wants the cheaper shadows sets `omni_shadow_mode = SHADOW_DUAL_PARABOLOID` per light
- Pre-clear positional shadow atlas once, individual passes use LOAD
- Merge same-framebuffer shadow passes into single render pass encoder

**Instance batching** (opaque passes):
- Lookahead detects consecutive draws with identical state
- Merges into instanced draws (`drawIndexed(indexCount, batchCount, ...)`)
- Shader uses: `draw_call.instance_index + gl_InstanceIndex`
- Exclusions: transparent pass, skinned meshes, multimesh, particle instances

**firstInstance optimization**:
- Encodes instance base index in `firstInstance` draw parameter
- Compare remaining push constant fields via memcmp
- Skip SetBindGroup when unchanged (saves 1 IPC per draw)

**Subpass disabling**:
- All subpass-based post-processing disabled (WebGPU has no input attachments)
- Tonemap subpass variants disabled

### 7.2 Skeleton Atlas

All bone data consolidated into a single GPU storage buffer:
- Bump allocator assigns contiguous slots per skeleton
- Per-frame: walk dirty list, memcpy to CPU mirror, single `buffer_update_direct`
- Shader adds `bone_offset` to bone indices for atlas lookup
- Reduces N per-skeleton IPC crossings to 1 per frame

### 7.3 Canvas 2D Renderer

- Binding layout shifted (0-3 → 1-4) to reserve binding 0 for push constant ring buffer
- Instance data goes through intermediary buffer to avoid reading from write-combined memory
- `use_lcd` field explicitly initialized in all batch creation points

### 7.4 GPU Shader Compatibility Changes

| Change | Reason |
|--------|--------|
| `modf(x, y)` → `floor() + subtraction` | WGSL has no `modf` with out-param |
| `isinf(x)` → `abs(x) > 3.0e+10` | WGSL has no `isinf` |
| `isnan(x)` → `x != x` | WGSL has no `isnan` |
| Array varyings → individual vars | WGSL doesn't support array interface variables |
| switch → if/else chains | SPIR-V scoping issues with phi nodes in WGSL |
| `texture()` → `textureLod()` on radiance | Avoid mipmap artifacts from octahedral discontinuity |
| Depth textures named `*depth*` (`shadow_depth_atlas`, `directional_shadow_depth_atlas`, `source_depth_cube`, `source_depth`) | The reclassifier only turns a GLSL `texture2D`/`textureCube` into `texture_depth_*` by name and usage; a depth texture it misses is replaced by a blank float fallback (3.2) |
| Linear depth reads through `textureGather` + bilinear weights (`textureLod_linear_depth()`, `texture_linear_depth()`) | WGSL cannot linearly filter a depth texture without comparison; replaces earlier NEAREST reads, so soft and transmittance shadows match native exactly |
| `copy.glsl` `SOURCE_IS_DEPTH_FORMAT` / `COPY_MODE_SIMPLY_COPY_DEPTH_FORMAT` | `copy_depth_to_rect` reads the depth buffer as a depth texture (feeds `DEPTH_TEXTURE`, refraction, proximity fade) |
| Contact shadows' `read_depth()` returns 0 outside [0, 1) | WebGPU samplers have no border colour |
| `SamplerLodBias` UBO at set 1, binding 40 of both scene shaders, filled by `MaterialStorage::Samplers` | Carries the viewport's mipmap bias for `apply_sampler_lod_bias` (4.1) |
| Scene `_inc.glsl`: `POINT_COORD_USED` implies `POINT_SIZE_USED` | `POINT_COORD` alone makes the material draw points; with point size emulated the quad expansion was skipped and nothing drew (upstream bug, D3D12 too) |
| FSR2 `NO_IMAGE_ATOMICS` buffer forms, `SPD_NO_WAVE_OPERATIONS` (`thirdparty/amd-fsr2/patches/0004`) | No texture atomics or subgroups in WGSL |
| Volumetric fog's own nearest, clamped `depth_sampler` (binding 22) | Point sampler for the shadow-atlas gathers, separate from the shader's linear sampler |

---

## 8. Build System

### 8.1 Build Command

```bash
# WebGPU-only release template:
scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no

# Debug with both renderers:
scons platform=web target=template_debug dlink_enabled=yes webgpu=yes opengl3=yes
```

### 8.2 Build Pipeline

1. SCons reads `platform/web/detect.py` → sets `WEBGPU_ENABLED`, `RD_ENABLED`, links emdawnwebgpu
2. `modules/glslang/config.py` → enables glslang for SPIR-V compilation
3. `drivers/SCsub` → includes `drivers/webgpu/SCsub` → compiles driver sources, SPIRV-Tools (~150 .cpp), Tint (~300 .cc with C++20), and tint_wrapper
4. Emscripten (emcc) compiles C++ with `--use-port=emdawnwebgpu` headers
5. Emscripten links with emdawnwebgpu JS glue
6. `emscripten_helpers.py` packages: .js + .wasm into template zip

### 8.3 Runtime Loading

1. `engine.js` detects `renderingDriver === 'webgpu'`
2. Requests WebGPU device (async, before WASM loads)
3. Device ready → `Godot(moduleConfig)` called with preinitializedWebGPUDevice
4. C++ imports device, Tint is already compiled into the WASM, rendering begins

---

## 9. Platform/Web Integration

### 9.1 Device Request (Feature Negotiation)

The JS engine requests maximum capabilities from the adapter:
- All supported optional features (texture tiers, compression families, timestamp-query, etc.)
- Maximum limits for key resources (storage buffers, buffer size, bind groups, sampled/storage textures per stage, texture dimensions 1D/2D/3D, array layers, color-attachment bytes per sample). `maxSampledTexturesPerShaderStage` must reach 48 or the engine falls back to Forward Mobile
- `depth-clip-control` when the adapter has it (depth clamp for the cluster builder's light volumes)
- `powerPreference: 'high-performance'`
- Graceful: never requests unsupported features (gated by adapter.features.has())

### 9.2 Async Safety on WASM

WebGPU callbacks fire during `wgpuInstanceProcessEvents` which can interleave with object destruction. The freed-while-pending pattern (Section 1.4) prevents all UAF scenarios.

Additional safety:
- `ABORTING_MALLOC=0`: malloc returns NULL on OOM instead of aborting
- `ALLOW_MEMORY_GROWTH`: WASM linear memory grows on demand
- Force-signal in `fence_wait`: prevents deadlock in single-threaded environment

### 9.3 Threading

- Single WebGPU queue. In nothreads builds, and in threaded builds with the default (Safe) thread model, every WebGPU call is made on the main thread.
- Thread-enabled exports render on their **own pthread** by default (export option `variant/separate_render_thread`, on by default; it passes `--render-thread separate`, which `rendering/driver/threads/thread_model=2` also selects): the thread owns the canvas as an `OffscreenCanvas`, requests its own adapter/device (every emdawnwebgpu handle lives in the JS table of the thread that created it), creates the `RenderingDevice` there, and pumps the command queue through `Atomics.waitAsync`, yielding to its event loop after each frame so it presents. The main thread never blocks on it: `RenderingServer::sync_previous_draw()` compares frame counters and the web main loop waits for the previous frame asynchronously. Shader translation (Tint) then runs on the render thread. `engine.js` probes WebGPU-in-a-worker at startup and the engine falls back to Safe where it is unavailable. See `webgpu_notes/TASKS.md` §2.5 and `PERFORMANCE_AND_OPTIMIZATION.md`.
- No SharedArrayBuffer requirement for WebGPU itself; only threaded builds need cross-origin isolation.

### 9.4 WorkerThreadPool Nothreads Fix

On nothreads builds, `wait_for_group_task_completion` previously leaked Group allocations. Fixed to properly increment finished counter and free via `group_allocator.free()`.

### 9.5 Assertions Configuration

Release templates no longer accidentally enable assertions (was adding ~2MB and reducing performance). Now a multi-value option: `auto`/`no`/`yes`/`extra`.

---

### 9.6 Profiling on an Android Phone

`webgpu_tests/perf_guard/android/` drives Chrome on a USB-connected phone from the desktop: `adb reverse` so the phone loads `http://localhost:8099/` as a secure context (WebGPU without HTTPS), `adb forward` to `localabstract:chrome_devtools_remote` for raw CDP, instrumentation installed with `Page.addScriptToEvaluateOnNewDocument` before the engine's page runs. `probe.mjs` reports adapter/limits/renderer/fps; `fps_matrix.mjs` measures export variants cold; `gpu_time.mjs` reuses the desktop `gpu_time.mjs` timestamp-query instrumentation per command buffer or per pass, and can dump a shader module's WGSL (`--dump-wgsl`), substitute an edited one by exact code match (`--replace-pair`, since one label covers every material version of a scene shader), log the override constants each pipeline is created with (`--log-pipelines`) and request `shader-f16` (`--f16`). Exports must render on the main thread (`variant/separate_render_thread=false`): the separate render thread's pthread worker never returns to its event loop long enough to answer `Runtime.evaluate`. First results: `webgpu_notes/TASKS.md` §2.9.

---

## 10. Cross-Browser Compatibility

### Firefox/wgpu
| Issue | Solution |
|-------|----------|
| All bind group slots must be bound | Empty bind group pre-bound at gap indices |
| 8 storage buffers per stage (Metal limit) | Per-stage SSBO visibility via metadata comments |
| read_write storage in vertex shaders | Demote to read-only via string replacement |

### Chrome/Dawn
| Issue | Solution |
|-------|----------|
| No sized_binding_array | Flatten binding_array<T,N> → T at WGSL text level |
| CompositeAlphaMode_Opaque ignored | Strip alpha writes from BGRA8Unorm pipelines |
| shader-f16 not supported | Strip `enable f16;` directive |
| float32-blendable may be missing | Skip blend state when unavailable |

### Adreno GPUs (Android Chrome)
| Issue | Solution |
|-------|----------|
| No float32-filterable | Downgrade Float32 → Float16 for sample-only textures |
| No float32-blendable | Disable blending on float32 render targets |

### All Browsers
| Issue | Solution |
|-------|----------|
| No derivative uniformity guarantee | Prepend `diagnostic(off, derivative_uniformity)` |
| f32::MAX decimal overflow | Replace with hex float `0x1.fffffep+127f` |
| Limited storage texture formats | Promote R8/RG8/R16/RG16 to 32-bit |
| No texture component swizzle | Convert L8/LA8 to RGBA8 on CPU (once per texture at load); monochrome font atlases are rasterized as RGBA8 up front and skip it entirely |
| No 3-component texture formats | RGB8/RGBH/RGBF expanded to RGBA8 on CPU (once per texture at load) |
| No push constants | Ring buffer emulation |
| No combined image-samplers | SPIR-V-level split |
| No subpasses | Flatten to separate render passes |
| 256-byte row alignment | Applied in all buffer↔texture copies |

---

## 11. Diagnostics & Debugging

### Performance Counters

The driver maintains per-frame counters (reset each frame):
- Draw calls, set_bind_group calls (issued vs skipped)
- Push constant writes (issued vs skipped via firstInstance dedup)
- Render passes, bind group cache misses
- Pipeline/vertex buffer bind calls
- Gap bind group binds, first_instance draws

Logged to console every second via `EM_ASM` (always-on in current build).

### Shader Debugging

- JS monkey-patch on `device.createShaderModule` logs WGSL compilation errors
- `WEBGPU_VERBOSE` flag enables detailed WGSL dumps and bind group layout logging
- `dump_spirv_around_error`: hex dumps SPIR-V on parse failures
- SPIR-V cache hit/miss counters

### Device Lost Handler

Attached at initialization via JS `device.lost.then(...)`. Logs reason and message. No automatic recovery (page reload required).
