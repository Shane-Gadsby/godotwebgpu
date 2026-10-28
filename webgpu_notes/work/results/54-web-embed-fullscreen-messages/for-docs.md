# For the docs (task 54)

## Wiki: Engine-Additions-Web-Export-Options.md

### Options table: add these rows (after the start-button rows)

```markdown
| `html/embed_fullscreen/enabled` | bool | `false` | Lets a page that embeds the game in an iframe, also on another site, put it in fullscreen with its own button, through `postMessage`. The game sees a normal `WINDOW_MODE_FULLSCREEN` at full resolution. See [Fullscreen from the embedding page](#fullscreen-from-the-embedding-page). |
| `html/embed_fullscreen/allowed_origins` | String | `""` | The pages allowed to do that, comma-separated origins such as `https://lms.example.edu, https://site.example.com` (scheme, host and port, no path). Empty accepts any page. Shown only with `html/embed_fullscreen/enabled`. |
```

### New section

```markdown
## Fullscreen from the embedding page

A game is often embedded in a page on another site: a course platform, a portfolio, a game portal. That page usually
wants its own Fullscreen button. The browser does not let it put the game's canvas in fullscreen: a click in the page
does not count as a click inside an iframe from another site. The page can put the whole iframe in fullscreen, but
without help the game does not know: it stays at its windowed size, scaled up and blurry, and
`DisplayServer.window_get_mode()` still says windowed.

With `html/embed_fullscreen/enabled`, the game and the page talk through `postMessage`:

| Direction | Message | Meaning |
|---|---|---|
| page → game | `{type: 'godot:fullscreen', on: true \| false}` | The iframe entered or left fullscreen. Send it on every `fullscreenchange`, and in answer to `godot:ready`. |
| game → page | `{type: 'godot:ready'}` | The engine is running and listening. A fullscreen that started on the start screen or while loading is picked up through the answer. |
| game → page | `{type: 'godot:exit-fullscreen'}` | The game called `DisplayServer.window_set_mode(WINDOW_MODE_WINDOWED)` while the page holds the fullscreen. Only the page can leave it. |
| game → page | `{type: 'godot:request-fullscreen'}` | The game called `window_set_mode(WINDOW_MODE_FULLSCREEN)`. The page puts the iframe in fullscreen, so its own button stays in step. If the page does not answer within a second, the game puts its canvas in fullscreen itself, as without the option. |

While the page holds the fullscreen, the game behaves exactly as in its own fullscreen: `window_get_mode()` returns
`WINDOW_MODE_FULLSCREEN`, the canvas is drawn at the screen's full resolution, and the root window emits `size_changed`,
so the UI lays out at the new size. When the page leaves fullscreen (its button, or Esc), the game goes back to its
windowed size. The game's own fullscreen button keeps working.

The game only listens to its parent window, and only to the origins in `html/embed_fullscreen/allowed_origins` when
that is set. Without the option, nothing changes and no message listener is added.

The page, with `GAME-HOST` replaced by where the game is hosted:

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

The `allow="fullscreen"` on the iframe is needed for both the page's fullscreen and the game's own.
```

### Custom shells subsection (from task 51): add this row to the key table

```markdown
| `embedFullscreen` | `{enabled, allowedOrigins}` | `html/embed_fullscreen/*`. Read by the engine itself; a custom shell needs nothing for it. |
```

### Canvas sizing note (Project policy, default shell)

Add to wherever the default shell's scaling is described:

```markdown
The default shell scales the canvas to fit the page and refits whenever the canvas changes size, including after
`DisplayServer.window_set_size()` and when an embedding page's fullscreen ends.
```
