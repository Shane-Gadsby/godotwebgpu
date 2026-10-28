# Task 69: for TASKS.md

## §4 entry (append to §4.77, or a new §4.x after it)

### 4.x Start screen download line: full opacity, font and size (October 2026) *(work task 69; user answers on task 61)*

The line under the Start button (`#status-preload`) is drawn at full opacity, so `html/start_button/preload_text/color`
shows exactly as picked. Its default is now `#c4c4c4`, which is what the old `#e0e0e0` at 85% gave over the default
boot splash background `#242424` (0.85 x 224 + 0.15 x 36 = 195.8); the CSS text shadow went from alpha 0.6 to 0.51,
which is what the 85% layer made of it, so glyph edges match too. New `preload_text/font_family` (default the shells'
`'Noto Sans', 'Droid Sans', Arial, sans-serif`) and `preload_text/font_size` (float, CSS px, default 14.4 = the old
0.9rem at the browsers' 16 px root), shown with the other `preload_text` options while `preload_progress` is on, passed
as `GODOT_CONFIG.startButton.preloadText.fontFamily` / `.fontSize`, applied by both default shells, documented in
`config.js` and `EditorExportPlatformWeb.xml`. A preset saved before this keeps its stored `#e0e0e0` and now shows it
exactly (brighter). RESULTS_LINE

## Trap candidates

- **An element's `opacity` also fades its `text-shadow`**: making the line opaque with the same colour math left the
  shadow 15% darker than before. Scale a shadow's alpha by the opacity it loses (0.6 x 0.85 = 0.51) when replacing a
  group opacity with a premixed colour.
- **"Default looks as before" for a premixed colour holds only over the background it was mixed for**: the test
  project of task 61 used a custom boot splash colour, so it could not be the reference for this task; task 69 exports
  a copy with the default background.

## Questions for the user

- `preload-text-old-presets` (choice): presets saved before this change store `#e0e0e0` and now draw it exactly,
  brighter than before. **Default: leave them (the colour now shows as set)**; alternative: on load, treat a stored
  `#e0e0e0` as the old default and switch it to `#c4c4c4`.
- `preload-text-size-unit` (choice): `font_size` is in CSS px (like the button's `font_size`), so it no longer follows
  a player's browser default font size the way 0.9rem did. **Default: px**; alternative: rem.
