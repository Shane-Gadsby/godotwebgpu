# Task 42: Web loading screen progress bar toggle and colors (TASKS.md §2.49), summary

## What changed

- `platform/web/export/export_plugin.cpp`: four new Web export options. `loading_screen/progress_bar/enabled` (bool, default
  on, refreshes the dialog's visibility when changed), plus `border_color` / `background_color` / `fill_color` (Color),
  which are shown only while it is ticked (`get_export_option_visibility()`). The color defaults are the colors Chrome
  draws an unstyled `<progress>` with, measured: border `#b2b2b2`, background `#efefef`, fill `#0075ff`. `_fix_html()` writes
  them to a **new sibling placeholder** `$GODOT_PROGRESS_BAR`: `{"enabled", "borderColor", "backgroundColor", "fillColor",
  "customColors"}`. The colors are CSS hex, `#rrggbbaa` when alpha < 1. `customColors` is true when any color's HTML string
  differs from its default. The bar is not added to `$GODOT_LOADING_SCREEN`, so a custom shell that walks that object
  as a list of stages is unaffected.
- `misc/dist/html/full-size.html` (the shell the template ships) and `webgpu-full-size.html`: `enabled: false` keeps the bar
  at `display: none`, and in full-size the column closes up. With `customColors`, the shell adds class `custom-colors` and sets
  `--progress-*-color` custom properties: `appearance: none`, 1px border, rounded, 1rem high, `::-webkit-progress-value`
  / `::-moz-progress-bar` for the fill, and a CSS sweep of the fill color while indeterminate. Without it the bar is untouched,
  so each browser keeps its native look. Firefox's own colors are not Chrome's (`#8f8f9d` / `#e9e9ed` / `#3584e4`).
  That is why "defaults" means "unstyled" and not "draw these three colors".
- `platform/web/eslint.config.cjs`: `$GODOT_PROGRESS_BAR` added as a global (otherwise eslint fails on the shells).
- `platform/web/doc_classes/EditorExportPlatformWeb.xml`: the four members. `--doctool` leaves the file unchanged.

## Verification (actual results)

- Builds: editor `scons platform=linuxbsd target=editor dev_mode=yes webgpu=yes accesskit=no` (werror) OK, rebuilt
  after the XML change. Web `template_release` nothreads non-dlink OK. pre-commit on the changed files: all pass.
- **Default export HTML.** Exported the same project with the base editor + base template (lead's bin, webgpu-4.8 @46bd8e96)
  and with this branch's editor + template. The diff (`default_html.diff`) is 60 lines added and 2 changed: the new CSS and JS
  blocks, the `GODOT_PROGRESS_BAR` line, the `display` line in `setStatusMode()`, and `GODOT_CONFIG`, which differs only in
  `fileSizes.index.wasm` (a different wasm build). Rendered output is unchanged: old shell vs new shell with default
  options, at a fixed value, are **pixel-identical** in Chrome and Firefox for both shells. Indeterminate is identical in
  Chrome. In Firefox it differs only by the phase of the browser's native indeterminate animation.
- **Real exports, both browsers.** System Chrome headed with the required flags, adapter asserted as nvidia/lovelace,
  not fallback, maxSampledTexturesPerShaderStage 48. Firefox headed with `FIREFOX_WEBGPU_PREFS`, adapter asserted non-null.
  7 exports per browser: default / custom colors / off, the base (old) `full-size.html` as a custom shell, and
  `webgpu-full-size.html` default / custom / off as a custom shell. **14/14 PASS:** the game starts, there are no page or
  console errors, and the bar state while loading is as expected. Default and old shell: unstyled native. Custom:
  `custom-colors`, red 1px border, green background, `--progress-fill-color #0000ff`. Off: `display: none` throughout.
  Screenshots are in `screenshots/`.
- Stub-engine shell checks, same browser launches and adapter assertions. Colors land at the expected pixels: border top
  edge `#ff0000`, unfilled part `#00ff00`, filled part `#0000ff`, the same in Chrome and Firefox in both shells. The
  indeterminate sweep moves across the bar: blue span at 0 / 400 / 800 ms, e.g. Chrome full-size
  (2-115) → (156-269) → (312-381).
- `webgpu_notes/work/smoke.sh`: `SMOKE OK (smoke-20261028-113117-809677)`. Identical interface 292/292, text-scan
  584/584, 292 modules 0 invalid in Chrome. No shader code is touched.
- Safari: not reachable from this machine, not checked.

**Runs redone (coordinator's flag correction).** My first browser runs had the wrong launch settings. The native-look probe
(`probe.mjs`), the stub shell checks (`stubprobe.mjs`, `oldcmp.mjs`) and the first e2e run all launched Firefox without
the WebGPU prefs. `indet.mjs` launched system Chrome without flags. None of them asserted the adapter. All their results
were discarded and their files deleted. The shell checks were redone as `shellcheck.mjs` and the e2e as `e2e.mjs`, both
browsers, through one launch helper (`launch.mjs`) that uses the required Chrome flags and Firefox prefs and fails the run
on a wrong adapter. Every number above comes from the redone runs.

## What the lead must still run / risks

- By hand in a windowed editor: unticking `Loading Screen > Progress Bar > Enabled` hides the three color pickers. Not
  driven here: headless, no UI. The option is declared with `update_visibility = true`, the same as `variant/thread_support`.
- An export needs a template built from this tree. An old template's shell has no `$GODOT_PROGRESS_BAR`, so the options do
  nothing there. Nothing breaks: placeholder replacement is a no-op. A **new** template exported by an **old** editor
  would leave `$GODOT_PROGRESS_BAR` literal and throw a ReferenceError. That is the same as for every existing placeholder,
  since editor and template must match.
- Picking exactly the three default colors means "native look". In Firefox that is Firefox's own colors, not those three.
  This is documented in the class reference and in for-docs.md.
