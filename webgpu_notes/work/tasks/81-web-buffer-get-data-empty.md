# 81-web-buffer-get-data-empty

State: claimed
Owner: worker-81
Claimed: 2026-10-29
Needs: build, browser
Branch: work/81
Results: webgpu_notes/work/results/81-web-buffer-get-data-empty/

User answer `web-buffer-get-data-empty` (2026-10-28): **"Return empty"**. On the web, a first synchronous
`RenderingDevice.buffer_get_data()` currently returns zeros of the requested size: `buffer_get_data_direct()` returns
false while WebGPU's asynchronous map is pending, and `RenderingDevice::buffer_get_data()`
(`servers/rendering/rendering_device.cpp`) then falls through to the generic staging copy, which runs
`_flush_and_stall_for_all_frames()` and maps a buffer that cannot be mapped synchronously (TASKS §4.92, §3 trap 107).
Make it return an **empty array** instead, like `texture_get_data()` does in the same case, so a script can tell "not
ready" from real zeros, and skip the pointless staging copy and stall.

- Web only (`WEB_ENABLED`, where WebGPU is the only RD driver); native unchanged, bit for bit.
- **Audit every engine caller of `buffer_get_data()`** (grep the tree: renderer storage, effects, GPU particles,
  lightmapper, compositor, modules, `RenderingServer`/`RD` wrappers). Today they get zeros of the right size on a first
  web read; with empty, any caller that indexes or `memcpy`s the result without checking its size would read out of
  bounds. Guard each such caller (treat empty as "no data yet", as the texture path already does) and list them all,
  guarded or already safe, in SUMMARY.md.
- Update the debug warning text from task 77 (`_buffer_get_data_bind()`): "returned an empty array" instead of zeros.
- Class reference (`doc/classes/RenderingDevice.xml`, the web note on `buffer_get_data`): say it returns an empty array
  until the data has arrived.
- Extend `webgpu_tests/async_readback/`: the first sync `buffer_get_data()` on the web returns empty (size 0), a later
  one returns the data; natively it returns the data at once. The existing 68 checks still pass, natively, in Chrome and
  Firefox, with debug and release templates; the debug warning count is unchanged (four), release none.

## Rules (lead)

- Read `webgpu_notes/work/README.md` (browser launch rule, release-template rule, **never `git stash`**) and CLAUDE.md
  first.
- Test scope (user): only this task's checks, the dev_mode editor build and the web `template_debug` and
  `template_release` compiles; smoke.sh only if a shader interface changes. Say what you skipped.
- Commits plain, no Co-Authored-By / Claude-Session trailers.
- Hand back in `Results:` (SUMMARY.md, for-tasks.md, for-docs.md), set `State: ready-for-integration`.
