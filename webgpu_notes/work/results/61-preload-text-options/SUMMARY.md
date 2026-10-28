# Task 61: the start screen's download text, configurable: summary

## What changed

- `platform/web/export/export_plugin.cpp`: seven options under `html/start_button/preload_text/` (the inspector
  shows them as a "Preload Text" group under Start Button):
  `downloading` (bool, on) + `downloading_text` ("Downloading..."), `progress` (bool, on) + `progress_text`
  ("Downloading {percent}%"), `ready` (bool, on) + `ready_text` ("Ready"), and `color` (`#e0e0e0`, the color
  `#status-preload` already had in both shells' CSS). They are shown only while `enabled`, `preload` and
  `preload_progress` are all on, and each `*_text` only while its switch is on. `preload_progress` and the three
  switches now carry `update_visibility`. Written as `GODOT_CONFIG.startButton.preloadText = {downloading, progress,
  ready, color}`. A message switched off is written as `""`.
- Both default shells (`full-size.html`, `webgpu-full-size.html`) read `preloadText`. A missing key (an old config or a
  custom one) gets the old default. `{percent}` is replaced everywhere it appears (`split/join`), and a text without
  it is shown as it is. The text goes in with `textContent`, and the color is set only when the config has one. An
  empty message leaves the line empty. The line's `min-height` keeps its height, so nothing moves. Font, size, the
  0.85 opacity and the shadow stay in the CSS.
- `config.js` (`startButton.preloadText`) and `EditorExportPlatformWeb.xml` (seven new members, and
  `preload_progress` points to them) are documented.

## Verification (actual results)

- **Builds** (build lease, ccache): editor `dev_mode=yes accesskit=yes webgpu=yes gdscript_braces=yes linker=mold`
  (werror) OK. The web `template_debug` **and `template_release`** (nothreads, non-dlink, braces, raycast) are OK, with
  0 compiler warnings in any log. pre-commit is clean on every changed file. The smoke test was not run, since no
  shader code changed (the task file says so).
- **Export dialog visibility** (`scripts/vis.sh`, `EditorExportPreset::_get_property_list()` through task 51's
  addon, headless export): off gives `enabled` only. On gives `... preload, preload_progress, preload_text/downloading,
  downloading_text, progress, progress_text, ready, ready_text, color`. With `preload` off, the list stops at
  `preload`. With `preload_progress` off, it stops at `preload_progress`. Each switch off hides only its own `_text`,
  and all three off leaves the switches + `color`. The button off with stored `preload_text` keys gives `enabled` only.
  Nobody clicked through it in a windowed editor.
- **Browsers** (`scripts/preload_text_test.mjs`, `run_all.sh`, gpu lease): system Chrome ran headed with the required
  flags, and its adapter was asserted nvidia / lovelace / not fallback / 48. Firefox ran headed with
  `FIREFOX_WEBGPU_PREFS`, and its `requestAdapter()` was non-null. The test server gates every file: *hold* (no file
  answers), *half* (headers + half of each file, held), and *go*. The test checks the line in each state (text, color,
  no child elements, top/height). Every `textContent`/`innerHTML` write to the line is logged through an init script.
  Then it presses Start and waits for the game. The project is a baked Forward+ test project, exported with the
  release template under `xvfb-run --rendering-driver vulkan`. **34/34 PASS** (17 scenarios x 2 browsers, `runs/`):
  defaults; defaults from a config with `preloadText` removed (`nokey`); edited texts ("Fetching the game",
  `Got {percent}% ({percent} of 100)` gives "Got 49% (49 of 100)", `<b>Go!</b> & play` shown literally, color `#ff8000`),
  also in a cross-origin iframe; `Please wait` (no `{percent}`); each message off; all off; the
  `webgpu-full-size.html` shell (defaults and edited); and four copies with `fileSizes` stripped, the only way the
  "Downloading..." state holds still (with the sizes in the config, the percentage replaces it within a frame). The
  percentage never went backwards in any run. Game up 0.68-1.0 s after the press.
- **Defaults pixel-identical** (`scripts/compare.py`, `runs/compare.txt`): the same export with the webgpu-4.8 shell
  (the release zip with its `godot.html` swapped for webgpu-4.8's, or `custom_html_shell` set to webgpu-4.8's
  `webgpu-full-size.html`) against the new one, in the hold/half/go states. Base vs default, base vs nokey,
  base_nosize vs default_nosize and wg_base vs wg_default are **0 pixels different in all 24 comparisons**, in both
  browsers. The button rect and the line top/height in every direct run matched that shell's default run, including
  all-off and the long edited texts.

## For the lead

- By hand: in a windowed editor, tick Start Button > Enabled and watch Preload Text appear and hide with Preload /
  Preload Progress and each switch.
- Editor and templates must come from the same tree. An older template's shell ignores `preloadText` and shows the old
  hard-coded messages.
- Not checked: Safari, and a real network.
- The new editor's builds printed "screen reader support driver requires dependencies" (no AccessKit in the fresh
  worktree's `bin/build_deps`). This is unrelated.
