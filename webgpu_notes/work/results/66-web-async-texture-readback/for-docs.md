# Task 66: for the docs

## Wiki (Engine Additions): "Reading pixels back from the GPU: get_image_async()"

On the web, the browser cannot make the CPU wait for the GPU. So `Texture2D.get_image()` on a texture the GPU draws (a
`ViewportTexture` from `Viewport.get_texture()`, a `Texture2DRD`, a `DrawableTexture2D`, an `ImageTexture` in an exported
game) returns `null` the first time, and later returns a copy that is a frame or more old. Natively it returns the
pixels at once, so a project tested on the desktop can break on the web without an error.

Use `get_image_async()` instead. It works the same on every platform:

```gdscript
func save_screenshot():
	get_viewport().get_texture().get_image_async(func(image: Image):
		if image:
			image.save_png("user://screenshot.png"))
```

- The callback runs once, a few frames later, on the main thread.
- The image is what was drawn in the frame you called it, even from `_ready()`, so you do not need to await
  `RenderingServer.frame_post_draw` first.
- It is `null` only if the texture is freed before the pixels arrive or the read fails. A black frame comes back as a
  black image.
- Several requests finish in the order you made them.

To `await` it, let the callback emit a signal:

```gdscript
signal image_read(image: Image)

func capture(texture: Texture2D) -> Image:
	texture.get_image_async(image_read.emit)
	return await image_read
```

Keep one request per signal in flight: everything awaiting the signal resumes on its next emission.

Lower level: `RenderingServer.texture_2d_get_async(rid, callback)`, and `RenderingDevice.texture_get_data_async()` /
`buffer_get_data_async()`, which on the web now always deliver the real data (sometimes a frame later than natively).

Cost: reading back a 1280x720 frame every frame costs about 0.25 ms per frame in Chrome on a desktop GPU, without
stalling it.

## drivers/webgpu/README.md (the "Synchronous readback" bullet)

- **Synchronous readback** -- Not available in WebGPU. The first synchronous read of a texture returns empty (`null`
  Image) and of a buffer zeros; later reads return the previous copy. RenderingDevice's asynchronous downloads
  (`texture_get_data_async`, `buffer_get_data_async`, and `Texture2D.get_image_async` on top) wait for the staging
  buffer's `mapAsync` through `buffer_get_readback_status()`, holding the batch (and its staging blocks, out of the
  download ring) until it has, so their callbacks always get the real data.

## CLAUDE.md, "Buffer mapping" decision (one sentence to append)

`RDD::buffer_get_readback_status()` (default `OK`) is how shared code asks whether a download's staging buffer can be
read yet; RenderingDevice's asynchronous downloads wait on it instead of trusting `fence_wait()`, which on WebGPU
force-signals.
