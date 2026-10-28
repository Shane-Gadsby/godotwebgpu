# Task 81: on the web, a first synchronous buffer_get_data() returns an empty array (worker-81)

User answer `web-buffer-get-data-empty` = "Return empty".

## What changed

- `servers/rendering/rendering_device.cpp` `buffer_get_data()`: under `WEB_ENABLED`, when the driver's
  `buffer_get_data_direct()` returns false (async map not done), set `r_not_ready` and return an **empty array**. The
  staging copy, `_flush_and_stall_for_all_frames()` and the unmappable `buffer_map()` are now `#else` (native only), so
  native is unchanged bit for bit and the web skips the pointless stall. `_buffer_get_data_bind()`'s debug warning now
  says "returned an empty array for buffer of N bytes".
- Docs: `RenderingDevice.buffer_get_data` (web note: empty array until the data has arrived),
  `RenderingServer.mesh_surface_get_arrays` and `Mesh.surface_get_arrays` (web note; PrimitiveMesh unaffected).

## Caller audit (every engine caller of `buffer_get_data()`; `grep -rn "buffer_get_data("` outside thirdparty)

| Caller | Before (web) | Now |
|---|---|---|
| `RenderingDevice::_buffer_get_data_bind` (script API) | zeros + warning | empty + warning (text updated) |
| `MeshStorage::mesh_get_surface` (vertex, attribute, skin, index, LOD index, blend shape) | zeros | **guarded**: all reads pass `r_not_ready`; if any is not ready, every data array is cleared (all-or-nothing, as consumers index them by vertex/index count). The vertex padding trim `resize(size - 4)` is skipped on an empty array (it would have been a negative resize). |
| ↳ `RenderingServer::mesh_surface_get_arrays` (Mesh/ArrayMesh/ImmediateMesh `surface_get_arrays`, MeshDataTool, soft bodies, trimesh shapes) | zero arrays | **guarded** (web only): empty `Array()` without `mesh_create_arrays_from_surface_data()`'s error; debug builds `WARN_PRINT_ONCE`. Godot/Jolt soft bodies already `ERR_FAIL` on an empty array. |
| ↳ `mesh_surface_get_lods`, `mesh_surface_get_blend_shape_arrays`, `_mesh_get_surface`, `ArrayMesh::_get_surfaces` | zeros | already safe: sizes are taken from the arrays (empty dict/array) |
| ↳ `SoftBodyRenderingServerHandler::prepare` (`scene/3d/physics/soft_body_3d.cpp`) | zeros, then overwritten | **guarded**: an empty vertex array would have been an out-of-bounds write in `set_vertex()`/`set_normal()`; it starts from zeros of the uncompressed size (`(vertex_stride + normal_stride) * vertex_count`), exactly what it got before |
| `MeshStorage::_multimesh_set_mesh` AABB rebuild | AABB from zeros | **guarded**: an out-of-bounds read; skips the rebuild (keeps the current AABB) when the buffer is short |
| `MeshStorage::_multimesh_make_local` | zeros | **guarded**: copy `min(size, cache)`, zero the rest (uninitialized memory otherwise; same zeros as before) |
| `MeshStorage::_multimesh_get_buffer` (`multimesh_get_buffer`, `MultiMesh.buffer`) | zeros | **guarded**: an out-of-bounds read; returns an empty array |
| `ParticlesStorage::particles_get_current_aabb` (`capture_aabb`) | AABB of zeros | was already safe (size `ERR_FAIL`); web-only quiet `AABB()` instead of the error when empty |
| `GI::voxel_gi_get_octree_cells` / `voxel_gi_get_data_cells` | zeros | already safe: pass-through to the caller (VoxelGIData save, editor) |
| `LightmapperRD` light probes (`modules/lightmapper_rd`) | n/a | already safe: `memcpy` uses the returned size; editor-only bake on its own local device |

Every guard behaves identically natively (natively the read always returns the full size); only
`particles_storage.cpp` and `rendering_server.cpp` are `#ifdef WEB_ENABLED`.

## Verification (2026-10-29)

- Builds: dev_mode editor (`werror`) OK; web `template_debug` and `template_release` (nothreads, no dlink) OK.
- `webgpu_tests/async_readback/` extended from 68 to **74 checks**: on the web a buffer's first synchronous read
  (frame 5) must be empty (never zeros) and every web read is empty or the data; at frame 35 each buffer reads the
  data, whole and at an offset (bytes 8-27); an ArrayMesh's `surface_get_arrays()` is empty at frame 5 on the web /
  the data natively, and the data at frame 35 everywhere. The runners count the new mesh warning (debug web 1,
  release 0, native 0).
- Results (`test_results.txt`): **native 74/74**; **Chrome debug 74/74, Firefox debug 74/74, Chrome release 74/74,
  Firefox release 74/74**; RD warnings debug 4 (41x5, 17x3, 100 B, 52 B, unchanged), release 0, native 0; mesh
  warning debug 1, release 0; 0 WebGPU validation messages, 0 unexpected console errors. Chrome adapter nvidia/lovelace,
  48 sampled textures; Firefox adapter non-null.
- Observed: buf_100 is empty at frame 6 too in Chrome, data at frame 6 in Firefox, data by frame 7 in both.

## Skipped (user's test scope)

`smoke.sh` (no shader or interface change), every other tier (scene smoketest, Forward+ matrix, perf guard).
Soft body and MultiMesh guards are not exercised by a test (no tier drives them on the web); they were checked by reading.

## Risks

- Behavior change visible to scripts on the web: `Mesh.surface_get_arrays()` on an ArrayMesh/ImmediateMesh,
  `MultiMesh.buffer`, `capture_aabb()` now return empty on a first call instead of zeros. Code that indexed the zeros
  (e.g. `arrays[Mesh.ARRAY_VERTEX]`) now gets an empty array; the data was wrong before, now it is visibly absent.
- `_multimesh_make_local` on the web still caches zeros when the buffer was set via `multimesh_set_buffer()` and not
  yet read back, then uploads them on the next dirty region: pre-existing data loss, unchanged (see for-tasks.md).
