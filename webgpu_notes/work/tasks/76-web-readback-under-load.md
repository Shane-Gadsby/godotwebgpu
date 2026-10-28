# 76-web-readback-under-load: async readback every frame on WebGPU; float readback 11x dark

State: integrated (TASKS §4.88)
Owner: worker-76
Claimed: 2026-10-28 23:33
Needs: build, browser
Branch: work/76
Results: webgpu_notes/work/results/76-web-readback-under-load/

Found by work task 74 (2026-10-28, `results/74-scale-change-temporal-state/SUMMARY.md` point 4 and its `bench/`), in
webgpu-4.8 with tasks 62, 66 and 71 merged:

1. **`get_image_async()` every frame on WebGPU:** Chrome logs `[Buffer] used in submit while pending map` on every
   call; with one request per frame the callbacks stop arriving; two in flight fail with "asynchronous GPU readback
   failed" and stall. Task 66's download batches (§4.82) take a pending block out of the reuse ring; task 62 had also
   added a guard in `_staging_buffer_allocate()` (`RDD::buffer_is_map_pending`, branch `work/62`, commit d81dd0011c)
   that the lead **dropped at merge** as redundant (§4.83, §3 trap 102). Find the real cause: whether that guard was
   needed after all (a block can be reused before its batch is formed, or a texture download path that bypasses the
   batches), or something else. Fix it so a readback every frame, and several in flight, work indefinitely with zero
   validation errors, on Chrome and Firefox.
2. **Float readback 11x darker on the web than natively** (task 74's HDR float capture of a viewport). Find out whether
   it is the readback (format conversion of RGBAH / RGBAF, `texture_readback_convert`), the viewport's HDR / sRGB
   handling on the web, or the bench; fix the engine if it is the engine.

Verification: extend `webgpu_tests/async_readback/` with a sustained case (one `get_image_async` per frame for ≥ 600
frames, and 2-4 in flight) and a float-HDR viewport value check against native; 0 console errors in Chrome and Firefox
(debug and release templates); the existing 56 checks still pass; task 62's histogram readback every frame still 0
errors (`results/62-camera-histogram-readback/tools/web_suite.mjs`).

## Rules (lead)

- Read `webgpu_notes/work/README.md` (browser launch rule, release-template rule) and CLAUDE.md first; TASKS §4.82-4.84
  and trap 102.
- Test scope (user): only this task's checks plus the dev_mode editor build and the web template_release compile.
