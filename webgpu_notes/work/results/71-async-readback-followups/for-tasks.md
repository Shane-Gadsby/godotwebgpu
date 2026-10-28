# Task 71: for TASKS.md

## §4 entry (suggested: §4.8x Awaitable texture readback and the web's debug warning, October 2026)

Follow-up to §4.82 from the user's answers on the hand-offs page. `Texture2D.get_image_awaitable()` and
`RenderingServer.texture_2d_get_awaitable(rid)` return a `Signal` emitted once with the `Image` (or `null`), so
`var image: Image = await texture.get_image_awaitable()` works natively and on the web; each call has its own signal (a
plain `RefCounted` with a user signal, kept alive only by the read's callback, `RenderingServer::create_image_request()`),
so any number can be in flight. The Callable forms are unchanged. On the web, a debug build's synchronous `get_image()` /
`texture_2d_get()` that returns `null` because the readback has not arrived prints one `WARN_PRINT` (`console.warn`) per
texture pointing at the async API (`RendererRD::TextureStorage::texture_2d_get()`, `DEBUG_ENABLED && WEB_ENABLED`; not
for invalid or non-copyable textures, which keep their own errors); release templates are silent. Tier
`webgpu_tests/async_readback/` now 56 checks plus the warning count: native, Chrome and Firefox 56/56 with debug and
release templates; warnings exactly 96x7 + 333x7 in debug, none in release, none natively.

## §3 trap candidates

- **A `Signal` returned from a C++ method is awaitable only while its object lives**: a `Signal` holds an ObjectID, not a
  reference (from the code, not a failing run), so a per-request `RefCounted` whose only reference is the returned `Signal` is freed before the `await`
  connects. Keep it alive from the thing that will emit it (here, a `Ref` bound into the read's callback).

## Questions for the user (defaults marked)

1. **Name of the awaitable**: `get_image_awaitable()` / `texture_2d_get_awaitable()`. Alternatives: `read_image()`, or
   an object form `await texture.request_image().completed` (SceneTreeTimer-style). **Default: keep
   `get_image_awaitable()`.**
2. **Warn for `RenderingDevice.texture_get_data()` too?** It returns empty on the web the same way, but it is the
   low-level API and the task named only `get_image()` / `texture_2d_get()`. **Default: no.**

## Note for the camera-sim session (supersedes task 66's)

Landed on `work/71` (pending integration). `read_back()` in `scripts/main.gd` needs no signal of its own any more:

```gdscript
func read_back(texture: Texture2D) -> Image:
	return await texture.get_image_awaitable()
```

or inline: `var image: Image = await get_viewport().get_texture().get_image_awaitable()`. It returns the frame drawn in
the frame you call it, natively 1 frame later and 1-3 on the web, `null` only if the texture is freed or invalid. Both
download buttons can capture at once (each call has its own signal), so task 66's "one capture per signal" note no longer
applies. Any synchronous `get_image()` left in the project now warns once per texture in a debug web export
(`console.warn`, "returned null for render target WxH"), which is how to find the ones still to convert. Needs the
editor and web templates rebuilt from the integrated tree.
