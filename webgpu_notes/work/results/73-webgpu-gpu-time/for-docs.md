# For the shared docs

- `drivers/webgpu/README.md` (or `webgpu_site/TECHNICAL_REFERENCE.md`, timestamps):
  - GPU timestamps need the optional `timestamp-query` feature.
  - Each is the end-of-pass write of an empty compute pass. The beginning write goes to a spare query: emdawnwebgpu
    cannot omit the index.
  - Results come back through a ring of 4 `MAP_READ` buffers per frame slot, never waited for, so
    `viewport_get_measured_render_time_gpu()` and the Visual Profiler show values a few frames old on the web.
  - Without the feature they read 0.
- `webgpu_site/CORRECTNESS_AND_COMPATIBILITY.md` (if it lists "GPU profiler N/A on the web"): GPU times now work in
  Chrome and Firefox with `timestamp-query`. Safari is untested.
- The class reference note is on the branch (`RenderingServer.viewport_get_measured_render_time_gpu`).
