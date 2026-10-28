# For the docs (task 42)

## Wiki: Engine-Additions-Web-Export-Options.md

### Options table: add these rows after the three `loading_screen/<stage>/...` rows

```markdown
| `loading_screen/progress_bar/enabled` | bool | `true` | Whether the loading screen shows its progress bar. See [Loading screen](#loading-screen). |
| `loading_screen/progress_bar/border_color` | Color | `#b2b2b2` | The bar's border. Shown only with `loading_screen/progress_bar/enabled`. |
| `loading_screen/progress_bar/background_color` | Color | `#efefef` | The bar's unfilled part. Shown only with `loading_screen/progress_bar/enabled`. |
| `loading_screen/progress_bar/fill_color` | Color | `#0075ff` | The bar's filled part. Shown only with `loading_screen/progress_bar/enabled`. |
```

### Replacement for the whole "## Loading screen" section

```markdown
## Loading screen

The default HTML shell shows a stage name under the boot splash while the page downloads and the engine starts. Each stage has three options under **Loading Screen** in the preset: `text`, `show_text` and `show_spinner`. A hidden text keeps its line, so the progress bar does not move between stages.

| Stage | Default text | Spinner by default |
|---|---|---|
| `initializing_webgpu` | Initializing WebGPU... | No |
| `downloading` | Downloading... | No |
| `starting_engine` | Starting engine... | Yes |
| `initializing_engine` | Initializing engine... | Yes |
| `loading_extensions` | Loading extensions... | Yes |
| `starting_display` | Starting display... | Yes |
| `starting_renderer` | Starting renderer... | Yes |
| `starting_audio` | Starting audio... | Yes |
| `loading_translations` | Loading translations... | Yes |
| `loading_fonts` | Loading fonts... | Yes |
| `registering_types` | Registering types... | Yes |
| `finalizing_setup` | Finalizing setup... | Yes |
| `loading_autoloads` | Loading autoloads... | Yes |
| `loading_main_scene` | Loading main scene... | Yes |
| `rendering_first_frame` | Rendering first frame... | Yes |
| `compiling_shaders` | Compiling shaders... | Yes |

The `downloading` stage has the download percentage appended. `compiling_shaders` is shown when the browser compiles a shader that was not baked at export time, with the number compiled so far appended.

### Progress bar

The progress bar under the stage text has its own options under **Loading Screen > Progress Bar**:

| Option | Default | Effect |
|---|---|---|
| `enabled` | `true` | Shows the bar. When off, the bar is left out and the rest of the loading screen closes up around it. |
| `border_color` | `#b2b2b2` | The bar's 1-pixel border. |
| `background_color` | `#efefef` | The bar's unfilled part. |
| `fill_color` | `#0075ff` | The bar's filled part. |

The three colors are shown in the export dialog only while `enabled` is ticked. Their defaults are the colors Chrome draws a plain progress bar with. While all three stay at their defaults, the bar is not styled at all, so each browser keeps its own look, and exports made before these options existed look the same. Change any of the three and the bar is drawn with all three in every browser: rounded, 1 rem high, with a sweeping block of the fill color while the download size is not yet known. A color with transparency is passed on with its alpha.

### Custom shells

A custom shell receives the stage settings as the `$GODOT_LOADING_SCREEN` placeholder, a JSON object keyed by stage name, with `text`, `showText` and `showSpinner` for each stage.

The progress bar settings come in a placeholder of their own, `$GODOT_PROGRESS_BAR`, a JSON object:

| Key | Value |
|---|---|
| `enabled` | `true` or `false` |
| `borderColor`, `backgroundColor`, `fillColor` | CSS hex colors, `#rrggbb`, or `#rrggbbaa` when the color has transparency |
| `customColors` | `true` when any of the three colors differs from its default |

For example: `{"enabled":true,"borderColor":"#b2b2b2","backgroundColor":"#efefef","fillColor":"#0075ff","customColors":false}`. A custom shell that does not use either placeholder keeps working.
```

## Class reference

Done on the branch: `platform/web/doc_classes/EditorExportPlatformWeb.xml` documents the four new members (checked with
`--doctool`, which leaves the file unchanged).
