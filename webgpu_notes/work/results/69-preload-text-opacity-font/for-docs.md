# Task 69: wiki text (Engine-Additions-Web-Export-Options.md)

## 1. "New options" table: replace the `preload_text/color` row with these three rows

| `html/start_button/preload_text/color` | Color | `#c4c4c4` | The line's text color, drawn at full opacity, so it shows exactly as picked. |
| `html/start_button/preload_text/font_family` | String | `'Noto Sans', 'Droid Sans', Arial, sans-serif` | The line's CSS `font-family` list. |
| `html/start_button/preload_text/font_size` | float | `14.4` | The line's font size, in CSS pixels. |

## 2. "The download line's text": replace the paragraph that starts '`preload_text/color` sets the line's text color' and the one after it with

`preload_text/color` sets the line's text color, `preload_text/font_family` its font (a CSS `font-family` list, such as `'Courier New', monospace`; a font the player's browser does not have falls back to the next one in the list) and `preload_text/font_size` its size in CSS pixels. The line keeps its height for its font size, so a bigger size makes the line taller and moves the button up a little to keep both centered. The line is drawn at full opacity, so the color you pick is the color on screen. The messages are inserted as plain text, so `<b>` shows up as typed and does not make anything bold.

The defaults give the same start screen as before these options existed: the line used to be `#e0e0e0` at 85% opacity, which over the default boot splash background is `#c4c4c4`, the default color now, in the same 14.4 px (0.9rem) font. Over another boot splash background color the old line took on 15% of that background, so there the default line differs from the old one by 15% of the difference between that background and `#242424` (two or three levels per channel on a dark background); pick the color you want to see. A preset saved before full opacity has `#e0e0e0` stored as its color and now shows that color exactly, a little brighter than before; set the color to `#c4c4c4` to get the old look back.

## 3. "Start screen in a custom shell": replace the `preloadText` row of the key table with

| `preloadText` | `{ downloading, progress, ready, color, fontFamily, fontSize }`: the line's three messages as the shell should show them (an empty string is a message switched off; `progress` still contains `{percent}`), its CSS hex color, its CSS `font-family` list and its font size in CSS pixels (a number) |

And in the example, replace the line `status.style.color = text.color;` with:

```js
status.style.color = text.color;
status.style.fontFamily = text.fontFamily;
status.style.fontSize = `${text.fontSize}px`;
```
