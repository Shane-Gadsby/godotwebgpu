# 72-monitoring-clean-frame

State: ready-for-integration
Owner: worker-72
Claimed: 2026-10-28 22:10
Needs: build, gpu, browser
Branch: work/72
Results: webgpu_notes/work/results/72-monitoring-clean-frame/

User answer on task 62's question `histogram-clean-frame` (hand-offs page, 2026-10-28): **"Separate task"**. Read TASKS
§4.50 (monitoring overlays), task 62's results (`results/62-camera-histogram-readback/`, its for-tasks.md proposal) and
task 66's async readback (§4.82) first.

camera-sim renders a whole second view of the scene for its "Download Image" button, because zebras, false colour and
focus peaking are drawn into the camera's output. Give a way to get the frame **without the monitoring overlays**:
e.g. `Camera3D` / `Viewport` keeping the pre-overlay image as a readable texture **only while requested**
(`get_monitoring_clean_texture()` or a request/flag; no memory or cost otherwise), readable with
`get_image_async()` / the awaitable from task 71. Pick the API that fits; document it.

Verification: with overlays on, the clean texture equals the same frame rendered with overlays off (pixel-identical
natively, Forward+ and Mobile; and on WebGPU); no extra allocation or pass when not requested (defaults bit-identical,
and a cost check); works for two viewports; Compatibility documented (no compute overlays there, so the frame itself).

## Rules (lead)

- Read `webgpu_notes/work/README.md` (browser launch rule, release-template rule) and CLAUDE.md first.
- **Test scope (user, 2026-10-28):** only the tests directly related to this task, the default/bit-identical check, the
  dev_mode editor build and the web `template_release` compile. No full Forward+ matrix, scene smoketest or perf_guard;
  smoke.sh only if a shader interface changes. Say in SUMMARY.md what you skipped.
- camera-sim (`/mnt/109313D2109313D2/godot-game-dev/camera-sim`) is read-only; scratch copies only.
