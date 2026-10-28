# NN-web-embed-fullscreen-messages: fullscreen driven by the embedding page, via postMessage

State: open
Owner:
Claimed:
Needs: build (web template + editor), browser (Playwright, two origins); best **after NN-web-window-set-size-signal**
Branch: work/NN
Results: webgpu_notes/work/results/NN-web-embed-fullscreen-messages/

User request, 2026-10-28. camera-sim does this in GDScript today:
- `scripts/main.gd`: `setup_embed_fullscreen()`, `_on_embed_fullscreen()`, and the `_embed_fullscreen`
  branches in `_on_fullscreen_button_toggled()`;
- its parent page is `test-scripts/parent.html`.

This task moves it into the web template, so any project can be put in fullscreen by a page on another
domain, and the game sees a normal `WINDOW_MODE_FULLSCREEN`.

## The problem

A game is often iframed into a page on another origin (an LMS, a course site, itch-style hosting), and the page
wants its own Fullscreen button. The browser won't let that button put the game's canvas in fullscreen,
because a click in the parent isn't user activation inside a cross-origin iframe. What the parent *can* do
is `iframe.requestFullscreen()`. But then:
- the engine thinks it's windowed: `GodotDisplayScreen.isFullscreen()` only counts the canvas as the
  fullscreen element, and inside the iframe `document.fullscreenElement` is null;
- the canvas stays at the project size and the shell scales it up, so it's blurry;
- the game's own fullscreen button is out of sync.

## Goal

### Export options

| Option | Type | Default | Shown |
|---|---|---|---|
| `html/embed_fullscreen/enabled` | bool | false | always |
| `html/embed_fullscreen/allowed_origins` | String (comma-separated origins, e.g. `https://lms.example.edu, https://site.example.com`) | `""` = any embedding page | only when enabled |

- Declare `enabled` with `update_visibility = true`, and hide `allowed_origins` in
  `get_export_option_visibility()` while it's unticked.
- Pass both through `$GODOT_CONFIG` (e.g. `embedFullscreen: {enabled, allowedOrigins: [...]}`), and document
  them, including the parent-page snippet below.

### Message protocol (engine JS: `library_godot_display.js`, `GodotDisplayScreen`)

Accept messages only when `event.source === window.parent` (and `window.parent !== window`), and
`event.origin` is in `allowedOrigins` when that's set. Reply to the parent with `postMessage(msg, origin)`,
using the origin the parent's last accepted message came from. Use `'*'` only for `godot:ready`, which
carries no data.

| Direction | Message | Meaning |
|---|---|---|
| parent → game | `{type: 'godot:fullscreen', on: true \| false}` | The iframe entered or left fullscreen. The parent sends it on every `fullscreenchange`, and in answer to `godot:ready`. |
| game → parent | `{type: 'godot:ready'}` | The engine is up and listening (so a fullscreen that started on the start screen or during loading is picked up). |
| game → parent | `{type: 'godot:exit-fullscreen'}` | The game asked to leave fullscreen (`window_set_mode(WINDOWED)`) while the parent holds it. |
| game → parent | `{type: 'godot:request-fullscreen'}` | Optional, see below. |

### Engine behaviour

- **On `{on: true}`**, treat the window as fullscreen:
  - `window_get_mode()` returns `WINDOW_MODE_FULLSCREEN`;
  - `updateSize()` sizes the canvas to the iframe's `innerWidth/innerHeight × pixel ratio`, as its
    `isFullscreen` branch does now;
  - `rect_changed_callback` and `size_changed` fire;
  - the fullscreen callback (`godot_js_display_fullscreen_cb`) fires as for a canvas fullscreen.

  While "on", keep tracking `resize` events, because the iframe may still be growing when the message
  arrives.
- **On `{on: false}`**: back to windowed and the previous canvas size, with the same callbacks.
- **`window_set_mode(WINDOWED)` while parent-fullscreen:** post `godot:exit-fullscreen` instead of
  `exitFullscreen()`, which would reject in the iframe. The mode changes when the parent's `{on: false}`
  arrives.
