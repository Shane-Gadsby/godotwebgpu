# Task 66: an asynchronous texture readback that waits for the real pixels, on every platform

Branch `work/66`, worker-66, 2026-10-28.

## What changed

**Script API**
- `Texture2D.get_image_async(callback)`: calls `callback(image)` exactly once, later, on the main thread, with an `Image`
  of the pixels, or `null` when the texture is invalid, freed before the data arrives, or the read fails. Base class:
  `callback.call_deferred(get_image())`. `ViewportTexture`, `Texture2DRD`, `ImageTexture`, `DrawableTexture2D` and
  `PortableCompressedTexture2D` override it with the new `RenderingServer.texture_2d_get_async(texture, callback)`.
  Callable rather than awaitable, as `RenderingDevice.*_get_data_async` are; `await` is documented with a signal
  wrapper (`get_image_async(image_read.emit); var image = await image_read`), and the test project uses that pattern.
- **When the copy is made:** `RenderingServerDefault` queues the request and issues it after `draw_viewports()`, so the
  image is what *this* frame rendered, even from `_ready()`. (Synchronous `get_image()` reads before this frame draws,
  i.e. the previous frame.)
- `RendererTextureStorage::texture_2d_get_async()` (pure virtual): RD builds the Image from
  `RD::texture_get_data_async()` with everything captured at request time (`_texture_2d_image_from_data()`, factored out
  of `texture_2d_get()`, which is otherwise unchanged); GLES3 and the dummy storage read synchronously and call back
  deferred.

