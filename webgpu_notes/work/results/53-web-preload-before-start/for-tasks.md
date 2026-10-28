# For TASKS.md (task 53)

## Proposed archive entry (§4, next to §4.61)

### 4.xx Web start screen downloads the game while it waits (October 2026) *(work task 53; §2.51)*

`html/start_button/preload` (on) and `html/start_button/preload_progress` (on; shown only while `preload` is) — both
shown only with `html/start_button/enabled`, both passed as `GODOT_CONFIG.startButton.preload` / `.preloadProgress`
(forced `false` without the button). New engine API `Engine.prototype.prefetch(onProgress)`: fetches, through the
`Preloader`, exactly what `startGame()` will ask for (`<exe>.wasm`, the main pack, `shaderPack`, `shaderExtras` at low
priority, and the texture pack `pickTexturePack()` picks from the **adapter**'s features), with no wasm compile and no
GPU device. The `Preloader` now keys downloads by URL (a second request, from `startGame()` or a second
`preloadFile()`, reuses the in-flight or finished one; a second preload to the same path replaces, never duplicates,
its `preloadedFiles` entry), forgets a failed download so the next request refetches it with the usual retries, frees
every buffer once the files are in the engine's file system (`releaseDownloads()`), and tells a progress callback set
late (startGame after a prefetch) the current totals at once, so the bar continues instead of restarting. Shader extras
count only toward the prefetch's own progress, never the loading bar. `Engine.pickTexturePack` is public on
`SafeEngine` and accepts an adapter. Both default shells call `prefetch()` on the start screen unless
`navigator.connection.saveData`, and show "Downloading N%" (never backwards) then "Ready" under the button.
Side fix: a connection cut mid-body used to leave a `Preloader` download waiting forever (an unhandled rejection; the
stream never closed); it now errors the stream, so a buffered download retries and a raw one fails instead of hanging.

## §3 trap candidate (cost < 1 h here, but would again)

- **A headless editor export has no shader pack.** `--headless` has no RenderingDevice, so the export-time bake is
  skipped silently: no `<name>.shaders.pck` / `.shaders_extra.bin`, and `shaderPack` / `shaderExtras` are missing from
  `GODOT_CONFIG`. A test that needs them must export under `xvfb-run -a <editor> --rendering-driver vulkan` (as
  `webgpu_tests/forward_plus/export.sh BAKE=1` does). Task 51's exports were all headless, so none had shader packs.

## Note for the camera-sim session

The fork now does what `web/shell.html`'s START SCREEN preload does, inside the engine (task 53, branch `work/53`,
merged into webgpu-4.8 once integrated). Once camera-sim exports with an editor and templates from that tree:

- Delete the `window.fetch` wrapper (`nativeFetch`, `preloads`, `wakePreload()`, `preload()`, the `window.fetch =`
  override) and the duplicated `pickTexturePack()`. They would still work, but they would now be doubling a download
  the engine already dedups.
- Replace `startPreloading()` with
  `engine.prefetch((loaded, total) => { ... }).then((complete) => { preloadLabel.textContent = complete ? 'Ready' : ''; })`,
  keeping the `navigator.connection.saveData` check in front of it. `prefetch()` fetches exactly what `startGame()`
  will ask for (wasm, main pack, shader pack, shader extras, the adapter's texture pack), never compiles the wasm or
  creates a device, and `startGame()` reuses every download. `Engine.pickTexturePack(packs, adapter)` is public if
  the shell still needs the pack's name.
- Or drop the custom START SCREEN code entirely and use the default shell's: `html/start_button/enabled` with
  `preload` and `preload_progress` (both default on) give the same screen, "Downloading N%" / "Ready" included; the
  button's look is `html/start_button/*` (UniSQ purple through `background_color`). Check camera-sim's other shell
  changes against the fork's `full-size.html` before switching; not compared here.
- Keep the download in the game's page, not the embedding page (Chrome partitions the HTTP cache by top-level site).
