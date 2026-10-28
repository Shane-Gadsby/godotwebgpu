# Task 69: the start screen's download line at full opacity, with a font family and size: summary

## What changed

- `platform/web/export/export_plugin.cpp`: `html/start_button/preload_text/color` now defaults to **`#c4c4c4`**. That
  is the old `#e0e0e0` at 85% over the default boot splash background `#242424`: 0.85 x 224 + 0.15 x 36 = 195.8, which
  rounds to 196. Two new options follow the other `preload_text` options (shown only while `enabled`, `preload` and
  `preload_progress` are on): `font_family` (String, default the shells' `'Noto Sans', 'Droid Sans', Arial,
  sans-serif`) and `font_size` (float, CSS px, `1,128,0.1,or_greater`, default `14.4`, which is the old `0.9rem` at the
  browsers' 16 px root). Both are written as `GODOT_CONFIG.startButton.preloadText.fontFamily` / `.fontSize`, and the
  JSON has `14.4`, not a float32 tail, because the constant is a double.
- Both default shells: `#status-preload` loses `opacity: 0.85`. It gets `color: #c4c4c4` and
  `text-shadow ... rgba(0,0,0,0.51)`, since the 85% layer also faded the 0.6 shadow, and 0.6 x 0.85 = 0.51. It also
  gets **`opacity: 0.999`**, and the CSS comment says why. That value only keeps Chrome's grayscale text antialiasing.
  Chrome draws fully opaque text over an opaque background with subpixel (LCD) antialiasing, which gave colour fringes
  up to 75 levels against the old line. 0.1% of any background is under half a level, so the colour still comes out
  exactly: the exact-pixel checks below pass. `will-change` would also give grayscale, but it moves the line onto its own
  layer, which snaps to whole pixels and shifted the text by up to 91 levels. The script applies `fontFamily` and
  `fontSize` (as px) when the config has them, so an old or custom config keeps the CSS defaults.
- Documentation is in `config.js` (`preloadText` keys and defaults) and in `EditorExportPlatformWeb.xml` (two new
  members, plus the text for `color` and `preload_progress`).

## Verification (actual results)

- **Builds** (build lease, ccache): the editor built with `dev_mode=yes accesskit=yes webgpu=yes gdscript_braces=yes
  linker=mold` and passed (werror). The web `template_debug` and **`template_release`** built (nothreads, non-dlink,
  braces, raycast), twice, with 0 compiler warnings. The editor log has 6 "XML failed validation" lines, which come
  from wayland-scanner and are also in task 61's log. pre-commit is clean. The C++ did not change after those builds.
  The last shell edit (`will-change` to `opacity: 0.999`) was tested through the template zip with its `godot.html`
  swapped for this tree's `full-size.html` (`scripts/base_zip.py`), which is byte for byte what the build packs: the
  first build's zip had full-size.html verbatim. So the `bin/` zips in the worktree still carry the `will-change` shell.
- **Option visibility** (`scripts/vis.sh`, `runs/visibility.txt`): `font_family` and `font_size` show with the line
  (`on`, and each message switched off). They are hidden with `preload` off, with `preload_progress` off, and with the
  button off.
- **Browsers** (`scripts/run_all.sh` + the extended `preload_text_test.mjs`, gpu lease). Chrome was system Chrome,
  headed, with the required flags; the adapter was asserted nvidia/lovelace/not fallback/48. Firefox was headed with
  `FIREFOX_WEBGPU_PREFS`; `requestAdapter()` was non-null. The project is task 61's test project with the **default
  boot splash background**, because task 61's project set its own background. Exports were made under
  `xvfb-run --rendering-driver vulkan`. **22/22 PASS** (11 scenarios x 2 browsers, `runs/run_all.txt`). On the first
  run, Firefox `styled_iframe` closed the browser after the press, after every line check had passed, and passed on the
  rerun. The test checks text, colour, opacity, font family, font size (numerically: Firefox reads 14.4 as 14.4063px,
  in the old shell's 0.9rem too), layout, the message sequence, and the game coming up after the press.
  - **A chosen colour reads back exactly** (`compare.py`): the line has pixels exactly at the colour and none brighter.
    Chrome: `exact_e0` (old presets' `#e0e0e0`) 30 px of (224,224,224), `styled` `#ff8000` 386 px, default 30 px of
    (196,196,196). Firefox: 17 / 376 / 17 px.
  - **Font family and size apply**: `styled` and `wg_styled` (`'DejaVu Serif', serif`, 32 px, both shells, also in a
    cross-origin iframe) computed `"DejaVu Serif", serif` / `32px`. The drawn text width equals a canvas measure in that
    font (within 0.6 px) and differs from the default family's. The line is taller, as it should be, and nothing else
    moved.
- **Defaults against today's start screen** (`runs/compare_strict.txt`, `runs/compare_tol5.txt`): the comparison is
  the same export with the webgpu-4.8 shells, which hold the old `#e0e0e0` at 85%, against the new defaults, in the
  hold/half/go states. The pairs are base/default, base/nokey (no `preloadText` in the config), the two `fileSizes`-
  stripped exports, and the two webgpu-full-size shells. **Not pixel-identical**:
  - Chrome: 202-604 pixels differ, **at most 5 levels**, all on glyph edges, darker, with no colour (max chroma 0).
  - Firefox: 335-927 pixels differ, **at most 2 levels**.
  - With `--tolerance 5`, all 24 comparisons pass.

  Exact identity is not reachable with a premixed colour at full opacity. The browsers' text antialiasing depends on
  the text colour (Skia's contrast/gamma), so `#c4c4c4` drawn opaque and `#e0e0e0` drawn into a layer at 85% cover the
  same edge pixels differently. Firefox also rounds the intermediate layer. `runs/aa_variants.txt` shows seven CSS
  variants on a static page; the best of them is the 5 / 1 levels above.

## Not run (test scope)

No Forward+ matrix, scene smoketest, perf_guard or smoke.sh (no shader touched). No windowed editor check of the
export dialog, no Safari. Press-to-game was 4-11 s in these runs, in both the old-shell and new-shell exports
(the engine and the press path are unchanged).

## For the lead

- The user should judge the defaults result: 5 levels on glyph edges in Chrome and 2 in Firefox, against "pixel-
  identical". The question is in for-tasks.md.
- Presets saved before this change store `#e0e0e0` and now draw it exactly, so their line is brighter (as the task
  asked). This is also in for-tasks.md as a question.
- The editor and templates must come from the same tree. An old template's shell ignores `fontFamily`/`fontSize` and
  draws the stored colour at 85%.
