# Task 61: wiki text (Engine-Additions-Web-Export-Options.md)

## 1. "New options" table: replace the `html/start_button/preload_progress` row with these rows

| `html/start_button/preload_progress` | bool | `true` | Shows that download's progress under the button: "Downloading 48%", then "Ready". Shown only with `html/start_button/preload`. See [The download line's text](#the-download-lines-text). |
| `html/start_button/preload_text/downloading` | bool | `true` | Shows `downloading_text` before the download's size is known. Shown only with `preload_progress`, as are all the `preload_text` options. |
| `html/start_button/preload_text/downloading_text` | String | `Downloading...` | That message. Shown only with `preload_text/downloading`. |
| `html/start_button/preload_text/progress` | bool | `true` | Shows `progress_text` while the files download. |
| `html/start_button/preload_text/progress_text` | String | `Downloading {percent}%` | That message; every `{percent}` becomes the percentage. Shown only with `preload_text/progress`. |
| `html/start_button/preload_text/ready` | bool | `true` | Shows `ready_text` once the game has downloaded. |
| `html/start_button/preload_text/ready_text` | String | `Ready` | That message. Shown only with `preload_text/ready`. |
| `html/start_button/preload_text/color` | Color | `#e0e0e0` | The line's text color. |

## 2. "Downloading behind the start screen": replace the sentence 'With `preload_progress`, a line under the button says "Downloading 48%", then "Ready".' with

With `preload_progress`, a line under the button says "Downloading 48%", then "Ready". The next section shows how to change those messages or turn them off.

## 3. New subsection, right after "Downloading behind the start screen" (before "Start screen in a custom shell")

### The download line's text

The line under the button has three messages, each with a switch and a text under **Html > Start Button > Preload Text** in the export dialog (shown while `preload_progress` is on):

| State | Switch | Text option | Default |
|---|---|---|---|
| The download has started, but its size is not known yet | `downloading` | `downloading_text` | `Downloading...` |
| The files are downloading | `progress` | `progress_text` | `Downloading {percent}%` |
| Every file has arrived | `ready` | `ready_text` | `Ready` |

In `progress_text`, every `{percent}` is replaced by the whole percentage downloaded so far. The percentage never goes backwards and stays at 99 until the last file has arrived. A text without `{percent}`, such as `Please wait`, is shown as it is. A message that is switched off leaves the line empty in its state. The line keeps its height, so the button does not move. If a file fails to download, the line is emptied, whatever `ready` says, because that file is downloaded again when the player presses Start.

The export writes the size of each file into the page, so the percentage usually appears within a frame and the `downloading` message is rarely seen. A custom shell's config without `fileSizes` has no total and shows the `downloading` message until the game is ready.

`preload_text/color` sets the line's text color (default `#e0e0e0`, the color it always had). The line keeps the default shell's font, size, 85% opacity and text shadow. The messages are inserted as plain text, so `<b>` shows up as typed and does not make anything bold.

The defaults give the same start screen as before these options existed, and a preset saved before them loads with the defaults.

## 4. "Start screen in a custom shell": add a row to the key table, after `preloadProgress`

| `preloadText` | `{ downloading, progress, ready, color }`: the line's three messages as the shell should show them (an empty string is a message switched off; `progress` still contains `{percent}`) and its CSS hex color |

And in the example, replace the `engine.prefetch(...)` call with:

```js
const text = GODOT_CONFIG.startButton.preloadText;
status.style.color = text.color;
engine.prefetch((loaded, total) => {
	status.textContent = total ? text.progress.split('{percent}').join(Math.floor(100 * loaded / total)) : text.downloading;
}).then((complete) => { status.textContent = complete ? text.ready : ''; });
```
