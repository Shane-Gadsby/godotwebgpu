# Task 71: for the shared docs

The class reference is updated on the branch (`doc/classes/Texture2D.xml`, `RenderingServer.xml`, `Viewport.xml`).
Nothing in `CLAUDE.md`, `drivers/webgpu/README.md` or `webgpu_site/*` must change. Suggested additions:

## Wiki, "Engine Additions" (the page that documents `get_image_async()`, if task 66's for-docs put one there)

Add after the Callable form:

> **Awaiting the image.** `Texture2D.get_image_awaitable()` and `RenderingServer.texture_2d_get_awaitable(rid)` start the
> same read and return a `Signal` emitted once with the `Image` (or `null`):
>
> ```gdscript
> var image: Image = await get_viewport().get_texture().get_image_awaitable()
> ```
>
> Every call has its own signal, so any number of reads can be awaited at once. Await it in the same function; the
> signal fires once, on a later frame.
>
> **The web's debug warning.** In a debug export, a synchronous `get_image()` / `RenderingServer.texture_2d_get()` that
> returns `null` because the pixels have not come back from the GPU yet prints one `console.warn` per texture, naming the
> async methods. Release exports print nothing.

## `webgpu_site/CORRECTNESS_AND_COMPATIBILITY.md` (where it describes the web's first-null `get_image()`)

One sentence: "Debug exports warn once per texture when this happens (`WARN_PRINT`, so `console.warn`); release exports
stay silent. The fix in a project is `await texture.get_image_awaitable()` or `get_image_async(callback)`."

## `webgpu_tests/README.md`, the async_readback tier line

"... plus the awaitable forms (task 71) and the debug-only sync-readback warning: `run_web.mjs` fails a debug export
that does not warn exactly once for each of the two synchronously read textures, and a release export that warns at
all; `run_native.sh` fails on any such warning natively."
