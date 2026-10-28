# 71-async-readback-followups

State: claimed
Owner: worker-71
Claimed: 2026-10-28 22:06
Needs: build, browser
Branch: work/71
Results: webgpu_notes/work/results/71-async-readback-followups/

User answers on task 66's questions (hand-offs page, 2026-10-28): `async-readback-awaitable` **"Add an awaitable"** and
`async-readback-null-warning` **"Debug-only warning"** (`async-readback-null-vs-zeros`: keep `null`). Read TASKS §4.82
and `results/66-web-async-texture-readback/` first.

1. A built-in awaitable form next to the Callable one, so `var image = await texture.<method>()` works natively and on
   the web without a user-made signal. Pick the shape that fits Godot's API (a small RefCounted with a `completed`
   signal returned by e.g. `Texture2D.request_image()`; or a coroutine-friendly method), name it, document it with an
   example, and keep `get_image_async(callback)` as is. Same for `RenderingServer.texture_2d_get_async` if it fits.
2. On the web, debug builds print a warning once per texture when a synchronous `get_image()` (and `texture_2d_get`)
   returns `null` because the pixels are not back yet, pointing at the async API. Severity per CLAUDE.md's web logging
   note (`WARN_PRINT` reaches `console.warn`); never in release templates; not for textures that are genuinely invalid.

Verification: extend `webgpu_tests/async_readback/` with the awaitable (natively, Chrome, Firefox) and a check that the
warning prints once in a debug export and never in a release export.

## Rules (lead)

- Read `webgpu_notes/work/README.md` (browser launch rule, release-template rule) and CLAUDE.md first.
- **Test scope (user, 2026-10-28):** only the tests directly related to this task, the default/bit-identical check, the
  dev_mode editor build and the web `template_release` compile. No full Forward+ matrix, scene smoketest or perf_guard;
  smoke.sh only if a shader interface changes. Say in SUMMARY.md what you skipped.
- camera-sim (`/mnt/109313D2109313D2/godot-game-dev/camera-sim`) is read-only; scratch copies only.
