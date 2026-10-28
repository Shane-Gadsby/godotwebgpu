# 80-dynres-remember-and-step

State: ready-for-integration
Owner: worker-80
Claimed: 2026-10-29 06:58
Needs: build, gpu, browser
Branch: work/80
Results: webgpu_notes/work/results/80-dynres-remember-and-step/

User answers on task 75 (2026-10-29): `dynres-remember-key` **"Also per window mode"** and `dynres-step-size`
**"Expose it"**. Read TASKS §4.91 and `results/75-dynamic-resolution-controller/` first.

1. `scaling_3d_dynamic_remember` keys the saved scale by renderer + GPU + viewport path **and window mode** (windowed /
   fullscreen / exclusive fullscreen; for a SubViewport, its window's mode), so windowed and fullscreen keep separate
   values. Old saved files keep working (a key without the mode is read as a fallback once, then rewritten).
2. Expose the step: `scaling_3d_dynamic_step` on `Viewport` / `RenderingServer` and `rendering/scaling_3d/dynamic_step`
   (default 0.05, range ~0.01-0.25), used by both the GPU-time and frame-time paths.

Verification: remember round-trips per mode (windowed and fullscreen keep separate scales; natively and across a web
reload), the step changes the change size (bench, natively and in Chrome), defaults behave exactly as task 75's
(settle, 0 changes/min, CPU_LIMITED), off bit-identical.

## Rules (lead)

- Read `webgpu_notes/work/README.md` (browser launch rule, release-template rule) and CLAUDE.md first.
- Test scope (user): only this task's checks, the default/bit-identical check, the dev_mode editor build and the web
  `template_release` compile; smoke.sh only if a shader interface changes. Say what you skipped.
- Commits plain, no Co-Authored-By / Claude-Session trailers.
