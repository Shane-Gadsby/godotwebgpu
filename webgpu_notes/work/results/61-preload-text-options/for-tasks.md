# Task 61: for TASKS.md

## §4 entry

### 4.xx Web start screen: the download line's text, configurable (October 2026) *(work task 61; user request)*

`html/start_button/preload_text/`: `downloading` + `downloading_text` ("Downloading..."), `progress` + `progress_text`
("Downloading {percent}%", every `{percent}` replaced, a text without it shown as is), `ready` + `ready_text` ("Ready"),
`color` (`#e0e0e0`, the color the shells' CSS already had). They are shown only while `enabled`, `preload` and
`preload_progress` are on, and each text only while its switch is on. Passed as `GODOT_CONFIG.startButton.preloadText`
(documented in `config.js`), with `""` for a message switched off. Both default shells insert the messages with
`textContent` and fall back to the old texts for a missing key. An empty message leaves the line empty at the same
height. Worker: dev_mode editor, web template_debug and template_release clean. Visibility through
`_get_property_list()` is right for every switch combination. 34/34 browser runs (Chrome and Firefox headed, adapter
asserted, a gated server holding each state): edited texts, `{percent}` in the middle and twice, no `{percent}`, each
message off, all off, color, literal `<b>`, a cross-origin iframe, the WebGPU shell. Defaults are 0 pixels different
from the webgpu-4.8 shells in all three states in both browsers. Not done: the windowed-editor dialog, Safari.

## §3 trap candidate

- **"Downloading..." is up for one frame only, so a test that waits to see it fails.** The export writes `fileSizes`
  into the config, so the `Preloader` has its total before any header arrives, and the line jumps to "Downloading 0%"
  at once. Without `fileSizes` (a custom config) there is never a total, and the line says "Downloading..." until
  "Ready". To hold either state still, gate the server rather than throttle it (task 61's `preload_text_test.mjs`), and
  strip `fileSizes` to test the unknown-size message.

## Questions for the user (defaults marked)

1. `preload-text-opacity`: the line is drawn at 85% opacity with a shadow, as before, so a chosen color shows slightly
   dimmer than picked. **Default: keep it** (the start screen stays as it was, and the option docs say so). The
   alternative is to drop the opacity when the color is not the default.
2. `preload-text-font`: the line keeps the shell's font and size (0.9rem). **Default: no font/size options.**
   Alternative: add `font_size` / `font_family`, like the button has.
