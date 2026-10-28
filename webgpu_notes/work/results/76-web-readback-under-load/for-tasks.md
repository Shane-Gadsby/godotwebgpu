# For TASKS.md (task 76)

## §4 entry

### 4.86 Web readback under load: no flush inside an asynchronous download (October 2026) *(work task 76; found by task 74)*

On WebGPU a readback larger than the download staging ring could hold lost most of its data and broke the frame. The
ring was capped at 16 MB (the driver's upload cap, `API_TRAIT_STAGING_BUFFER_MAX_SIZE_MB`), and a 1920x1080 RGBA16F
read needs 73 x 256 KB blocks. Past the cap, `texture_get_data_async()` / `buffer_get_data_async()` flushed and stalled
mid-request (upstream code). The ~88 % already copied was never read, so the image came back ~11x darker (task 74's
HDR capture). The flushed blocks, which no batch held, were then reused while `mapAsync` was pending: "[Buffer] used in
submit while pending map" on every read, a failed map, then a page error that ended the main loop (callbacks stopped).
Fix: an asynchronous download never flushes; on `FLUSH_AND_STALL_ALL` it takes a block past the limit
(`RenderingDevice::_download_staging_overflow_block()`). The download ring uses `staging_buffer/max_size_mb` (128 MB);
the driver cap applies to uploads only, which took 600 frames with a 16.6 MB read each from 17.8 s to 4.6 s in Chrome.
`STALL_PREVIOUS` takes a new block when the stalls moved `current` onto one this frame wrote. Task 62's dropped
`buffer_is_map_pending()` guard would not have fixed it: it never sees the clear-and-claim path, and it leaves the lost
data. `webgpu_tests/async_readback/` gained `load_*` checks: a 1920x1080 HDR viewport read every frame for 600 frames,
bursts of 4, its luminance against native (0.27155) and a float probe including values above 1. `run_web.mjs` also fails
on WebGPU validation messages and on unexpected console errors. Worker: 60/60 natively and in Chrome and Firefox (debug
and release), 0 validation messages; lum 0.271551 on the web against 0.271550 natively, where the base tree gave
0.025309, 10 bad submits and a timeout. Task 62's histogram suite: 0 console errors (release).

## §3 trap candidates

- **An asynchronous readback must never flush for staging room** (2026-10-29, work task 76). Upstream's
  `texture_get_data_async()` flushed and stalled when the download ring was full, and dropped the regions copied
  before the flush (the request is pushed only after its last region). Natively that needs a read over 128 MB. On WebGPU
  the ring was 16 MB, so a 1920x1080 RGBA16F read lost ~88 % of itself, which looked like an HDR/sRGB problem (11x
  dark). The flush also left mapping blocks in the ring, so the same read wrote into them: "used in submit while
  pending map". When a web readback looks dark or a validation error repeats once per read, compare the read's size with
  the download ring first.
- **Count validation messages in every web tier** (2026-10-29): Chrome logs "used in submit while pending map" as a
  console warning plus an engine error, and a runner that only matched `/ERROR|error/` on W66 lines passed for weeks
  while a perf mode produced them. A dropped submit costs the whole frame's commands.
- **A JS-side GPU buffer tracer finds misuse without a rebuild** (`results/76-web-readback-under-load/tools/`):
  `gpu_trace_init.js`, injected with `page.addInitScript`, wraps `mapAsync` / `unmap` / `copy*` / `submit` and names each
  submit that uses a buffer whose map is pending or mapped, with its wasm stack.

## Questions for the user

- `web-download-staging-cap`: the web's download staging ring now goes up to the project's
  `rendering/rendering_device/staging_buffer/max_size_mb` (128 MB) instead of the 16 MB upload cap. A project that
  reads a 1920x1080 HDR viewport every frame keeps ~70 MB of blocks and their CPU copies, but runs at 4x the frame rate
  (no create/free per frame). Choices: **keep 128 MB (default)** / a separate web download cap (e.g. 64 MB).
