# Task 66: for TASKS.md

## §4 entry (suggested: §4.78 Asynchronous texture readback that waits for the data, October 2026)

On the web a GPU readback cannot block, so `Texture2D.get_image()` on a rendered texture returns `null` on its first
call and an older copy later (§2.13, §4.2 item 7.8); a project tested natively broke silently on the web (camera-sim's
download buttons, work task 66). Added `Texture2D.get_image_async(callback)` and
`RenderingServer.texture_2d_get_async(texture, callback)`: the callback runs once, deferred, on the main thread, with the
Image or `null` (invalid or freed texture, failed read). `RenderingServerDefault` issues queued requests after
`draw_viewports()`, so the image is what that frame drew, even from `_ready()`. `ViewportTexture`, `Texture2DRD`,
`ImageTexture`, `DrawableTexture2D`, `PortableCompressedTexture2D` override it; other textures call back with
`get_image()`. Await through a signal (`get_image_async(sig.emit); await sig`), documented.

Underneath, `RD::texture_get_data_async` / `buffer_get_data_async` now wait for WebGPU's `mapAsync`: previously
`_stall_for_frame()` read the staging buffers when the frame came round again whether the map had completed or not
(`fence_wait()` force-signals on WebGPU), so a slow frame got the zero shadow. A frame's downloads now go into an ordered
`DownloadBatch` queue; a batch whose staging buffers are still mapping (`RDD::buffer_get_readback_status()`, default `OK`,
WebGPU from the map state) waits with everything behind it, its staging blocks out of the download ring so nothing
copies into a buffer being mapped. Other drivers complete a batch in the same call that queued it; bit-identical natively.
A failed map calls back with an empty array and an error.

Tier `webgpu_tests/async_readback/` (49 checks: patterns at widths 1/3/96/333, RGBA8 and RGBA16F byte-exact, HDR 2D,
black/transparent frames, ordering across frames and textures, freeing while pending, buffers, ImageTexture, the await
pattern): native, Chrome and Firefox 49/49, debug and release templates. `gpu_time --uncapped` with a 1280x720 readback
every frame: 1.10 -> 1.34 ms/frame, GPU 0.04 -> 0.13 ms, no stall.

## §3 trap candidates

- **On WebGPU, `fence_wait()` does not mean the copy is readable** (2026-10-28, task 66). It force-signals so a frame
  never deadlocks, and `buffer_map()` on a staging buffer whose `mapAsync` has not resolved hands back the zero-filled
  shadow. Anything that reads a download after a fence on this backend must ask `buffer_get_readback_status()` first;
  RenderingDevice's async downloads do. A staging buffer must also not be recorded into while its map is pending (a
  validation error that drops the whole submit), which is why waiting batches take their blocks out of the ring.
- **A 1 px wide SubViewport is 2 px wide** (native and web): a readback test of width 1 must use an RD texture, and a
  ColorRect sized from the requested size leaves the second column unpainted.
- **The synchronous web `get_image()` alternates**: each consumed read starts a fresh copy and returns `null` until it
  lands (Chrome frames 5/6/7: null, data, null). A retry loop that calls it every frame sees data only every other frame.

## Questions for the user (defaults marked)

1. **Callable, or also an awaitable?** `get_image_async(callback)` follows `RenderingDevice.*_get_data_async`; `await`
   works through a script signal (documented). A built-in `await texture.get_image_async()` would need a per-request
   object with its own signal (a new class). **Default: keep the Callable only.**
2. **A one-time warning in debug builds when the synchronous `get_image()` returns null on the web?** §4.28 made the
   readback note verbose because warnings looked like failures; debug builds now have a real `console.warn` channel.
   **Default: no warning; the docs on `get_image()`, `Viewport.get_texture()` and `RD.texture_get_data()` point to the
   async method.**
3. **Should the synchronous `get_image()` on the web keep returning `null` (not zeros)?** It does today; the task text
   said "zero-filled". **Default: keep `null`** (a script can tell it from a black frame).

## Note for the camera-sim session

Landed on `work/66` (pending integration): `Texture2D.get_image_async(callback)`. In `scripts/main.gd`, `read_back()`
becomes:

```gdscript
signal _image_read(image: Image)

func read_back(texture: Texture2D) -> Image:
	texture.get_image_async(_image_read.emit)
	return await _image_read
```

It returns the frame drawn in the frame you call it (so the click's own frame, no `frame_post_draw` wait), arrives
1-2 frames later natively and 1-5 on the web, and is `null` only if the texture is freed or the read fails, so the
download buttons keep their `image == null` message and lose the 30-frame retry and the alpha test (black frames now
come back as black). Only one capture per signal may be in flight; the two buttons can share one if a capture finishes
before the next starts, otherwise give each its own signal or use a lambda:
`texture.get_image_async(func(image): download_png(image, "camera-sim-screenshot"))`. For the histogram, if task 62
has not landed, request one `get_image_async()` of the small readback texture every 0.25 s and use the image in the
callback, instead of reading the previous `get_image()`. Needs editor and web templates rebuilt from the integrated tree.
