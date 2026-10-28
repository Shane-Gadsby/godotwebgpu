# Dynamic 3D resolution: design (task 68)

Status: design for the user's decision. No engine code was changed in this round. The numbers below
come from a throwaway bench (`proto/` beside this file: `main.gd`, `heavy.gdshader`, `run_web.mjs`) run on
the RTX 4080 SUPER, 2560x1440 at 165 Hz, Chrome 156 (Vulkan) and Playwright's Firefox, and natively
on Vulkan, with the lead's `bin/` editor and nothreads web templates of `1c3d11cfd4`.

## 1. What exists today

**Changing `scaling_3d_scale` is a full reconfigure.** `RendererViewport::viewport_set_scaling_3d_scale()`
calls `_configure_3d_render_buffers()`, which calls `RenderSceneBuffersRD::configure()`, whose first act
is `cleanup()`: every internal-size texture is freed and recreated, and every `RenderBufferCustomDataRD`
gets `free_data()` + `configure()`. Concretely, on every scale change:

| state | where | what a change does to it today |
|---|---|---|
| colour/depth/MSAA/velocity, SSAO/SSIL/SSR/SSCS buffers, cluster builder | `RenderSceneBuffersRD`, `RenderBufferDataForwardClustered::free_data()` | freed and reallocated at the new size |
| **FSR 2 context** | `RenderBufferDataForwardClustered::fsr2_context` | `memdelete`d; `ensure_fsr2()` creates a new one next frame with `maxRenderSize` = the *new* internal size. New context = `firstExecution` = accumulation reset (`ffx_fsr2.cpp:822`): the upscaled history (display size) is thrown away |
| TAA history | named texture `taa/history` | freed; `TAA::process()` reseeds it from the current frame (no flash, but the frame and the next few are un-accumulated: aliasing/shimmer returns until it re-converges) |
| **auto-exposure luminance** | `Luminance::LuminanceBuffers::current` | freed and recreated **cleared to 0**; `luminance_reduce.glsl` then adapts from `prev_lum = 0` (clamped to the min sensitivity) at `auto_exposure_speed` — an exposure pump on every change. `set_immediate` is not raised because the camera attributes' version did not change |
| SDFGI / VoxelGI / volumetric fog | per-viewport, but fog's froxel buffers follow the internal size | fog: reallocated (temporal reprojection restarts) |
| samplers (mip bias = log2(scale)) | `update_samplers()` | recreated (cheap) |

FSR 2 itself is built for dynamic resolution: a context made with `maxRenderSize` accepts any
`renderSize` up to it per dispatch (`ffx_fsr2.cpp:263`, `constants.renderSize` vs `maxRenderSize`), and
its history is at display size. Godot simply never uses that: it destroys the context instead.

**GPU time.** `RenderingServer.viewport_set_measure_render_time()` / `viewport_get_measured_render_time_gpu()`
bracket a viewport's draw with `capture_timestamp("vp_begin_<id>")`/`("vp_end_<id>")`; the results are read
when the frame slot is reused, i.e. 2-3 frames late. Vulkan, D3D12 and Metal implement RD timestamps;
GLES3 only over desktop GL (`glQueryCounter`, not GLES/WebGL). **On WebGPU the driver has timestamp
readback switched off** (`rendering_device_driver_webgpu.cpp:1341`, "mapAsync can get stuck pending"),
so the API returns 0. Two more problems in that driver path: it writes timestamps with
`wgpuCommandEncoderWriteTimestamp`, i.e. `GPUCommandEncoder.writeTimestamp()`, which is not in the
WebGPU spec (see §3 for which browsers have it), and it maps the readback buffer every frame.

**camera-sim's Auto** (`scripts/main.gd` 1027-1215) resizes its SubViewport through a candidate list
(Match screen, 1440p, 1080p, 900p, 720p), settling 1 s + 10 frames per change (3 s after startup),
passing a size when the median of 60 frames is <= 1/55 s and at most a quarter miss 25 ms, failing early,
and saving the most pixels that passed per machine/browser/window mode in `user://display.cfg`.
Its project uses TAA and no engine auto exposure.

MEASUREMENTS_PLACEHOLDER
