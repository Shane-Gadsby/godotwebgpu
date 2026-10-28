# For the docs (task 51)

## Wiki: Engine-Additions-Web-Export-Options.md

### Options table: add these rows after the `html/orientation` row

```markdown
| `html/start_button/enabled` | bool | `false` | Opens the page on a start screen; nothing of the engine runs until the player presses Start. See [Start screen](#start-screen). |
| `html/start_button/text` | String | `Start` | The button's text. Shown only with `html/start_button/enabled`, as are all the options below. |
| `html/start_button/background_color` | Color | `#3a3a3a` | The button's background. |
| `html/start_button/text_color` | Color | `#ffffff` | The button's text, and its keyboard focus ring. |
| `html/start_button/custom_state_colors` | bool | `false` | Uses `hover_color` and `pressed_color` instead of darker shades of the background. |
| `html/start_button/hover_color` | Color | `#313131` | The background while the pointer is over the button. Shown only with `custom_state_colors`. |
| `html/start_button/pressed_color` | Color | `#292929` | The background while the button is pressed. Shown only with `custom_state_colors`. |
| `html/start_button/border_color` | Color | `#ffffff` | The button's border. |
| `html/start_button/border_width` | int | `0` | The border's width in CSS pixels; `0` draws none. |
| `html/start_button/border_radius` | int | `8` | The corners' radius in CSS pixels. |
| `html/start_button/font_family` | String | `'Noto Sans', 'Droid Sans', Arial, sans-serif` | A CSS `font-family` list. |
| `html/start_button/font_size` | int | `24` | The text size in CSS pixels. |
| `html/start_button/padding` | Vector2i | `(48, 12)` | Space around the text in CSS pixels: `x` left and right, `y` above and below. |
```

### New section, after "## Orientation and fullscreen"

```markdown
## Start screen

With `html/start_button/enabled`, the exported page opens like itch.io's "Run game": the boot splash with a Start button under it. Nothing of the engine runs until the player presses it. The engine is not downloaded or compiled, and no GPU device or audio context is created, so a page that embeds the game costs a visitor nothing until they choose to play. Pressing the button runs the usual startup, with the same loading screen, stages and progress bar.

The press is a user gesture inside the page, so the game's audio can start without waiting for another click.

The button is a real HTML button. <kbd>Tab</kbd> reaches it and shows a focus ring in the text color, and <kbd>Enter</kbd> or <kbd>Space</kbd> presses it. It is not focused when the page loads, because inside an iframe that would scroll the parent page down to the game. A browser that lacks something the export needs, such as WebGPU, shows the error straight away instead of the button.

The pointer turns into a hand over the button. The background turns 15% darker under the pointer and 30% darker while the button is pressed. Tick `custom_state_colors` to choose those two colors yourself.

Downloading the game in the background while the start screen waits is not part of this option yet.

### Custom shells

The settings reach the shell as `startButton` in `$GODOT_CONFIG`, so a custom shell can read them as `GODOT_CONFIG.startButton`:

| Key | Value |
|---|---|
| `enabled` | `true` or `false` |
| `text` | The button's text |
| `backgroundColor`, `hoverColor`, `pressedColor`, `textColor`, `borderColor` | CSS hex colors, `#rrggbb`, or `#rrggbbaa` when the color has transparency. `hoverColor` and `pressedColor` are already darkened from the background unless the preset sets its own. |
| `borderWidth`, `borderRadius`, `fontSize`, `paddingHorizontal`, `paddingVertical` | Numbers, in CSS pixels |
| `fontFamily` | A CSS `font-family` list |

The engine does not act on these keys itself: a custom shell that ignores them starts the game straight away, as before.
```
