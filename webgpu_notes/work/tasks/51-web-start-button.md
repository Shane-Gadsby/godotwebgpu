# 51-web-start-button: a "click to start" screen in the default web shell, with export options

State: open
Owner: worker-51
Claimed:
Needs: build (web template + editor), browser (Playwright)
Branch: work/51
Results: webgpu_notes/work/results/51-web-start-button/

User request, 2026-10-28. camera-sim already does this in its own copy of the shell
(`/mnt/109313D2109313D2/godot-game-dev/camera-sim/web/shell.html`; every change there is marked `START SCREEN`).
That copy is the reference implementation. This task moves the feature into `misc/dist/html/full-size.html`,
behind export options, so projects don't need a copy of the shell.

The background download that goes with it is a separate task (53-web-preload-before-start), which builds
on this one.

## Goal

With the option ticked, the exported page opens on a start screen, like itch.io's "Run game": the splash
image with a button under it. **Nothing of the engine runs until the button is pressed:** no wasm
download or compile, no GPU device, no audio context. Pressing it runs today's startup unchanged, with
the same loading screen, progress bar and stages.

### Export options (Web preset, `html/` section)

| Option | Type | Default | Shown |
|---|---|---|---|
| `html/start_button/enabled` | bool | **false** | always |
| `html/start_button/text` | String | `"Start"` | only when enabled |
| `html/start_button/background_color` | Color | a neutral dark grey | only when enabled |
| `html/start_button/text_color` | Color | white | only when enabled |

- Declare `enabled` with `update_visibility = true` (the third `ExportOption` argument, as for
  `variant/thread_support`). Hide the others in `EditorExportPlatformWeb::get_export_option_visibility()`
  while it's unticked, as `shader_baker/separate_pack` is hidden behind `shader_baker/enabled`.
- Off by default, so existing exports don't change.
- Pass the settings to the shell in `$GODOT_CONFIG` (e.g. `config["startButton"] = {enabled, text,
  backgroundColor, textColor}` in `_fix_html`), so custom shells can read them too. `EngineConfig` (`config.js`)
  should parse and document the new key like the other `html/` ones.
- Class/export docs: describe each option, and say that the press counts as a user gesture inside the page.

### Shell behaviour (`misc/dist/html/full-size.html`)

- Show the start screen as a new status mode (camera-sim adds a `'start'` mode to `setStatusMode()`): the
  splash, then the button, centred like the loading column.
- **Keep the missing-features check at page load.** A browser without WebGPU gets the error notice straight
  away, not a Start button that fails.
- **Don't focus the button on load.** In an iframe, that scrolls the parent page to the game.
- Press → disable the button → run the existing startup (in camera-sim's copy, the old `else` branch moved
  into `startEngine()`).
- Enter and Space work, because it's a real `<button>`.

## Verification

- Export with the option off. The page is byte-identical to today's apart from the config key, and behaves
  the same.
- Export with it on, in Chrome. Before the press:
  - `engine.rtenv` is unset;
  - the canvas still has no inline width;
  - the only requests are the page, `index.js` and the splash.

  After the press, startup runs as usual, with the loading screen and its stages.
- The same inside a cross-origin iframe: no scroll jump in the parent on load.
- The text and colours from the options show up, checked in a screenshot.
- In the export dialog, the three sub-options appear and disappear with the tick box.
- Test script to adapt: `test-scripts/start_test.mjs` (two origins: parent on `localhost:8701`, game on
  `127.0.0.1:8702`, with an optional throttled server).

## Additions from the user (2026-10-28, in the lead's session)

The start button's look is customizable beyond the text and two colors above. Add, each shown only while
`html/start_button/enabled` is ticked: **border color**, **border width**, **border radius**, **font family**, **font
size**, **padding**, with `cursor: pointer` by default (an option if you see a use), and the button **slightly darker on
hover and slightly darker again while pressed** (derived from the background color; an override color for each state if
cheap). Keyboard focus ring kept (accessibility), Enter/Space (a real `<button>`). Pass them in the same
`$GODOT_CONFIG` `startButton` object and document them.

The **background download is task 53**, on top of this one: leave preload out of this task.

Test scripts: `webgpu_notes/work/inputs/camera-sim-2026-10-28/test-scripts/` (copied from the camera-sim session; their
Chrome launch is correct). Browser rule: `webgpu_notes/work/README.md`.
