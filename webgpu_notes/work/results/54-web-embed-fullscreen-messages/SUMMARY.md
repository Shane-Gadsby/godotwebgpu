# Task 54: fullscreen driven by the embedding page (`html/embed_fullscreen/*`), summary

## What changed

- `platform/web/export/export_plugin.cpp`: `html/embed_fullscreen/enabled` (bool, off, `update_visibility`) and
  `html/embed_fullscreen/allowed_origins` (String, placeholder text, shown only while `enabled`). Written as
  `config["embedFullscreen"] = {enabled, allowedOrigins: [...]}` in `$GODOT_CONFIG`: the list is split on commas,
  trimmed, and a trailing `/` is dropped (a message's origin never has one). Empty list = any parent. Documented in
  `EditorExportPlatformWeb.xml` (protocol included) and `config.js` (`EngineConfig.embedFullscreen`).
- `platform/web/js/libs/library_godot_display.js` (`GodotDisplayScreen`), all inert unless the option is on **and** the
  page is in a frame (`window.parent !== window`); otherwise no listener is added:
  - `initEmbed()` (from `init()`): a `message` listener that accepts only `event.source === window.parent`, only
    `event.origin` in `allowedOrigins` when set, only `{type: 'godot:fullscreen', on}`. It remembers the origin, and
    posts `{type: 'godot:ready'}` to the parent with `'*'` (no data).
  - `isFullscreen()` is also true while the parent holds the fullscreen (`embedHoldsFullscreen()`: parent on, not an
    echo, nothing in this document fullscreen). So `updateSize()`'s existing fullscreen branch sizes the canvas to the
    iframe x DPR, re-checked every frame (it follows the iframe while it grows), and the existing per-frame size check
    fires the rect-changed callback / `size_changed`. On `{on: false}`, the canvas goes back to `desired_size`.
  - The C++ fullscreen callback is stored (`fullscreen_cb`) and called through `reportFullscreen()`, deduplicated, on
    every state change (message or this document's `fullscreenchange`), so `window_get_mode()` follows.
  - Echo: the game's own canvas fullscreen makes the parent report its iframe as fullscreen. An `{on: true}` that comes
    while this document has a fullscreen element (or after it entered one while the parent was off) is marked as echo
    and ignored; `{on: false}` clears it.
  - `exitFullscreen()` while the parent holds it posts `{type: 'godot:exit-fullscreen'}` (to the remembered origin)
    and returns 2. `DisplayServerWeb::window_set_mode(WINDOWED)` then leaves the mode as it is; it changes when the
    parent's `{on: false}` arrives.
  - `godot_js_display_fullscreen_request` goes through `requestEmbedFullscreen()`: when a protocol parent is known (an
    accepted message came in), it posts `{type: 'godot:request-fullscreen'}` and falls back to the canvas request after
    1 s without `{on: true}`. With no known parent it is the canvas request as before (no delay).
  - `fullscreen_on_first_tap` does not fire while the parent holds the fullscreen; a parent `{on: true}` counts as the
    first fullscreen. `atexit` clears the state.
- `platform/web/display_server_web.cpp`: the `== 2` check above (one `if`).
- `misc/dist/html/full-size.html` (lead's addition from task 52): `fitCanvasToViewport()` also runs from a
  `ResizeObserver` on the canvas, so it refits whenever the engine changes the canvas's CSS size (`window_set_size()`,
  entering or leaving a parent's fullscreen), not only on a browser `resize`. I used an observer rather than a hook in
  the display library: it needs no engine change and covers every source of a canvas resize. The shell's own transform
  does not change the observed size, so it cannot loop. `webgpu-full-size.html` has no fit function (it uses the
  preset's policy and no transform), so it is unchanged.

## Verification (actual results)

- Builds (build lease): editor `scons platform=linuxbsd target=editor dev_mode=yes webgpu=yes accesskit=no` (werror)
  rc 0; web `template_release dlink_enabled=no webgpu=yes opengl3=no threads=no` rc 0. `pre-commit run --files` on
  every changed file: clean.
- Browsers: `scripts/embed_test.mjs` (from camera-sim's `embed_test.mjs` / `start_test.mjs`), under the gpu lease.
  System Chrome headed with the required flags, adapter asserted nvidia / lovelace / not fallback / 48. Firefox headed
  with `FIREFOX_WEBGPU_PREFS`, `requestAdapter()` non-null. Parent `localhost:8701` (`scripts/parent/parent.html` = the
  task's snippet + a log + `?norequest` / `?silent` switches + a sibling `evil.html` frame), game on `127.0.0.1:8702`.
  Test project `test_project/` (1024x640, stretch disabled, in-game Fullscreen/Windowed buttons; logs mode, sizes,
  `size_changed` count and the bottom-right corner marker). Five exports: `main` (on), `start` (on + start button),
  `deny` (`allowed_origins="https://lms.example.edu, https://site.example.com/"`), `allow` (`..., http://localhost:8701/`),
  `off` (defaults). **Chrome and Firefox: every check PASS** (Chrome: round 1, where scenario `main` failed 4 checks from
  a test bug (my snapshot overwrote the `windowed` command before the game read it), then `main` rerun all PASS:
  `runs/chrome_round1.json`, `runs/chrome_main_rerun.json`; Firefox one round, `runs/firefox.json`).

  | # | Check | Chrome | Firefox |
  |---|---|---|---|
  | 1 | Parent button: mode FULLSCREEN, canvas = iframe x DPR, shell scale 1, `size_changed` fired, window/viewport/corner = canvas | 1189x1217 (the test window's fullscreen), scale 1, signals 0→1 | 2560x1440, scale 1, signals 0→1 |
  | 2 | Exit: windowed, 1024x640, shell refit to 0.914 | via parent `exitFullscreen()` (Playwright's Escape does not leave fullscreen in Chrome) | via **Escape** |
  | 3 | `window_set_mode(WINDOWED)` while parent-fullscreen: parent got `godot:exit-fullscreen` and left; mode still FULLSCREEN right after the call, WINDOWED after the answer | yes, 13 ms | yes, 37 ms |
  | 4 | Parent fullscreen on the start screen, then Start; Start, then parent fullscreen while loading: game comes up fullscreen | yes / yes (4a: first frame windowed, fullscreen 32 ms after ready) | yes / yes |
  | 5 | Sibling frame and self-posted `{on: true}` ignored; `deny` export ignores the parent; `allow` export accepts it | yes | yes |
  | 6 | Option off: no `message` listener on window (init-script hook; Chrome also DevTools `DOMDebugger.getEventListeners`: 0), no `godot:ready`; parent fullscreen leaves the game windowed and shell-scaled; in-game button → canvas fullscreen | yes | yes (hook only) |
  | 7 | Game's own button, parent ignores the request: canvas fullscreen after the 1 s fallback, the parent echoes `{on: true}`, **one** mode change in and one out, no `exit-fullscreen` posted. Parent with no protocol: canvas fullscreen at once | yes (1131 ms; 43 ms) | yes (1095 ms; 115 ms) |
  | 8 | `godot:request-fullscreen`: in-game click → the parent's `iframe.requestFullscreen()` is granted (the iframe click's activation reaches the cross-origin parent) → fullscreen; in-game Windowed → exit | yes, 46 ms | yes, 173 ms |

  Screenshots of the UI laid out at the new size: `runs/{chrome,firefox}_1_parent_fullscreen.png` (orange corner
  marker in the bottom-right corner). Console: only the favicon 404, the test parent's missing `screen-wake-lock`
  permission, and Firefox's usual warnings.
- **Default export unchanged** (`default_html.diff`): base = the lead's editor + `template_debug` nothreads from 14:25
  (webgpu-4.8 HEAD) vs this branch, same project, default options. The HTML differs only by the `embedFullscreen`
  config key, the wasm size (debug vs release) and the shell's `ResizeObserver` lines.
- **Shell refit** (`scripts/resize_test.mjs` = task 52's, plus a check that under policy 1 the shell's scale equals
  `min(inner/css)` and the canvas is centered after every step): this branch PASS under policies 0/1/2 (policy 1:
  scale 1.25 → 1.333 after `window_set_size(800,600)`, 1.6 after `Window.size = 700x500`, 0 warnings). The base export
  FAILS exactly those two steps (scale stays 1.25). Centering was already right (translate -50%); only the scale was
  stale. `runs/fix_nothreads.json`, `runs/base_nothreads.json`.
- `webgpu_notes/work/smoke.sh`: `SMOKE OK (smoke-20261028-143912-1771711)`: interface 302/302 identical, text-scan
  604/604, 302 modules, 0 invalid. No shader code touched.

## Not done / risks

- **Threads template not built or run.** I did not touch the render-thread path: the canvas size goes through the
  existing `updateSize()` / `transferred_size` path, and the callback through the existing `fullscreen_change_callback`
  (which defers to the main thread). The lead may want one `embed_test.mjs` run on a threads export; the test reads
  `canvas.width`, which is stale on a transferred canvas (task 52), so its size checks would need the Godot-side sizes.
- Not checked: Safari; the export dialog by hand in a windowed editor (the visibility rule copies `start_button`'s);
  a real Escape in Chrome.
- The request path waits up to 1 s before the canvas fallback when the parent speaks the protocol but has no
  `godot:request-fullscreen` handler. The docs snippet has the handler.
- A parent on a sandboxed origin (`'null'`) is answered with `'*'` (`postMessage()` cannot target `'null'`). The
  messages carry no data.
- The scripts in `scripts/` refer to my scratch paths (`export.sh`'s `W`) and the w54 worktree; adjust before reuse.
