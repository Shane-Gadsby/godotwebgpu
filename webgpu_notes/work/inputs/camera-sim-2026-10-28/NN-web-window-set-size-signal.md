# NN-web-window-set-size-signal: `DisplayServer.window_set_size()` on web never emits `size_changed`

State: open
Owner:
Claimed:
Needs: build (web template), browser (one short Playwright run)
Branch: work/NN
Results: webgpu_notes/work/results/NN-web-window-set-size-signal/

Found by the camera-sim session on 2026-10-28.

## The bug

On the web platform, `DisplayServer.window_set_size(Vector2i(w, h))` resizes the `<canvas>`, but the root
`Window` never hears about it:
- no `size_changed`, and the viewport keeps its old size;
- so the UI keeps laying out at the old size, cropped or offset inside the new canvas;
- and the shell's `fitCanvasToViewport()` isn't re-run either, because the game never reacts to a resize.

It shows with canvas resize policy 1 ("Project"), which `misc/dist/html/full-size.html` forces. That's
the only policy where `desired_size` decides the canvas size.

**Cause:**
- `godot_js_display_desired_size_set()` (`platform/web/js/libs/library_godot_display.js`) stores the size
  and then calls `GodotDisplayScreen.updateSize()` straight away, which resizes the canvas.
- On the next `DisplayServerWeb::process_events()`, `godot_js_display_size_update()` finds the canvas
  already at the new size and returns 0.
- So `rect_changed_callback` never runs, and neither does the `emscripten_set_canvas_element_size()` /
  WebGPU surface resize that follows it.

`Window::set_size()` on the root window doesn't have the bug, because it updates the viewport itself.
camera-sim uses it as a workaround (`scripts/main.gd`, `_on_embed_fullscreen`).

## Goal

`DisplayServer.window_set_size()` on web resizes the canvas and the game's root window together, the same
way a browser resize does. Either fix works:
- `godot_js_display_desired_size_set()` only stores `desired_size`, and the next `process_events()`
  applies it through the normal path (`size_update` → `rect_changed_callback` → canvas and surface
  resize);
- or `DisplayServerWeb::window_set_size()` runs that path itself when the size actually changed.

Check whether anything relies on the resize happening synchronously inside `desired_size_set`. One
candidate is engine startup, where the project's window size is applied.

## Verification

- A web export with a script that calls `DisplayServer.window_set_size(Vector2i(800, 600))` from a
  button:
  - `get_tree().root.size_changed` fires;
  - `get_window().size` and `get_viewport().get_visible_rect().size` report 800×600;
  - the UI lays out at the new size, checked in a screenshot.
- Then set it back, and confirm the same holds for the original size.
- Normal browser-window resizes still behave as before, under all three canvas resize policies.
- WebGPU: no "surface texture differs from configured" warning after the resize. The surface has to
  follow the canvas.
