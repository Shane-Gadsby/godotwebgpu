# 74-scale-change-temporal-state

State: integrated (TASKS §4.86)
Owner: worker-74
Claimed: 2026-10-28 22:24
Needs: build, gpu, browser
Branch: work/74
Results: webgpu_notes/work/results/74-scale-change-temporal-state/

Phase 2 of the dynamic-resolution build (DESIGN.md's outline item 2, §4): **a `scaling_3d_scale` change no longer
flashes or shimmers**, for every change (user answer Q1), not only dynamic ones. Today `RenderSceneBuffersRD::configure()`
frees every internal-size buffer: auto exposure re-adapts from minimum (a +34% flash for ~10 frames), FSR 2's context
is destroyed (a 4-5x shimmer pulse over ~8 frames), TAA reseeds.

- One FSR 2 context per viewport created with `maxRenderSize` at the largest render size the viewport may use (the
  current scale, or the dynamic max once task 75 exists), reused across scale changes with its history.
- TAA history **resampled** into the new size (one blit; user answer Q3).
- The auto-exposure luminance carried over a reconfigure instead of recreated at 0.
- Volumetric fog left as today (Q4). Anything else that resets visibly on a scale change: list it.

Verification: DESIGN.md §2.3's native capture (`proto/main.gd mode=capture`) around a change with FSR 2, TAA and auto
exposure: no luminance jump, no FSR 2 pulse (numbers before/after); the same on the web (Chrome); with no scale change,
renders bit-identical to before in Forward+ and Mobile (Compatibility unaffected, say so); the Forward+ matrix arms
`fsr2`, `taa`, `scaling_3d` and `auto_exposure` (if present) with `--parity`.

## Rules (lead)

- The design is task 68's `results/68-dynamic-resolution/DESIGN.md` (merged into webgpu-4.8 once the user's build ends;
  until then read it at `/mnt/109313D2109313D2/godot-editors/godotwebgpu-68/webgpu_notes/work/results/68-dynamic-resolution/DESIGN.md`), with its `data/` and `proto/` bench. The user answered its nine
  questions on 2026-10-28: Q1 **every change** (manual scale changes stop flashing too), Q2 warn and use Bilinear for FSR 1
  on WebGPU, Q3 **resample** the TAA history, Q4 leave volumetric fog as today, Q5 one viewport at a time largest first,
  **Q6 use the submit-to-done queue depth** to skip the CPU-bound probe when it is clear, Q7 target in **fps**, Q8 max
  scale above 1.0 allowed (default 1.0), **Q9 an opt-in `scaling_3d_dynamic_remember`** saving the settled scale per
  machine in `user://`.
- Read `webgpu_notes/work/README.md` (browser launch rule, release-template rule) and CLAUDE.md first.
- **Test scope (user):** only this task's checks, the default/bit-identical check, the dev_mode editor build and the web
  `template_release` compile; no full Forward+ matrix, scene smoketest or perf_guard unless listed; smoke.sh only if a
  shader interface changes. Say what you skipped.
- camera-sim is read-only; scratch copies only.
