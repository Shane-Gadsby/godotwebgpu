# Task 76: web readback under load (SUMMARY)

Branch `work/76`. Code in `servers/rendering/rendering_device.{h,cpp}` (and a comment in `rendering_device_driver.h`);
no driver or shader change. Tier `webgpu_tests/async_readback/` extended.

## Cause: one bug, both symptoms

WebGPU caps the staging ring at 16 MB (`API_TRAIT_STAGING_BUFFER_MAX_SIZE_MB`, meant for uploads), and the download
ring copied that cap. A 1920x1080 RGBA16F readback is 510 regions of 64x64 x 8 B = 32 KB. Because
`_staging_buffer_allocate()` tests `p_amount < available` strictly, a 256 KB block takes 7 of them, so the read needs
73 blocks against a 64-block ring. Past block 64 the allocator returns `STAGING_REQUIRED_ACTION_FLUSH_AND_STALL_ALL`,
and `texture_get_data_async()` / `buffer_get_data_async()` (the upstream code path) then:

1. **Submit and stall the frame without the request**, whose `TextureGetDataRequest` is only pushed after its last
   region. It resets `frame_local_count` to 0, so the ~448 regions copied before the flush are never read. The callback
   gets them as zeros, and only the last ~62 regions (the bottom rows) hold data. **That is the "11x darker" float
   readback** (task 74: L 0.031 against 0.354; here 0.0253 against 0.2716 = 10.7x). It is the readback, not HDR or
   sRGB handling. Natively the same bug needs a read of more than 128 MB, so it never showed.
2. **Leave the flushed blocks mapping inside the ring.** With no request in the frame, `_stall_for_frame()` forms no
   batch, so the 64 blocks are neither held nor moved: their staging entries stay in `frames[frame]`. Then "clear the
   whole staging buffer" sets their `frame_used` to 0, and the rest of the same read is copied straight back into them
   while `mapAsync` is pending. **That is "[Buffer] used in submit while pending map / while mapped"**: each such submit
   drops the frame's commands, a later batch read a failed map ("An asynchronous GPU readback failed"), and then the page
   died on `Failed to execute 'writeBuffer' on 'GPUQueue': parameter 1 is not of type 'GPUBuffer'`, the main loop's end:
   **"the callbacks stop arriving"**.

Reproduced on the base tree (`webgpu-4.8` at 3f611e4544, debug template) with the new tier and a JS-side GPU buffer
tracer (`tools/gpu_trace_init.js` + `tools/trace_run.mjs` wrap mapAsync/unmap/copy*/submit and flag submits that use a
buffer that is not unmapped): 10 bad submits, 23 console errors, lum 0.025309 against native 0.271550, then a timeout
(`logs/trace_base_dbg.json`, `logs/chrome_base_dbg.log`).

**Task 62's dropped guard** (`buffer_is_map_pending()` in the allocator's reuse branch) is **not** the fix. It only
covers the "old block" branch, but the flush path claims blocks through `_staging_buffer_execute_required_action()`'s
clear-and-claim, which never asks. It would also leave cause 1, the lost data, untouched. The case task 62 itself saw
(a 4 KB histogram every frame) is covered by task 66's batch hold (the lead's merged-tree run: 0 errors). With this
fix no block whose map is pending can be in the ring at all, so the guard would never fire.

## Fix

- **Asynchronous downloads never flush for room.** On `FLUSH_AND_STALL_ALL`, `buffer_get_data_async()` and
  `texture_get_data_async()` take a block past the ring's limit (`_download_staging_overflow_block()`) instead of
  flushing. Stalling is what an async read promises not to do, and the flush split the request. The old
  "flush_frames" code is gone. The limit becomes soft, and a held batch's release already frees blocks over it.
- **The download ring keeps the project's `staging_buffer/max_size_mb`** (128 MB). The driver's 16 MB cap now applies
  to uploads only. It was written for uploads ("waste CPU heap after the loading spike"), while the download ring grows
  only with the readbacks a project asks for. With the cap, every 16.6 MB read created and freed ~73 buffers each frame:
  600 frames with a read each took 17.8 s in Chrome, and 4.6 s without the cap.
