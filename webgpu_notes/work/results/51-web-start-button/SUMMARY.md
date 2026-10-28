# Task 51: Web start screen (`html/start_button/*`), summary

Built to the revised spec (`tasks/51-web-start-button.md`, camera-sim's start screen plus the user's styling
additions). **Preload is not in this branch** (task 53). An untested prototype written against the first outline is
kept apart as `for-task-53-startup-hold-prototype.patch` (see for-tasks.md); nothing of it is in the commits.

## What changed

- `platform/web/export/export_plugin.cpp`: Web export options `html/start_button/enabled` (default **off**,
  `update_visibility`), and, shown only while it is ticked: `text` ("Start"), `background_color` (`#3a3a3a`),
  `text_color` (white), `custom_state_colors` (off, `update_visibility`) with `hover_color` / `pressed_color` (shown only
  while that is ticked), `border_color` (white), `border_width` (0 px), `border_radius` (8 px), `font_family` (the
  loading label's Noto Sans stack), `font_size` (24 px), `padding` (Vector2i, 48 x 12 px). Without custom state colors the
  export darkens the background 15% (hover) and 30% (pressed). `_fix_html()` writes them as `config["startButton"]` in
  `$GODOT_CONFIG` (no new placeholder). The config JSON now escapes `<` as `<`, so a text with `</script>` cannot end
  the script (a no-op for every config without `<`).
- `misc/dist/html/full-size.html` (the template's shell) and `webgpu-full-size.html`: a `<button id="status-start">`, a
  `'start'` status mode, styles through `--start-*` custom properties (`cursor: pointer`, `:hover`, `:active`,
  `:focus-visible` 2 px ring in the text color). The old startup branch is now `startEngine()`; with the option on, the
  page shows the start screen and calls it on the press (button disabled first). The missing-features check stays at
  page load. The button is never focused by script. With the option off, `startEngine()` runs at once, as before.
- `platform/web/js/engine/config.js`: `EngineConfig.startButton` parsed and documented (the engine itself ignores it).
- `platform/web/eslint.config.cjs` unchanged (no new placeholder). `doc_classes/EditorExportPlatformWeb.xml`: 13 members.

## Verification (actual results)

- Builds (build lease): editor `scons platform=linuxbsd target=editor dev_mode=yes webgpu=yes accesskit=no` (werror) OK;
  web `template_release` nothreads non-dlink OK. pre-commit on all changed files: pass.
- **Export dialog visibility**, through `EditorExportPreset::_get_property_list()`, the list the dialog's inspector shows
  (an export plugin printing it during a headless export, `scripts/vis_addon`): off → `enabled` only; on → `enabled,
  text, background_color, text_color, custom_state_colors, border_color, border_width, border_radius, font_family,
  font_size, padding`; on + custom state colors → also `hover_color, pressed_color`. Not clicked by hand in a windowed
  editor.
- **Default export HTML**: base editor + template (lead's bin at webgpu-4.8 835f047a) vs this branch, same project.
  `default_html.diff`: the `GODOT_CONFIG` line differs only by the added `startButton` key (and the wasm size); with
  whitespace ignored the rest is the new CSS, the `<button>`, the style setup, one `setStatusMode()` line and the
  `startEngine()` wrapper (the old branch re-indented inside it).
- **Browsers**: every run through `scripts/start_test.mjs` (the camera-sim script, adapted): system Chrome headed with
  the required flags, adapter asserted nvidia / lovelace / not fallback / maxSampledTexturesPerShaderStage 48; Firefox
  headed with `FIREFOX_WEBGPU_PREFS`, adapter asserted non-null. Parent on `localhost:8701` (a tall page, the iframe
  partly below the fold), game on `127.0.0.1:8702`. 8 exports x 2 browsers:
  - default rendering unchanged: base vs new default export, canvas screenshots **pixel-identical** (0 differing px)
    in Chrome and Firefox; the base `full-size.html` as a custom shell with the option on (it has no start code) starts
    straight away and renders identically too.
  - start screen (cross-origin iframe, both shells): before the press `engine.rtenv` unset, canvas without inline
    width, requests only `/index.html`, `/index.js`, `/index.png`; parent `scrollY` 0; nothing focused. After the press
    the button is disabled, the usual loading screen runs, game up 5-10 s later, no console errors other than the
    favicon 404 and the iframe's `screen-wake-lock` permissions-policy message (the engine's keep_screen_on; the test
    parent's iframe does not allow it; unrelated).
  - styles, computed: idle `rgb(58,58,58)`, hover `rgb(49,49,49)`, pressed `rgb(41,41,41)`, `cursor: pointer`;
    custom export: green `#1a8033`, 4 px red border, 20 px radius, 40 px Georgia, padding 20 x 60, yellow text, text
    `Play </script> é` intact; custom state colors: hover `#0000ff`, pressed `#ff00ff80`.
  - keyboard: Tab reaches the button in one press and shows `outline: solid 2px <text color>`; Enter (custom export,
    iframe) and Space (custom-state export, direct) start the game in both browsers.
  - Result: 15/16 PASS. **Firefox, Space held**: the button does not take `:active` while Space is held (Chrome does),
    so the "pressed" shade does not show for Space in Firefox; mouse press does show it in Firefox, and Space still
    starts the game. Browser behavior, left as is (a keydown class could add it; see for-tasks.md).
  - One Chrome run was redone: my first script took an element screenshot while the mouse button was held, which
    scrolled the page and swallowed the click (the game only started 110 s later). Fixed the script (no screenshot while
    held, and it now asserts the press reached the button); the rerun passed. No page change.
  - Screenshots: `screenshots/` (start screen, custom hover and focus, WebGPU shell in the iframe, default running).
- `webgpu_notes/work/smoke.sh`: `SMOKE OK (smoke-20261028-124623-1195911)`: interface 292/292 identical, text-scan
  584/584, 292 modules 0 invalid in Chrome. No shader code touched.

## For the lead

- By hand: tick `HTML > Start Button > Enabled` in a windowed editor's export dialog and watch the options appear.
- Safari not checked (not reachable here).
- Editor and template must match: an old template's shell ignores `startButton` (the game just starts, verified with
  the old shell). The scripts in `scripts/` refer to my scratch paths; adjust before reuse.
- Default looks and two small options are questions in for-tasks.md.
