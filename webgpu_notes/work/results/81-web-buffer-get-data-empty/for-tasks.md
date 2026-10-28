# For TASKS.md (task 81)

## §4 entry

### 4.9x Web: a first synchronous buffer_get_data() returns an empty array, not zeros (October 2026) *(work task 81; user answer `web-buffer-get-data-empty`)*

`RenderingDevice::buffer_get_data()` on the web now returns an empty array (and sets `r_not_ready`) while WebGPU's
asynchronous map is pending, like `texture_get_data()`, instead of falling through to the generic staging copy, which
stalled every frame in flight (`_flush_and_stall_for_all_frames()`) and then mapped a buffer that cannot be mapped
synchronously, returning zeros. Native keeps the staging path untouched (`#else`). Every engine caller was audited
(table in `webgpu_notes/work/results/81-web-buffer-get-data-empty/SUMMARY.md`): `mesh_get_surface()` is now
all-or-nothing (any array not ready → all empty; its vertex-padding trim no longer resizes an empty array to -4),
`mesh_surface_get_arrays()` returns an empty array quietly on the web with a debug `WARN_PRINT_ONCE`, the soft body
handler starts from zeros of the right size (an empty buffer would have been written out of bounds), and the three
MultiMesh readbacks (AABB rebuild, make-local cache, `multimesh_get_buffer`) check the size (two were out-of-bounds
reads). `async_readback` grew to 74 checks: 74/74 natively and in Chrome and Firefox with debug and release
templates; RD warnings still four in debug, mesh warning one, release none.

## Trap candidates

- **Engine code that reads a GPU buffer synchronously on the web gets an empty array on a first read** (task 81; before
  that, zeros of the right size). Any `buffer.ptr() + offset` or `memcpy(..., size_from_elsewhere)` on the result is an
  out-of-bounds read; check `size()` (or pass `r_not_ready`) and treat empty as "no data yet". `mesh_get_surface()`
  must stay all-or-nothing: its consumers index every array by the surface's vertex and index counts.
- (Pre-existing, unchanged) `MeshStorage::_multimesh_make_local()` on the web caches zeros for a MultiMesh whose
  buffer was set with `multimesh_set_buffer()` and never read back, then uploads those zeros with the next dirty
  region: `multimesh_set_buffer()` followed by `multimesh_instance_set_transform()` loses the other instances on the
  web. Fix candidate (§2): keep a CPU copy in `_multimesh_set_buffer()` on the web (make the cache local from
  `p_buffer` directly) so no readback is ever needed.

## Questions for the user

None.
