# Task 77: debug web warning for RenderingDevice.texture_get_data() / buffer_get_data()

Branch `work/77`, worker-77, 2026-10-29. User answer `warn-rd-texture-get-data` = "Yes, warn too". Follow-up to task 71
(TASKS §4.84).

## What changed

`servers/rendering/rendering_device.{h,cpp}`:
- `texture_get_data()` and `buffer_get_data()` take an optional `bool *r_not_ready = nullptr`. Set when the web's
  asynchronous readback has not arrived yet: for textures, the WebGPU driver path (`API_TRAIT_TEXTURE_GET_DATA_VIA_DRIVER`)
  returned nothing; for buffers (under `WEB_ENABLED`), `buffer_get_data_direct()` returned false, after which the
  staging fallback can only hand back zeros. Every existing caller passes nothing, so nothing changes for them.
- The script bindings now go to `_texture_get_data_bind()` / `_buffer_get_data_bind()` (same names, args and defaults,
  so the API and GDExtension hashes are unchanged). Under `#if defined(DEBUG_ENABLED) && defined(WEB_ENABLED)` they
  `WARN_PRINT` once per texture/buffer (flag on RD's `Texture` / `Buffer`); otherwise they just forward. Only the
  bindings warn, so engine-internal reads (e.g. `TextureStorage::texture_2d_get()`, which has task 71's own warning)
  never print this one, and nobody gets two warnings for one `get_image()`.
- Invalid RID, no copy-from usage, bound texture, bad layer: RD's own error, never "not ready", no warning.

Messages (console.warn through WebLogger):
> RenderingDevice.texture_get_data() returned no data for texture 41x5 (layer 0): on the web, GPU pixels are read back
> asynchronously and had not arrived yet (each call starts a new copy). Use RenderingDevice.texture_get_data_async() to
> wait for them, or "await texture.get_image_awaitable()" for a Texture2D. Shown once per texture, in debug builds only.

> RenderingDevice.buffer_get_data() returned zeros for buffer of 100 bytes: on the web, GPU data is read back
> asynchronously and had not arrived yet (once it has, a call returns what an earlier call copied). Use
> RenderingDevice.buffer_get_data_async() to wait for it. Shown once per buffer, in debug builds only.

`buffer_get_data()` does have the first-read behavior, but as **zeros**, not an empty array (as its doc already says):
the driver returns false while its map is pending and RD falls through to the generic staging copy, which on WebGPU can
never be mapped synchronously. Behavior kept; see the question in for-tasks.md.

Docs: one sentence on each of `RenderingDevice.texture_get_data` / `buffer_get_data`'s existing web notes.

Tier `webgpu_tests/async_readback/`: two RD textures (41x5, 17x3, RGBA8, copy-from) and two storage buffers (100, 52
bytes), read synchronously at frames 5, 6, 7 (41x5, 100 B) or 5 only; natively each read must return the data (8 new
checks), on the web what came back is recorded as information. Frame 8 adds RD reads of an invalid texture RID, the
no-copy-from texture and an invalid buffer RID (errors, no warning). `run_web.mjs` requires exactly the four RD
warnings, as `warning`, in debug and none in release (task 71's two get_image() warnings still checked separately);
`run_native.sh` requires none. `Buffer is either invalid` added to the expected console errors.

## Verification (actual numbers; raw output in test_results.txt)

| run | checks | get_image warnings | RD warnings |
|---|---|---|---|
| native Vulkan, Forward+ (dev_mode editor) | **68/68** | 0 (expected 0) | 0 (expected 0) |
| Chrome (nvidia/lovelace, 48), template_debug | **68/68** | 2 -> ok | 4: 41x5, 17x3, 100 B, 52 B, as `warning` -> ok |
| Firefox, template_debug | **68/68** | 2 -> ok | 4, as `warning` -> ok |
| Chrome, template_release | **68/68** | 0 -> ok | 0 -> ok |
| Firefox, template_release | **68/68** | 0 -> ok | 0 -> ok |

0 WebGPU validation messages and 0 unexpected console errors in all four web runs. Web sync reads, frame 5/6/7: textures
empty then data on frame 6 or 7 (and empty again after data, each data return consumes the copy); buffers zeros then
data on frame 6 or 7.

The existing 60 checks all pass in every run (68 = 60 + 8 new native-data checks).

**Builds:** editor `dev_mode=yes` (accesskit, webgpu, gdscript_braces, mold): clean. Web `template_debug` and
`template_release` (nothreads, no dlink, raycast): clean; the warning text is in the debug wasm, not in the release
wasm. `pre-commit run --files` on every changed file: clean.

## Skipped (user's test scope)

Forward+ matrix, scene smoketest, perf_guard, `smoke.sh` (no shader or interface change), threaded/dlink templates.
The bit-identical check: natively and in release the bindings only forward to the unchanged methods (`r_not_ready`
null); nothing else changed.

## Risks

- A WebGPU driver error inside `texture_get_data` (staging buffer creation failed) also returns empty and would warn
  once besides its own error. Harmless.
- `buffer_get_data` on a buffer whose driver handle is null returns false from `buffer_get_data_direct` too (it prints
  its own error first); same.
