# For TASKS.md (task 52 → §4)

**§4.x Web `DisplayServer.window_set_size()` resizes the root window (task 52, 2026-10-28).**
`godot_js_display_desired_size_set()` resized the canvas synchronously, so the next `process_events()` size check
saw nothing. The root `Window` never emitted `size_changed`, the viewport stayed at the old size, and the WebGPU
surface was never reconfigured ("surface texture differs from configured" warning). Under the render thread the
OffscreenCanvas was not resized at all.
- `desired_size_set` now returns whether the canvas changed. `DisplayServerWeb::window_set_size()` then runs the same
  path as a browser resize (`_canvas_size_changed()`: rect callback, canvas element size, surface).
- Policy 0 (None) no longer consumes a pending page resize.
- Verified with `webgpu_notes/work/results/52-web-window-set-size-signal/resize_test.mjs` under policies 0/1/2,
  nothreads and threads.

Still open: `full-size.html`'s `fitCanvasToViewport()` doesn't refit after an engine-driven canvas resize (only on
`window.resize`). It goes with the shell tasks 51/54.

**§3 trap candidate:**
- A web JS function that both stores and *applies* a size starves the per-frame "did it change?" check that
  carries the side effects (signals, surface resize).
- Separately, under the render thread `canvas.width` of the transferred element is stale. Read
  `DisplayServer.window_get_size()` or `GodotDisplayScreen.transferred_size`, never the element.
