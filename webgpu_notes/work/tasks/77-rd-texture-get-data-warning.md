# 77-rd-texture-get-data-warning

State: done (merged `896940581f`, verified on the merged tree, TASKS §4.92)
Owner: worker-77
Claimed: 2026-10-29 07:05
Needs: build, browser
Branch: work/77
Results: webgpu_notes/work/results/77-rd-texture-get-data-warning/

User answer `warn-rd-texture-get-data` (2026-10-29): **"Yes, warn too"**. Task 71 (TASKS §4.84) made debug web builds warn
once per texture when a synchronous `Texture2D.get_image()` / `RenderingServer.texture_2d_get()` returns null because
the pixels are not back yet. Do the same for `RenderingDevice.texture_get_data()` (and `buffer_get_data()` if it has the
same first-read behavior on the web): debug web builds only, once per texture/buffer, `WARN_PRINT` (console.warn),
pointing at `texture_get_data_async()` / `get_image_awaitable()`; never natively or in release; not for invalid
resources. Extend `webgpu_tests/async_readback/` (its runner counts warnings): exactly the expected warnings in a debug
export, none in release, the existing 60 checks still pass, natively, Chrome and Firefox.

## Rules (lead)

- Read `webgpu_notes/work/README.md` (browser launch rule, release-template rule) and CLAUDE.md first.
- Test scope (user): only this task's checks, the default/bit-identical check, the dev_mode editor build and the web
  `template_release` compile; smoke.sh only if a shader interface changes. Say what you skipped.
- Commits plain, no Co-Authored-By / Claude-Session trailers.
