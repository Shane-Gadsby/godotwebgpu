# 53-web-preload-before-start: download the game while the start screen waits

State: claimed
Owner: worker-53
Claimed: 2026-10-28 13:29
Needs: build (web template + editor), browser (Playwright), **after 51-web-start-button**
Branch: work/53
Results: webgpu_notes/work/results/53-web-preload-before-start/

User request, 2026-10-28. camera-sim does this already in `web/shell.html` (search `START SCREEN`). This task
moves it into the engine: as an export option, and with a proper engine API instead of camera-sim's
`window.fetch` wrapper.

## Goal

While the start screen (51-web-start-button) waits for the press, download everything `startGame()` will
need. After the press, only the engine's own startup is left. The engine still doesn't *run* before the
press: no wasm compile, no GPU device.

### Export options

| Option | Type | Default | Shown |
|---|---|---|---|
| `html/start_button/preload` | bool | true | only when `html/start_button/enabled` is ticked |
| `html/start_button/preload_progress` | bool | true | only when both of the above are ticked |

- `preload` has no effect without the start button: without one, the engine starts immediately anyway. Hide
  it in `get_export_option_visibility()` unless `html/start_button/enabled` is ticked, and declare `preload`
  with `update_visibility = true` so `preload_progress` follows it.
- `preload_progress` shows "Downloading 48%" under the button, then "Ready". camera-sim shows it.
- Pass both through `$GODOT_CONFIG` (alongside the start-button settings) and document them.

### Engine API (`platform/web/js/engine/engine.js`, `preloader.js`)

camera-sim currently has to wrap `window.fetch` and hand pre-fetched bytes back to the engine's own fetches.
Replace that with:
- **`Engine.prototype.prefetch()`.** It starts fetching, through the existing `Preloader`, exactly the files
  `startGame()` will ask for:
  - `${executable}.wasm`;
  - the main pack;
  - `shaderPack`;
  - `shaderExtras`;
  - the texture pack `pickTexturePack()` chooses.

  Each is tracked so a later `startGame()` reuses the in-flight or finished download, and never fetches it
  twice. (Today `preloadFile()` called twice for the same URL downloads it twice, and pushes two entries into
  `preloadedFiles`.)
- **Choosing the texture pack before there's a GPU device.** `pickTexturePack()` reads `device.features`, but
  no device should exist before the press. Use the `navigator.gpu.requestAdapter()` adapter's features. The
  device enables every compression family the adapter has (`requestWebGPUDevice`'s `optionalFeatures`), so
  the choice is the same. Also expose `Engine.pickTexturePack` on `SafeEngine`; camera-sim's shell currently
  duplicates its logic because it isn't reachable.
- **Progress:** a callback for the shell's "Downloading N%" line. When `startGame()` takes over, its
  `onProgress` should continue from what's already loaded, not restart from 0.
- **Failures:** a file that fails during prefetch is fetched again normally by `startGame()`, keeping the
  existing retry behaviour.

### Shell

- Call `prefetch()` when the start screen shows, unless `navigator.connection.saveData` is true. In that case
  the download waits for the press, as it does today.
- A press before the download finishes carries on with it.

## Why it has to be in the game's own page

Chrome partitions the HTTP cache by top-level site (and frame site). A parent page prefetching the same
files would not speed up a cross-origin iframe of the game, so the prefetch must run inside the game's own
document. Put that sentence in the docs, because embedders will ask.

## Verification

- Two origins (`test-scripts/start_test.mjs`). Log every request the game server receives:
  - with the download finished before the press: **each file is requested exactly once**;
  - the engine stays idle (`engine.rtenv` unset) for at least 5 s after the download finishes;
  - after the press, time to the first frame is about the same as an export where everything is cached.
- Throttle the server to ~40 MB/s, so the whole download takes ~10 s, and press at ~2 s:
  - each file is still requested once;
  - the loading bar keeps moving;
  - the game starts when the download completes.
- Save-Data on (DevTools network conditions, or `navigator.connection` stubbed): nothing is fetched before
  the press.
- Option combinations in the export dialog: `preload` is hidden while the start button is off;
  `preload_progress` is hidden while `preload` is off.
- camera-sim results for comparison:
  - each file requested once in all three runs;
  - the 40 MB/s download finished around 10 s, and the engine started right after;
  - the export's shader cache didn't match the template at the time, so camera-sim's startup times aren't a
    baseline.

Test scripts: `webgpu_notes/work/inputs/camera-sim-2026-10-28/test-scripts/` (copied from the camera-sim session). Browser rule: `webgpu_notes/work/README.md`.
