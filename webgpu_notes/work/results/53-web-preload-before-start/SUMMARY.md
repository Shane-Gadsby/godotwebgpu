# Task 53: download the game while the start screen waits, summary

## What changed

- `platform/web/js/engine/engine.js`: **`Engine.prototype.prefetch(onProgress)`** fetches, through the `Preloader`,
  exactly what `startGame()` will ask for: `<exe>.wasm` (via `Engine.load`, so the wasm is downloaded but not compiled),
  the main pack, `shaderPack`, `shaderExtras` (low priority) and the texture pack `pickTexturePack()` picks from the
  **`requestAdapter()` adapter**'s features (new internal `Engine.chooseTexturePack`). No device, no compile. It returns
  one promise (repeat calls get the same one) that resolves `true`/`false` (all arrived / one failed) and never rejects;
  the progress callback gets its final report before the promise resolves and is then detached.
  `Engine.pickTexturePack` is public on `SafeEngine` and documented (it accepts a device or an adapter). `Engine.load`
  forgets a wasm download that failed (headers or body), so `startGame()` fetches it again. `loadShaderExtras()` takes
  over a prefetched extras download (or falls back to its own fetch if that one fails). `start()` releases every buffered
  download once the files are in the file system.
- `platform/web/js/engine/preloader.js`: downloads keyed by URL (`fetchFile`): a second request reuses the in-flight or
  finished one. A second preload to the same path replaces its `preloadedFiles` entry, so nothing is written twice. A
  failed download is removed from the map and from the progress, so the next request refetches it with the usual 4
  attempts. `takeDownload` / `releaseDownloads` free the buffers. A progress callback set late is told the current
  totals on the next frame, so `startGame()`'s `onProgress` continues from what the prefetch already loaded. Extras go
  to a separate tracker that only the prefetch's progress counts, never the loading bar. There is one rAF loop at most.
  **Side fix:** a connection cut mid-body used to raise an unhandled rejection and leave the stream open forever, so
  the download hung. It now errors the stream: a buffered download retries and a raw one fails. Node harness: base
  preloader crashes with an unhandled `TypeError: terminated`; new one retries (`scripts/preloader_test.mjs`, 10/10).
- `platform/web/export/export_plugin.cpp`: `html/start_button/preload` (bool, true, `update_visibility`) and
  `html/start_button/preload_progress` (bool, true). Both are shown only with `enabled`, and `preload_progress` only
  with `preload`. Written as `startButton.preload` / `startButton.preloadProgress`, forced false without the button.
- Both default shells (`full-size.html`, `webgpu-full-size.html`): when the start screen shows, `engine.prefetch()`
  runs unless `navigator.connection.saveData` is set. With `preloadProgress`, a `#status-preload` line
  (`aria-live=polite`) under the button shows "Downloading N%" (it never goes backwards and stops at 99 until done),
  then "Ready", or nothing if a file failed (that file is fetched again on the press). The comment carries the
  cache-partitioning sentence.