- **`STALL_PREVIOUS` hardening:** the stalls can take held download blocks out of the ring and move `current` onto a
  block this frame already wrote, which the caller then writes at offset 0. It now takes a new block in that case. I did
  not see this happen; the code path allows it.

## Verification (the task's list)

Tier `webgpu_tests/async_readback/` gained four `load_*` checks, which run in every pass from frame 200:
- a 1920x1080 HDR 3D SubViewport read every frame for 600 frames: all callbacks, in order, every image whole (no
  alpha-0 texel on a 32x18 grid) and equal to the settled value within 0.2 %;
- 10 bursts of 4 reads in one frame (66 MB of staging);
- its mean luminance against native Vulkan's (0.27155, 3 % tolerance);
- a float probe: unshaded 0.6/0.3/0.15 and 3.0/1.5/0.05 through the Linear tonemapper, within 0.5 %.

`run_web.mjs` now also fails on any WebGPU validation message and on any console error the project does not provoke
on purpose (before, validation warnings went unnoticed). `run_native.sh`'s timeout is 240 s.

| run | result | load: 600 reads | lum (native 0.271550) |
|---|---|---|---|
| native Vulkan (dev_mode editor) | 60/60, 0 warnings | 600 callbacks, max latency 2 frames, 5.3 s | 0.271551 |
| Chrome, debug | 60/60, 0 validation, 0 unexpected errors, 2 expected sync warnings | 600, max 6 frames, 4.8 s | 0.271551 |
| Chrome, release | 60/60, 0 / 0, no warnings | 600, max 6, 4.7 s | 0.271551 |
| Firefox, debug | 60/60, 0 / 0, 2 expected sync warnings | 600, max 5, 5.5 s | 0.271551 |
| Firefox, release | 60/60, 0 / 0, no warnings | 600, max 6, 5.3 s | 0.271551 |
| Chrome, base tree (debug) | timeout, lum 0.025309, 10 bad submits | stalls | 0.025309 |

Chrome adapter nvidia/lovelace, not fallback, 48 sampled textures; Firefox an adapter, every run. Bursts 40/40 and
probe exact (0.5996/0.2998/0.1499, 3.0/1.5/0.05; the first is half-float rounding of 0.6) everywhere. The 56 earlier
checks all pass. Traced fixed run: 46,240 maps, all OK, 0 bad submits, map latency median 4 / max 6 rAFs.

Task 62's `tools/web_suite.mjs` (histogram project in a scratch dir, `suite`, which includes interval 0.0 = a readback
every frame at 165 fps): debug finished, only console message the debug-only sync `get_image()` warning (task 71, by
design); release finished, **0 console errors/warnings**. Counts identical to the CPU reference (all `bins_differing`
0).

Builds: editor `accesskit=yes webgpu=yes gdscript_braces=yes linker=mold`, with and without `dev_mode=yes` (werror):
0 warnings. Web `template_debug` and `template_release` (nothreads, no dlink, raycast): 0 warnings. `pre-commit` on
the changed files: clean. The last edit, a comment in `rendering_device_driver.h`, came after these builds.

**Not run** (test scope): `smoke.sh` (no shader or interface change), the matrix, other tiers, threaded templates.

## What the lead must still run

The tier on the merged tree: `./export.sh debug && node run_web.mjs` (Chrome and `--browser firefox`), the same for
release, and `./run_native.sh`. Each web run takes ~40 s more than before. A different GPU or driver could move the
native luminance (0.27155, 3 % tolerance) enough to fail `load_hdr_lum_vs_native`; the constant is in `main.gd`.

## Risks

- Memory: a project reading big textures every frame on the web now keeps up to 128 MB of download blocks (plus their
  CPU shadows) instead of churning 16 MB. Asynchronous reads can exceed even that for one frame (four 16.6 MB reads in
  one frame did). Blocks over the limit are freed when a held batch completes, but blocks of batches that completed at
  once stay in the ring (it never shrank before either).
- Native: identical unless a frame's async downloads exceed 128 MB, which used to flush and lose data and now takes
  extra blocks. Synchronous `texture_get_data()` / `buffer_get_data()` still flush and stall as before.
- `RenderingDevice` code shared with upstream: the removed flush_frames blocks and the new helper conflict at the next
  upstream sync if upstream touches them. Upstream has the same data loss for reads over 128 MB, unreported.
