# For TASKS.md (task 54)

## Proposed archive entry (§4, after §4.62)

### 4.xx Web: the embedding page drives the game's fullscreen (October 2026) *(work task 54; camera-sim)*

`html/embed_fullscreen/enabled` (off) and `html/embed_fullscreen/allowed_origins` (comma-separated, shown only while
enabled; empty = any parent), passed as `GODOT_CONFIG.embedFullscreen = {enabled, allowedOrigins}`. A page on another
origin cannot fullscreen the game's canvas (its click is not user activation in a cross-origin iframe), so it
fullscreens the iframe and says so with `postMessage`: parent → game `{type: 'godot:fullscreen', on}` on every
`fullscreenchange` and in answer to game → parent `{type: 'godot:ready'}`; game → parent `{type: 'godot:exit-fullscreen'}`
(`window_set_mode(WINDOWED)` while the parent holds it; the mode changes on the parent's `{on: false}`,
`godot_js_display_fullscreen_exit()` returns 2) and `{type: 'godot:request-fullscreen'}` (`window_set_mode(FULLSCREEN)`
once a protocol parent is known; canvas request after 1 s without an answer). `GodotDisplayScreen.isFullscreen()` counts
the parent's fullscreen, so the existing `updateSize()` fullscreen branch and per-frame size check do the sizing
(iframe x DPR, follows the iframe while it grows) and `size_changed`; the C++ fullscreen callback is reported
deduplicated. Only `event.source === window.parent`, only listed origins; the parent's echo of the game's own canvas
fullscreen is ignored. Option off or not framed: no listener. `full-size.html`'s `fitCanvasToViewport()` now also runs
from a `ResizeObserver` on the canvas (task 52's leftover: it refits after `window_set_size()` and after a parent
fullscreen ends, where the browser's `resize` comes before the engine resizes the canvas). Worker: `embed_test.mjs` on
two origins, Chrome and Firefox, all 8 verification points pass (request path: the iframe click's activation lets the
cross-origin parent's `iframe.requestFullscreen()` succeed in both); default export differs only by the config key and
the observer lines; task 52's `resize_test.mjs` with a scale check passes under policies 0/1/2 (base fails the two
engine-driven resizes). Not done: threads template, Safari, export dialog by hand.

## §3 trap candidates

- **Playwright's `keyboard.press('Escape')` does not leave fullscreen in Chrome** (it does in Firefox). The key goes to
  the renderer, not the browser UI that owns the fullscreen exit, so a test that relies on it hangs in fullscreen and
  every following step fails. Leave through `document.exitFullscreen()` in the fullscreen document and say so. (Cost
  me nothing because the test fell back, but it would look like an engine bug.)
- **A test command channel through a single global (`window.__cmd`) loses commands when the snapshot helper writes it
  too.** Task 52's pattern (`__cmd = 'report'`) overwrote my `__cmd = 'windowed'` before the game's `_process` read it,
  and four checks failed as if `window_set_mode(WINDOWED)` did nothing. Wait for the game to clear the command before
  snapshotting. (Test-only; maybe not worth a trap.)

## Note for the camera-sim session

Once this is merged and the templates are rebuilt, camera-sim can drop its GDScript version:
1. Export preset: tick **HTML > Embed Fullscreen > Enabled**; optionally set **Allowed Origins** to the LMS/course
   site origins (comma-separated, scheme + host + port).
2. `test-scripts/parent.html` (and the real parent page): rename the messages `camera-sim:fullscreen` →
   `godot:fullscreen`, `camera-sim:ready` → `godot:ready`, `camera-sim:exit-fullscreen` → `godot:exit-fullscreen`, and
   add the `godot:request-fullscreen` branch from the docs snippet
   (`else if (type === 'godot:request-fullscreen' && !document.fullscreenElement) frame.requestFullscreen().catch(() => {})`).
3. `scripts/main.gd`: delete `setup_embed_fullscreen()` (and its call in `_ready`), `_on_embed_fullscreen()`,
   `_embed_fullscreen`, `_embed_windowed_size`, `_embed_callback`, and the `_embed_fullscreen` branches in
   `_on_fullscreen_button_toggled()`: call `DisplayServer.window_set_mode()` in both directions. The engine posts
   `godot:exit-fullscreen` itself when the parent holds the fullscreen, and `window_get_mode()` reports FULLSCREEN
   during it, so `is_canvas_fullscreen()` (rename it, it is no longer only the canvas) covers both cases.
4. `_on_window_resized()`: the `window.dispatchEvent(new Event('resize'))` workaround is no longer needed with the
   default shell (it now refits whenever the canvas size changes). The 0.3 s re-check of the button state can stay or
   go; the mode is now updated by the engine when the parent's message arrives.
5. The canvas now goes to the iframe size x DPR on its own; camera-sim's `get_window().size = ...` in
   `_on_embed_fullscreen` goes with that function. Its "Match screen" render resolution logic still reacts to
   `size_changed` as before.

## Questions for the user

- Should `godot:request-fullscreen` (the game's own fullscreen goes through the parent when one speaks the protocol)
  be a separate option? It is on whenever `html/embed_fullscreen/enabled` is, with the 1 s canvas fallback. Default
  answer: keep it as is (no option).
