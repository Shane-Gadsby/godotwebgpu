# For the docs (task 53)

Builds on task 51's for-docs.md (the Start screen section of the wiki page); apply that first.

## Wiki: Engine-Additions-Web-Export-Options.md

### Options table: add these rows after the `html/start_button/padding` row

```markdown
| `html/start_button/preload` | bool | `true` | Downloads the game while the start screen waits, so after the press only the engine's own startup is left. Shown only with `html/start_button/enabled`. See [Downloading behind the start screen](#downloading-behind-the-start-screen). |
| `html/start_button/preload_progress` | bool | `true` | Shows that download's progress under the button: "Downloading 48%", then "Ready". Shown only with `html/start_button/preload`. |
```

### Start screen section: replace the sentence "Downloading the game in the background while the start screen waits is not part of this option yet." with

```markdown
### Downloading behind the start screen

With `html/start_button/preload` (on by default), the page downloads what the game needs while the start screen waits: the engine, the main pack, the baked shader packs and the one texture pack this browser will load. After the press, only the engine's own startup is left. The engine itself still runs only on the press: its WebAssembly is not compiled and no GPU device is created before it. The texture pack is chosen from the GPU adapter's features, which give the same answer as the device the engine creates later.

A press before the download finishes carries on with it: nothing is downloaded twice, and the loading bar continues from what has already arrived. A file that fails to download is fetched again when the player presses Start. With `preload_progress`, a line under the button says "Downloading 48%", then "Ready".

The download waits for the press, as without the option, when the browser is in Save-Data mode (the player asked their browser to save data).

The download has to run in the game's own page. Browsers keep a separate HTTP cache for each site that embeds a page, so a parent page that fetches the same files would not speed up a cross-origin iframe of the game. If you embed the game, leave the downloading to the exported page.
```

### Custom shells subsection (from task 51): add these rows to the key table

```markdown
| `preload` | `true` when the page should download the game while it waits (`false` unless `enabled` is `true`) |
| `preloadProgress` | `true` when the page should show that download's progress (`false` unless `preload` is `true`) |
```

and this paragraph after the table:

```markdown
A custom shell starts the download with `engine.prefetch(onProgress)`, which fetches the files `engine.startGame()` will need and nothing else: no WebAssembly compile, no GPU device. `onProgress(loaded, total)` reports bytes, as `EngineConfig.onProgress` does (a total of 0 means unknown), and the returned promise resolves once every download has finished, to `true` when they all arrived or `false` when one failed (that file is then fetched again by `startGame()`). It never rejects. A later `startGame()`, on the same `Engine` or a new one, reuses every download, finished or still under way. `Engine.pickTexturePack(texturePacks, adapterOrDevice)` returns the texture pack the engine will load, if a shell needs to know it.
```

## Engine docs (`platform/web/js/engine/engine.js`, generated JS reference)

Done in the branch: `Engine.prototype.prefetch()` and `Engine.pickTexturePack()` are documented in their JSDoc
(the latter is no longer `@ignore`), and `EngineConfig.startButton` lists `preload` / `preloadProgress`.
`doc_classes/EditorExportPlatformWeb.xml` has both options, and `html/start_button/enabled` no longer says the engine
is not downloaded before the press.
