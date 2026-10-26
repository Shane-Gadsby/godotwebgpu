# For TASKS.md / the board (task 11)

- §2.35: **Firefox desktop (Linux, wgpu/Vulkan, naga) works on SPIRV-Cross WGSL exactly as on Tint's.** Forward+
  `--parity` matrix 69/71 on both (the same two SDFGI arms fail on both, Firefox-only, see below), scene smoketest 23/23
  on both, 0 naga module errors and 0 pipeline-creation errors on either. naga accepts every engine variant (276/276) and
  2564/2566 real-project stages on both sides. The two it rejects are the same for both converters (CanvasSdf `r16snorm`,
  see below). Frame and GPU times: see SUMMARY.md.
- New open item, not the converter's: **SDFGI differs from native by 0.277 in Firefox** (tolerance 0.08) with Tint and
  with SPIRV-Cross alike: `sdfgi` and `sdfgi_camera_move`. Chrome passes the same exports. The difference is subtle (frames in
  `results/11-firefox/sdfgi_frames/`). Some Firefox/wgpu behavior in an SDFGI pass. Worth a §2 entry, Firefox-only.
- Known, not the converter's: CanvasSdf's `r16snorm` storage texture is rejected by naga (capability
  STORAGE_TEXTURE_16BIT_NORM_FORMATS). It is in the corpus because the editor bakes it. At runtime the driver remaps the
  format (§4.14), and no export logged an error from it.
- Known, not the converter's: the baked Forward+ fixture still translates 97 stages at runtime in Firefox as in Chrome
  (task 07's gap), so `run_forward_plus.mjs --baked` reports its "did not use its shader cache" line (translated 97 >
  ceiling 80) for both converters.
- §3 trap candidate (measurement): **Firefox hides `GPUAdapterInfo`** (vendor/architecture empty), so a Firefox number
  cannot prove it ran on the real GPU from inside the page. `frame_time.mjs` logs the browser's row in nvidia-smi's
  process table instead (C+G, ~1.6 GB). Firefox does expose `timestamp-query`, so `gpu_time.mjs --browser firefox` works.
- §3 trap candidate (process): `git -C <repo> worktree add <relative path>` resolves the path against `<repo>`, not the
  shell's directory. It briefly put a worktree inside the lead's checkout (removed at once). Always pass an absolute path.
- Harness (CLAUDE.md "Testing" candidates):
  `BROWSER=firefox node validate_wgsl.mjs <dirs>` (naga validation without rendering, no lease);
  `WEBGPU_AUDIT=<file.jsonl>` on `run_scenes.mjs` / `run_forward_plus.mjs` / `frame_time.mjs` (every
  getCompilationInfo error and pipeline error, labeled);
  `run_forward_plus.mjs --dir <export>`;
  `gpu_time.mjs --browser firefox`;
  `perf_guard/frame_time.mjs` (rAF frame time, any browser, uncapped);
  `scene_smoketest/firefox_prefs.mjs` (one set of Firefox prefs, wgpu pinned to Vulkan via `dom.webgpu.wgpu-backend`).

## Questions for the user

None.
