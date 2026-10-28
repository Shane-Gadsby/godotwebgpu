# For TASKS.md (task 77)

## §4 entry

### 4.9x RenderingDevice synchronous readback warning on the web (October 2026) *(work task 77; user answer `warn-rd-texture-get-data`)*

Debug web builds print one `WARN_PRINT` (console.warn) per texture or buffer when a script's synchronous
`RenderingDevice.texture_get_data()` returns an empty array or `buffer_get_data()` returns zeros because the web's
asynchronous readback has not arrived yet, pointing at `texture_get_data_async()` / `buffer_get_data_async()` /
`get_image_awaitable()`. Only the script bindings warn (`_texture_get_data_bind()` / `_buffer_get_data_bind()`, which
read an optional `r_not_ready` out-parameter of the C++ methods), so engine reads such as `texture_2d_get()` (task 71's
own warning) never double up; invalid resources keep their own errors; release and native only forward.
`webgpu_tests/async_readback/` gained 8 checks (sync RD reads return the data natively) and counts the four expected
warnings. Worker: 68/68 natively, in Chrome and Firefox with debug and release templates; debug exactly 41x5, 17x3,
100 B, 52 B; release and native none; 0 validation messages.

## Trap candidates

- `buffer_get_data()` on WebGPU returns **zeros**, not an empty array, on a first read: `buffer_get_data_direct()`
  returns false both for "not handled" and "not ready", and RD then runs the generic staging copy, which cannot be mapped
  synchronously (task 77). Anything that tests "no data yet" on a buffer by emptiness misses it.
- A lease-dropping `trap` with a relative path in a script that `cd`s later never drops the lease (cost a few minutes,
  lease left held; use the absolute path).

## Questions for the user

- `web-buffer-get-data-empty` (choice): on the web, a first synchronous `RenderingDevice.buffer_get_data()` returns
  zeros of the requested size (and wastes a staging copy that can never be read). Should it return an empty array instead,
  like `texture_get_data()`?
  - **Keep zeros (default)**: no behavior change; scripts that index the result keep working; the debug warning says so.
  - Return empty: consistent with textures and avoids the pointless copy, but a script doing `bytes.decode_float(0)`
    on the first frame gets an error instead of 0.