- **`window_set_mode(FULLSCREEN)` while not fullscreen:** keep today's canvas `requestFullscreen()` (it works
  in the iframe when the parent's `<iframe>` has `allow="fullscreen"`).
  - Optionally, when embedded with the option on, post `godot:request-fullscreen` instead, so the parent
    fullscreens the iframe and its own page chrome stays consistent.
  - User activation from a click inside the iframe propagates to ancestor frames, so the parent's handler
    can usually call `iframe.requestFullscreen()`. **Verify in Chrome and Firefox before relying on it**, and
    fall back to the canvas request if no `{on: true}` comes back within ~1 s.
- **Canvas fullscreen inside the iframe** (the game's own button) also makes the parent see the iframe as
  fullscreen, and send `{on: true}`. Ignore it while the canvas itself is the fullscreen element: the
  existing path handles that. camera-sim had to handle this case.
- No effect at all when the option is off, and no listener is added.

### Parent page snippet (for the docs)

```html
<iframe id="game" src="https://GAME-HOST/index.html" allow="fullscreen"></iframe>
<button id="game-fullscreen">Fullscreen</button>
<script>
const GAME_ORIGIN = 'https://GAME-HOST';
const frame = document.getElementById('game');
document.getElementById('game-fullscreen').addEventListener('click', () => {
	document.fullscreenElement ? document.exitFullscreen() : frame.requestFullscreen().catch(() => {});
});
function sendState() {
	frame.contentWindow.postMessage({ type: 'godot:fullscreen', on: document.fullscreenElement === frame }, GAME_ORIGIN);
}
document.addEventListener('fullscreenchange', sendState);
window.addEventListener('message', (event) => {
	if (event.source !== frame.contentWindow || !event.data) return;
	if (event.data.type === 'godot:ready') sendState();
	else if (event.data.type === 'godot:exit-fullscreen' && document.fullscreenElement) document.exitFullscreen();
	else if (event.data.type === 'godot:request-fullscreen' && !document.fullscreenElement) frame.requestFullscreen().catch(() => {});
});
</script>
```

## camera-sim afterwards

Once this lands, camera-sim will:
- switch its parent page to the `godot:` message names;
- tick the option;
- delete its GDScript version: `setup_embed_fullscreen`, `_on_embed_fullscreen`, `_embed_*` and the
  `_embed_fullscreen` branches.

Note that in the hand-back so the camera-sim session can do it.

## Verification

Two origins: parent on `localhost:8701`, game on `127.0.0.1:8702`. Adapt `test-scripts/embed_test.mjs` and
`start_test.mjs`.
1. **Parent button → fullscreen.**
   - In GDScript, `window_get_mode() == WINDOW_MODE_FULLSCREEN`.
   - The canvas is the iframe's size × DPR, and the shell's CSS scale is 1.
   - `size_changed` fired, and the UI lays out at the new size (screenshot).
2. **Esc** (or the parent's `document.exitFullscreen()`): back to windowed and the original canvas size.
3. **Game calls `window_set_mode(WINDOWED)`** while parent-fullscreen: the parent leaves fullscreen.
4. **Parent fullscreens before the game is up** (on the start screen, or during loading): the game comes up
   fullscreen (`godot:ready` handshake).
5. **Security:** a message from another window (e.g. a second iframe, or a popup with `window.opener`) is
   ignored. With `allowed_origins` set, a parent on an unlisted origin is ignored.
6. **Option off:** no `message` listener is added (check with DevTools `getEventListeners(window)`), and the
   behaviour is unchanged.
7. **Game's own fullscreen button inside the iframe** (`allow="fullscreen"`): works as today, with no double
   handling of the parent's echo.
8. Optional `godot:request-fullscreen` path, in Chrome and Firefox.

camera-sim's GDScript version passed 1, 2, 3 (via its own Exit button) and 4 with this method, in Chrome,
on 2026-10-28.
