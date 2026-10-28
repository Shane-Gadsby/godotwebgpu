# For docs

Wiki, Engine Additions → Notable Fixes:
- **Web: `DisplayServer.window_set_size()` resizes the game, not just the canvas.** The root window emits
  `size_changed`, the viewport and the UI take the new size, and the WebGPU surface follows. Before, only the
  `<canvas>` changed (and with threads, not even that).
