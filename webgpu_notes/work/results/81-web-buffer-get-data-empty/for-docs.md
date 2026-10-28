# For the shared docs (task 81)

- `CLAUDE.md`: nothing required. If the lead wants a line, under "Buffer mapping": "On the web a synchronous
  `RenderingDevice::buffer_get_data()` returns an empty array until the asynchronous readback has arrived (sets
  `r_not_ready`), never zeros; engine code that reads a GPU buffer synchronously must treat empty as 'no data yet'
  (TASKS §4.9x, task 81)."
- `webgpu_site/CORRECTNESS_AND_COMPATIBILITY.md` (synchronous readback section, if it mentions buffers returning zeros):
  a first synchronous `buffer_get_data()`, `Mesh.surface_get_arrays()` on an ArrayMesh/ImmediateMesh, and
  `RenderingServer.multimesh_get_buffer()` on the web now return an **empty** array until the data has arrived (debug
  builds warn once for `buffer_get_data()` per buffer and once for mesh arrays); use `buffer_get_data_async()` or call
  again on a later frame. `GPUParticles3D.capture_aabb()` returns an empty AABB until then.
- Class reference: already changed on the branch (`RenderingDevice.buffer_get_data`, `RenderingServer.mesh_surface_get_arrays`,
  `Mesh.surface_get_arrays`).
