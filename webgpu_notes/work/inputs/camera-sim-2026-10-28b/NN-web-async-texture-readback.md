# NN-web-async-texture-readback: a texture readback that tells you when the pixels are real, on every platform

State: open
Owner:
Claimed:
Needs: build (editor + web template), browser (Playwright), native Vulkan run
Branch: work/NN
Results: webgpu_notes/work/results/NN-web-async-texture-readback/

User request, 2026-10-28, from the camera-sim session.

## The problem

On the web build, a GPU readback can't block (single-threaded wasm, WebGPU `mapAsync`). So
`Texture2D.get_image()` on a rendered texture (a `ViewportTexture`, `Viewport.get_texture()`, a `Texture2DRD`)
returns **a zero-filled image the first time**, and the real pixels arrive a frame or more later. That's by design
(TASKS §2.13 / §4.2, item 7.8). Since §4.28 the only notice is a `print_verbose`, so nothing tells the project.
Natively the same call returns the real pixels at once. A project written and tested natively therefore breaks
on web with no error: before camera-sim worked around it, its two download buttons did nothing, and release
builds crashed with `null function`.

Every web project that reads pixels back has to guess. camera-sim's workaround (`scripts/main.gd` `read_back()`)
calls `get_image()` up to 30 frames in a row until the centre pixel's alpha is non-zero. That only works because
its images are opaque, and it can't tell a black frame from one that hasn't arrived. Its histogram reads the
previous request's image every 0.25 s for the same reason.

## Goal

1. **An asynchronous readback that works the same everywhere**, so the same script is correct natively and on
   web, e.g.

   ```gdscript
   texture.get_image_async(func(image: Image): ...)   # or: var image = await texture.get_image_async()
   ```

   on `Texture2D` (at least `ViewportTexture` and `Texture2DRD`; `ImageTexture` can call back at once). The
   callback gets the **real** pixels of the frame that was current when it was called, or `null` with an error
   if the texture is freed or the readback fails. Natively, `RenderingDevice.texture_get_data_async` (upstream
   4.4+) is the natural back end; on WebGPU, the existing shadow-copy and `mapAsync` path, finishing on the
   promise rather than on a later `get_image()` call. If the engine's convention prefers a Callable to an
   awaitable, take the Callable and document `await` with a signal wrapper.
2. **`RenderingDevice.texture_get_data_async` and `buffer_get_data_async` work on WebGPU**, if they don't already,
   so lower-level code (compute readbacks, the histogram readback in NN-camera-histogram-readback) has a correct
   path too. Report what the driver does today.
3. **Make the synchronous call's web behaviour discoverable:** document in `Texture2D.get_image()`,
   `Viewport.get_texture()` and `RenderingDevice.texture_get_data()` that on web a rendered texture's first read
   is zero-filled, and point to the async method. Optionally, an `Engine`/`OS` query or a one-time warning in
   debug builds only, so released games stay quiet (the reason §4.28 made it verbose).
4. **Row alignment:** camera-sim chose a 128 px wide readback because at 96 px (384-byte rows) a WebGPU readback
   once came back garbled; WebGPU copies need 256-byte row alignment. The driver now pads rows (`aligned_bpr`
   in `rendering_device_driver_webgpu.cpp`). Add a test of odd widths (1, 3, 96, 333 px) to the new path, so
   the async readback is right at any size.

## Verification

- A test project that renders a known pattern into a SubViewport and calls `get_image_async()` on the first
  frame and on a later one: the callback's pixels match the pattern exactly, natively and on web, and it fires
  once per call.
- A black frame comes back as black with a real callback, not mistaken for "not arrived".
- Several outstanding requests on the same and on different textures complete in order with the right pixels.
- Odd widths (1, 3, 96, 333 px), RGBA8 and an HDR format (RGBA16F), on WebGPU.
- Freeing the texture or viewport while a request is pending: the callback gets `null` (or doesn't fire,
  documented), with no crash or leak.
- `gpu_time.mjs --uncapped`: a readback every frame doesn't stall the frame (compare against none).

## camera-sim afterwards

Hand this back to the camera-sim session when it lands. It will replace `read_back()`'s retry loop (Download Image
and Download Screenshot) with `await texture.get_image_async()`. If NN-camera-histogram-readback hasn't landed,
the histogram will request one async readback every 0.25 s instead of reading the previous `get_image()`.