- `config.js` (`startButton.preload*`) and `EditorExportPlatformWeb.xml` (both options, plus `enabled` no longer says
  the engine isn't downloaded) are documented.

## Verification (actual results)

- Builds (build lease): editor `scons platform=linuxbsd target=editor dev_mode=yes webgpu=yes accesskit=no` (werror) OK.
  Web `template_release` nothreads non-dlink OK, rebuilt after the last JS change (`scripts/build.sh`). pre-commit is
  clean on every changed file.
- Export dialog visibility, through `EditorExportPreset::_get_property_list()` (task 51's `vis_addon`, headless export):
  with the button off, only `enabled`. On: `... padding, preload, preload_progress`. On with preload off:
  `... padding, preload` (no `preload_progress`). On with progress off: both shown. Config output: off gives
  `preload:false, preloadProgress:false`, and so does preload off. Progress off gives `true/false`. Not clicked by hand
  in a windowed editor.
- **Browsers** (`scripts/preload_test.mjs`, `scripts/run_all.sh`, gpu lease). System Chrome ran headed with the
  required flags, and its adapter was asserted nvidia / lovelace / not fallback / maxSampledTexturesPerShaderStage 48.
  Firefox ran headed with `FIREFOX_WEBGPU_PREFS`, and its `requestAdapter()` was non-null. The parent was
  `localhost:8701` with the game in an iframe on `127.0.0.1:8702`, unless noted. Probes ran in every frame through an
  init script: WebAssembly compile/instantiate calls, `GPUAdapter.requestDevice` calls with a stack, and trusted
  click/keydown events. The test project is Forward+ with a VRAM-compressed texture (both families, separate packs) and
  a real bake (the export ran under `xvfb-run --rendering-driver vulkan`). Files: wasm 44.6 MB, shaders.pck 10.7 MB,
  shaders_extra.bin 29.7 MB, textures_bc/etc2 0.7 MB each, pck 6 KB. **Final round: 18/18 PASS** (9 scenarios x 2
  browsers, `runs/`):

  | Scenario | Chrome | Firefox |
  |---|---|---|
  | ready: "Ready", then 5 s idle (`rtenv` unset, 0 wasm compiles, 0 `requestDevice`), press | each file once; up **0.89 s** after the press | each file once; up **0.73 s** |
  | ready with `Cache-Control` | each once; 0.89 s | each once; 0.76 s |
  | warm-cache baseline (preload off; second visit, press) | **0.90 s** (Chrome's incognito context still refetched the 3 big files, from localhost) | **0.70 s** (nothing refetched) |
  | preload off, first visit: nothing fetched before the press | yes; 0.98 s | yes; 0.75 s |
  | early: 4.5 MB/s per connection, press at 2.0 s | download 10.5 s; bar 0.121 to 0.746 over 80 samples, 0 backwards, 0 stalls over 1.5 s; overlay gone 0.20 s after the last byte; each file once | 10.3 s; same bar figures; 0.10 s; each once |
  | Save-Data (`navigator.connection` stubbed) | only html/js/png before the press, no label; each once after | same |
  | `preload_progress` off (direct) | no line; files prefetched; idle 5 s; each once | same |
  | fail: `index.pck` answers 500 to its first 4 requests (direct) | prefetch gave up after 4; label empty; idle 5 s; the press fetched it again (5 requests), the rest once; up 0.88 s | same, 0.66 s |
  | `webgpu-full-size.html` shell (direct) | Ready, idle, each once; up 3.99 s* | same, 4.18 s* |
  | start button off (`auto`) | starts on its own; each once | same |

  \* The WebGPU shell's own `initWebGPU()` requests `maxSampledTexturesPerShaderStage` 16, so the engine falls back to
  Forward Mobile and compiles. That predates this task and was not changed.
  - **The texture pack is chosen right:** both adapters have BC and no ETC2, and both browsers fetched only
    `textures_bc.pck`, once, matching the device's choice (no second texture-pack request after the press).
  - "Time to first frame like a cached export": the prefetched press is 0.89 s against 0.90 s cached (Chrome), and
    0.73 s against 0.70 s (Firefox).
- **First round, fixed or not reproduced** (logs in `runs/round1_*`):
  - Fixed: in Firefox the label stayed at "Downloading 99%" although every download had finished. A rAF progress report
    landed after "Ready" and overwrote it. The fix: the engine sends the final report, then detaches the callback, and
    the shells ignore late reports.
  - Not reproduced in the second round (with or without `Cache-Control`): in one Chrome `ready` run the server saw
    `index.wasm`, `shaders.pck` and `shaders_extra.bin` twice, all within 0.6 s, before the press. That is too soon for
    the Preloader's retry (1 s delay), and the round-2 server log shows no aborted requests on those files. Most likely
    a Chrome network-level restart, but unexplained.
  - Not reproduced: in one Chrome `fail` run the game started without the test pressing anything. That run had no
    click probe. The rerun, with probes, showed the only trusted click at the test's press. My guess is a stray real
    click on the headed window, but that is unproven.
- `webgpu_notes/work/smoke.sh`: `SMOKE OK (smoke-20261028-133410-1505526)`: interface and text-scan 584/584 identical,
  292 modules, 0 invalid in Chrome. No shader code touched.

## Process note

One Firefox debug run (about 2 min, around 13:46) went out **without the gpu lease**. My `lease.sh take` returned
"held by worker-46" and my command chain carried on. It may have overlapped a worker-46 GPU run. Every other browser
run was under the lease.

## For the lead

- By hand: tick Start Button > Enabled in a windowed editor and watch Preload / Preload Progress appear and hide.
- Safari not checked. Real-network checks (a CDN, HTTP/2) not done; everything was on localhost.
- Editor and templates must come from the same tree. An old template's shell ignores `preload` (the game then downloads
  on the press, as before).
- Wiki text in for-docs.md. TASKS entry, a trap candidate (headless exports have no shader packs) and the note for the
  camera-sim session are in for-tasks.md.
