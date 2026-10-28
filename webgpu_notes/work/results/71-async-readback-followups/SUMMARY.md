# Task 71: an awaitable texture readback, and a debug-only web warning for the synchronous null

Branch `work/71`, worker-71, 2026-10-28. Follow-up to task 66 (TASKS §4.82), from the user's answers
`async-readback-awaitable` = "Add an awaitable" and `async-readback-null-warning` = "Debug-only warning".

## The API (for task 72 and camera-sim)

```gdscript
var image: Image = await texture.get_image_awaitable()                       # any Texture2D
var image: Image = await RenderingServer.texture_2d_get_awaitable(texture_rid)  # an RS texture RID
```

- `Texture2D.get_image_awaitable() -> Signal` and `RenderingServer.texture_2d_get_awaitable(texture: RID) -> Signal`.
  Each call starts the same read as `get_image_async()` / `texture_2d_get_async()` and returns a `Signal` emitted
  **once** with the result: an `Image` of what *this* frame drew, or `null` (invalid or freed texture, failed read).
  Same timing (natively 0-1 frames later, web 1-3), same ordering as the Callable form.
- Every call has its own signal, so any number of reads can be awaited at once, from any number of functions (no
  "one request per signal" rule as with task 66's documented signal wrapper).
- Await it right away, in the same function: it is emitted once, on a later frame (never synchronously, so there is no
  race between the call and the `await`).
- Nothing needs to hold a reference: the request object is kept alive by the callback only.
- `get_image_async(callback)` and `texture_2d_get_async(rid, callback)` are unchanged.
- Works for every `Texture2D`, including script-implemented ones (base class path: `get_image()` deferred).

Shape chosen: a method returning a `Signal`, rather than a new class (`SceneTreeTimer`-style `await x.request().completed`),
because it gives exactly `var image = await texture.method()`. Under the hood
(`RenderingServer::create_image_request()`, static, shared by both): a plain `RefCounted` with a `completed(image)`
user signal; the read's callback is a `callable_mp_static` bound to that `Ref`, so the request lives exactly until it is
emitted. GDScript's analyzer treats a `Signal`-typed call as awaitable (no REDUNDANT_AWAIT warning) and types the result
Variant, so `var image: Image = await ...` works with static typing.

## The warning

`RendererRD::TextureStorage::texture_2d_get()`, under `#if defined(DEBUG_ENABLED) && defined(WEB_ENABLED)`: when
RenderingDevice returns no data for a texture that is valid and has `TEXTURE_USAGE_CAN_COPY_FROM_BIT` (i.e. the web's
async readback has not arrived yet), `WARN_PRINT` once per texture (flag on the `Texture`):

> WARNING: Texture2D.get_image() / RenderingServer.texture_2d_get() returned null for render target 96x7: on the web, GPU
> pixels are read back asynchronously and had not arrived yet (each call starts a new copy). Use
> Texture2D.get_image_async(), "await texture.get_image_awaitable()" or RenderingServer.texture_2d_get_async() to wait
> for them. Shown once per texture, in debug builds only.

`WARN_PRINT` reaches `console.warn` through WebLogger (CLAUDE.md's web logging note), verified as `[warning]` in both
browsers. Textures that fail for a real reason (an invalid RID: `Parameter "tex" is null`; no copy-from usage: RD's own
error) print their existing error and no warning. Release templates compile the code out. Natively nothing changes.

Docs: `Texture2D.get_image_awaitable` (new, with an example), `RenderingServer.texture_2d_get_awaitable` (new);
`get_image_async`'s signal-wrapper example replaced by a pointer to the awaitable; web notes on `Texture2D.get_image`,
`RenderingServer.texture_2d_get` and `Viewport.get_texture` mention the awaitable and the debug warning. `--doctool .`
run, no unrelated file changed.

## Verification (actual numbers)

Tier `webgpu_tests/async_readback/` extended: 7 new checks (two awaits of the same viewport texture in flight at once,
`RenderingServer.texture_2d_get_awaitable()` of a viewport's RS texture, an invalid RID (null), a viewport freed the same
frame (null, no hang), a script-implemented `Texture2D` (base-class path, byte-exact), the inline typed one-liner; each
signal emitted exactly once, counted by a second connection), plus the warning check in the runners:
`run_web.mjs` requires exactly one `console.warn` for each of the two synchronously read textures (96x7, 333x7) in a debug
export and none in a release export (the project prints `debug=` so the runner knows); `run_native.sh` requires none.

| run | checks | sync-readback warnings | awaitable latency (frames) |
|---|---|---|---|
| native Vulkan, Forward+ (dev_mode editor) | **56/56** | 0 (expected 0) | 0-1 |
| Chrome (nvidia/lovelace, 48), template_debug | **56/56** | 2: 96x7, 333x7, as `warning` -> ok | 0-1 |
| Firefox, template_debug | **56/56** | 2: 96x7, 333x7, as `warning` -> ok | 0-2 |
| Chrome, template_release | **56/56** | 0 -> ok | 0-3 |
| Firefox, template_release | **56/56** | 0 -> ok | 0-2 |

(0 frames = null cases and the script texture, which call back the next idle.) Raw output: `test_results.txt`.

**Builds:** editor `dev_mode=yes` (accesskit, webgpu, gdscript_braces, mold): clean, 231 s. Web `template_debug` and
`template_release` (nothreads, no dlink, raycast): clean. `pre-commit run --files` on every changed file: clean.

## Skipped (user's test scope)

Forward+ matrix, scene smoketest, perf_guard, `smoke.sh` (no shader or interface change), threaded/dlink templates, the
bit-identical check (no existing code path changes behavior: the warning is compiled out natively and in release, and the
new methods are additive).

## Risks

- `RenderingServer` gains a non-virtual method and a static helper (`create_image_request`); no new class, no change to
  `get_image_async` or its overrides. GDExtension API: two new methods only.
- The warning is keyed off `RD::texture_get_data()` returning empty for a valid, copyable texture. The only other way that
  happens on the web is the WebGPU driver failing to create its staging buffer, which prints its own error first.
- An awaitable whose texture never calls back would keep its request alive; every `get_image_async` path calls back
  exactly once (task 66), so that cannot happen with the engine's textures. A script `Texture2D` that overrides
  `get_image_async` itself (not possible: it is not a virtual exposed to scripts) is not a concern.
