# Task 69: for TASKS.md

## §4 entry (append to §4.77, or a new §4.x after it)

### 4.x Start screen download line: full opacity, font and size (October 2026) *(work task 69; user answers on task 61)*

The line under the Start button (`#status-preload`) is drawn at full opacity, so `html/start_button/preload_text/color`
shows exactly as picked. Its default is now `#c4c4c4`, which is what the old `#e0e0e0` at 85% gave over the default
boot splash background `#242424` (0.85 x 224 + 0.15 x 36 = 195.8); the CSS text shadow went from alpha 0.6 to 0.51,
which is what the 85% layer made of it. `opacity: 0.999` stays, only to keep Chrome's grayscale text antialiasing (fully
opaque text gets subpixel colour fringes); 0.1% of the background is under half a level, so the colour is exact. New `preload_text/font_family` (default the shells'
`'Noto Sans', 'Droid Sans', Arial, sans-serif`) and `preload_text/font_size` (float, CSS px, default 14.4 = the old
0.9rem at the browsers' 16 px root), shown with the other `preload_text` options while `preload_progress` is on, passed
as `GODOT_CONFIG.startButton.preloadText.fontFamily` / `.fontSize`, applied by both default shells, documented in
`config.js` and `EditorExportPlatformWeb.xml`. A preset saved before this keeps its stored `#e0e0e0` and now shows it
exactly (brighter). Worker: 22/22 browser runs (11 scenarios, Chrome and Firefox
headed, adapter asserted, default splash background, both shells, cross-origin iframe): a chosen colour is drawn exactly
(`#e0e0e0`, `#ff8000`, `#c4c4c4`), `'DejaVu Serif'` 32 px applies; visibility checked with task 51's addon. Defaults
against the webgpu-4.8 shells are **not pixel-identical**: at most 5 levels on 200-600 glyph-edge pixels in Chrome, at
most 2 in Firefox, no colour (text antialiasing depends on the text colour). dev_mode editor and both web templates 0
warnings. Question `preload-text-near-identical`.

## Trap candidates

- **Chrome draws fully opaque text with subpixel (LCD) antialiasing, and text under `opacity` < 1 in grayscale**:
  removing a group opacity added colour fringes up to 75 levels. `will-change`/`translateZ(0)` also give grayscale but
  put the element on its own layer, which snaps it to whole pixels and moved the text by up to 91 levels; `opacity:
  0.999` keeps grayscale without moving anything.
- **A premixed colour cannot reproduce text at reduced opacity to the pixel**: the browsers' text antialiasing depends
  on the text colour, so `#c4c4c4` opaque and `#e0e0e0` at 85% differ by up to 5 levels (Chrome) / 2 (Firefox) on
  glyph edges, though both are (196,196,196) inside the glyphs.
- **Firefox computes font sizes in 1/60 px**: `14.4px` (and `0.9rem`) read back as `14.4063px`. Compare sizes
  numerically.
- **An element's `opacity` also fades its `text-shadow`**: making the line opaque with the same colour math left the
  shadow 15% darker than before. Scale a shadow's alpha by the opacity it loses (0.6 x 0.85 = 0.51) when replacing a
  group opacity with a premixed colour.
- **"Default looks as before" for a premixed colour holds only over the background it was mixed for**: the test
  project of task 61 used a custom boot splash colour, so it could not be the reference for this task; task 69 exports
  a copy with the default background.

## Questions for the user

- `preload-text-near-identical` (choice): the default line at full opacity cannot match the old one to the pixel.
  Glyph edges differ by at most 5 levels in Chrome and 2 in Firefox, invisible side by side
  (`results/69-.../screenshots/chrome_base_go.png` vs `chrome_default_go.png`). **Default: accept**. The alternative is
  for the shell to draw the default colour the old way (`#e0e0e0` at 85%), which is exact but means `#c4c4c4` alone is
  not drawn as picked.
- `preload-text-old-presets` (choice): presets saved before this change store `#e0e0e0` and now draw it exactly,
  brighter than before. **Default: leave them (the colour now shows as set)**; alternative: on load, treat a stored
  `#e0e0e0` as the old default and switch it to `#c4c4c4`.
- `preload-text-size-unit` (choice): `font_size` is in CSS px (like the button's `font_size`), so it no longer follows
  a player's browser default font size the way 0.9rem did. **Default: px**; alternative: rem.
