# Task 52: `DisplayServer.window_set_size()` on web now resizes the root window, viewport and surface

## What changed (commit on work/52)

The second of the task's two fixes: `DisplayServerWeb::window_set_size()` runs the resize path itself, synchronously.
- `godot_js_display_desired_size_set()` (`platform/web/js/libs/library_godot_display.js`) now returns whether the canvas
  changed, as `godot_js_display_size_update()` does (and also calls `GodotDisplayVK.updateSize()` then). Signature
  `vii` → `iii`, `godot_js.h` updated.
- Under canvas resize policy 0 (None) it returns 0 without touching anything. Before, it called `updateSize()`, which
  under policy 0 only resyncs `desired_size` to the canvas and returned 1, but that return value was dropped. If the
  page had just resized the canvas, the next frame's `size_update()` then saw nothing and the change was lost. Now
  that frame catches it.
- `check_size_force_redraw()`'s body is split into `DisplayServerWeb::_canvas_size_changed()` (rect-changed callback →
  `emscripten_set_canvas_element_size()` → WebGPU surface `window_set_size`, on the render thread when it owns the
  device). Both the per-frame check and `window_set_size()` call it.

Why not the first fix (only store `desired_size`, apply next frame): the frame drawn right after the call would use
the old canvas with the new viewport, and `window_get_size()` would lag one frame. The synchronous path has neither
problem. Re-entrancy is safe: `Window::set_size()` on the root updates `size` before it calls `window_set_size()`,
so the callback finds the same size and returns early.

Startup does not depend on the old synchronous behavior. `godot_js_display_setup_canvas()` sizes the canvas with
its own `updateSize()` call, and `main.cpp`'s `window_set_size` at init is editor-only (`TOOLS_ENABLED`).

Threaded builds were worse than the task described. With the render thread (OffscreenCanvas), `window_set_size()`
only changed the CSS size and `transferred_size`. The OffscreenCanvas itself was never resized, so the canvas stayed
1024x640 (baseline threads, policy 1 below). The fix resizes it.

## Verification

Test project `test_project/` (button → `DisplayServer.window_set_size(800, 600)`, commands through
`JavaScriptBridge`; `export_presets.cfg` points at absolute template paths in the w52 worktree, so edit them to
re-run). Driver: `resize_test.mjs <export_dir> <out_dir> <label>`. For each canvas resize policy 0/1/2 (the server
rewrites both GODOT_CONFIG and full-size.html's forced `canvasResizePolicy = 1`, and checks the policy in effect), it runs:
1. click the 800x600 button;
2. `window_set_size` back to 1024x640;
3. `Window.size = 700x500` and back;
4. a browser resize to 1000x700 and back to 1280x800.

The adapter was checked at the start of every run (nvidia/lovelace, not fallback, maxSampled 48), in system Chrome
with the Vulkan flags under the `gpu` lease. Builds:
- editor `dev_mode=yes` (rc 0);
- web `template_release webgpu=yes` nothreads and threads (rc 0).

Baseline: the lead's bin/ templates from 12:03/12:16 today (webgpu-4.8 HEAD), exported with the lead's editor.

| export | policy 1 after window_set_size(800,600) | back to 1024x640 | surface warning | policies 0 and 2 |
|---|---|---|---|---|
| fix nothreads | canvas 800x600, window/visible rect/corner 800x600, size_changed 0→1 | all 1024x640, signal 2 | none | identical to baseline |
| fix threads | same (800x600, signal 1) | same (signal 2) | none | identical to baseline |
| base nothreads | canvas 800x600, window still 1024x640, no signal | no signal | "surface texture (800 x 600) differs from configured (1024 x 640)" | — |
| base threads | canvas stays 1024x640, no signal | no signal | none (nothing resized) | — |

Browser resizes behave exactly as in the baseline under all three policies:
- policy 2 follows the viewport, 1000x700 then 1280x800, firing `size_changed`;
- policies 0 and 1 keep their size.

`Window.size` (camera-sim's workaround) still works. Screenshots:
- `runs/fix_threads_p1_after_window_set_size.png`: the corner marker sits in the bottom-right corner, so the UI is laid
  out at 800x600;
- `runs/base_nothreads_p1_after_window_set_size.png`: the marker is cropped off.

Full per-step logs are in `runs/`. fix_nothreads ends PASS. fix_threads' policy-2 "FAIL" lines come from the test itself: it reads
`canvas.width` of a canvas transferred to an OffscreenCanvas, and that attribute stays stale (the
baseline shows the same thing). The engine's own sizes there agree with the viewport.

smoke.sh: `SMOKE OK (smoke-20261028-124131-1152313)`: 292/292 identical interface, 584/584 text-scan facts,
292 modules, 0 invalid.

## Not done / risks

- `full-size.html`'s `fitCanvasToViewport()` still runs only on a browser `resize`. After `window_set_size()` the
  canvas keeps the old CSS scale factor, so an 800x600 canvas shows at 1.25x and is not re-centered for its new size.
  The aspect ratio is right now. That is shell work: the shell could also refit when the canvas's style size changes
  (ResizeObserver or a hook). It belongs with task 51/54's shell changes, not here.
- `window_set_size()` called from a thread other than the main loop's would now run the rect-changed callback on
  that thread. Native platforms behave the same way, and scripts call it from the main thread.
- The lead should re-run `resize_test.mjs` on the merged tree (nothreads and threads templates).