**RenderingDevice `texture_get_data_async` / `buffer_get_data_async` on WebGPU.** What the driver did before: they
already ran (the fork's `_end_frame()` starts a `mapAsync` on every download staging buffer right after the submit),
but `_stall_for_frame()` read the staging buffers when the frame came round again whether or not the map had completed.
`fence_wait()` on WebGPU force-signals, so on a slow frame the callback got the zero-filled (or stale) shadow, and the
staging block went back into the download ring while still mapping. Now:
- New driver hook `RDD::buffer_get_readback_status(BufferID)` -> `OK` / `ERR_BUSY` / `FAILED`, default `OK` (every other
  driver). WebGPU implements it from `map_complete` / `map_pending` / `wgpuBufferGetMapState()`, plus a new
  `WGBuffer::map_failed` set by the map callback.
- `_stall_for_frame()` moves the frame's download lists into a `DownloadBatch` queue and processes the queue in order. A
  batch whose buffers are still mapping waits (with every batch behind it, so callbacks keep their order), and its
  staging blocks are taken **out of the download ring** while it waits, so nothing records a copy into a buffer being
  mapped; they return to the ring (or are freed if the ring grew to its limit) once it completes. A failed map calls
  back with an empty array and an `ERR_PRINT`. A waited batch whose texture needs format conversion and was freed calls
  back empty instead of touching the freed driver texture. On every non-WebGPU driver a batch completes in the same
  `_stall_for_frame()` that queued it: same callbacks, same point, same order as before.
- Shutdown frees blocks held by waiting batches (callbacks dropped).

**Docs** (`doc/classes`): `Texture2D.get_image_async` (new, with the await pattern), `RenderingServer.texture_2d_get_async`
(new), web notes on `Texture2D.get_image`, `RenderingServer.texture_2d_get`, `Viewport.get_texture`,
`RenderingDevice.texture_get_data` / `buffer_get_data` (first-read behavior) and both `*_get_data_async` (can be a
frame or more later on WebGPU, always the real data). No debug-only warning added (for-tasks.md, question 2).

**Test tier** `webgpu_tests/async_readback/`: `project/` (one GDScript scene, 49 checks), `run_native.sh` (xvfb, Vulkan),
`export.sh [debug|release]` (xvfb + Vulkan export, not headless), `run_web.mjs --browser chrome|firefox` (asserts the
adapter first, exit 3 if refused). User args `mode=perf_none|perf_readback` for gpu_time.

## Verification (actual numbers)

The 49 checks: pattern SubViewports 1(=2)/3/96/333 x 7 and 256 x 64 requested in `_ready()` and at frame 10 (pixels match
the pattern of the frame the request was made in, exactly); HDR 2D SubViewport 333 x 7 (RGBH, within one 8-bit step);
fully transparent (all-zero) and opaque-black viewports come back as real images; 8 outstanding requests over 3 frames
on 3 textures (right pixels, callbacks in request order); `Texture2DRD` and `RD.texture_get_data_async` at widths 1, 3,
96, 333 in RGBA8 and RGBA16F (byte-exact); `buffer_get_data_async` of a 333-byte range and a second request after a
`buffer_update` in the same frame (byte-exact, the second sees the update); freeing a viewport the same frame / the next
frame, and freeing a `Texture2DRD`'s RD texture the next frame (callback fires once with `null`, no crash, no error);
`ImageTexture`; the documented `await` pattern; every callback exactly once.

| run | result | callback latency (frames) |
|---|---|---|
| native Vulkan, Forward+ (dev_mode editor) | **49/49** | 0-2 |
| Chrome (nvidia/lovelace, 48 textures), template_debug | **49/49** | 0-5 |
| Firefox, template_debug | **49/49** | 0-3 |
| Chrome, template_release | **49/49** | |
| Firefox, template_release | **49/49** | |

The 1 px wide SubViewport is 2 px wide (native and web alike); width 1 is covered by the RD textures. Raw results:
`results_debug.txt`, `results_release.txt`, `perf_and_release_run.txt`.

Synchronous `get_image()` on the web, as recorded by the test (frames 5/6/7): Chrome `null, data, null`, Firefox
`null, null, data` (each consumed read starts a new copy); native: data every time. It returns `null`, not a zero-filled
image, so the docs say `null`.

HDR 2D: the readback (RGBH) holds the shader's 8-bit pattern values as they are, not linearized, natively and on the web
alike (the `Viewport.get_texture()` doc says "linear encoding"; this canvas shader writes `COLOR` unshaded). Parity holds;
not investigated further.

**gpu_time.mjs --uncapped** (release export, Chrome, 1280 x 720; a `get_image_async()` of the main viewport every frame
vs none; two runs each): 1.09 / 1.11 ms per frame without, 1.34 / 1.33 ms with (913/897 vs 747/754 fps); GPU busy 0.04
-> 0.13 ms/frame. About 0.24 ms per readback, no stall. (That export loaded `translated 332, baked 0`; irrelevant here.)

**Bit-identical, existing behavior (native):** a Forward+ scene (shadows, SSAO, glow) plus a 2D SubViewport saved with
the synchronous `get_image()`, plus a synchronous `buffer_get_data()`, at frame 40: pre-change editor, post-change editor
and the pre-change editor again give identical PNGs (md5 `4ca04469...`, `2d4c536c...`) and identical bytes.

**Builds:** editor with `dev_mode=yes`: clean. Web `template_debug` and `template_release` (nothreads, no dlink,
raycast): clean. `pre-commit` on every changed file: clean (codespell fixed two spellings in the task file).

## Skipped (user's test scope)

Forward+ matrix, scene smoketest, perf_guard, `smoke.sh` (no shader or interface change), threaded and dlink web
templates, the test project on native GLES3/Compatibility, Android. No pre-change web template was built, so "the old
WebGPU async path could hand back zeros" rests on the code (`fence_wait()` force-signals; `buffer_map()` returns the zero
shadow when the map is pending), not on a failing run.

## Risks

- `RenderingDevice::_stall_for_frame()` is restructured (its upstream body moved into `_download_batch_complete()`): an
  upstream sync touching it conflicts there.
- New pure virtual `RendererTextureStorage::texture_2d_get_async`; new non-pure `RDD::buffer_get_readback_status`.
- Task 62's histogram (`buffer_get_data_async` in `camera_finish.cpp`, uncommitted on work/62) gets this for free: its
  "counts that do not add up are dropped" guard should stop triggering on WebGPU, and is harmless to keep.
- A waiting batch keeps its blocks out of the ring, so heavy readback on a slow web GPU grows the ring (new blocks up to
  `staging_buffer/max_size_mb`; past that, upstream's stall path applies as before).
