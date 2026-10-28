# 78-ssil-scale-change

State: open
Owner: worker-78
Claimed:
Needs: build, gpu
Branch: work/78
Results: webgpu_notes/work/results/78-ssil-scale-change/

User answer `ssil-scale-change` (2026-10-29): **"Carry it over"**. Task 74 (TASKS §4.86) made a `scaling_3d_scale` change
keep auto exposure, FSR 2 and TAA history; SSIL's last-frame buffer still resets (one frame of different indirect
light). Carry SSIL's history over a scale change the way TAA's is (resample into the new size), every change. Use task
74's bench (`results/74-scale-change-temporal-state/bench/`, repoint its paths) with SSIL on: before/after the change,
the frame-to-frame difference at or near steady; no-change renders bit-identical (Forward+; SSIL is Forward+ only);
the Forward+ matrix arms `ssil` and `scaling_3d` with `--parity`.

## Rules (lead)

- Read `webgpu_notes/work/README.md` (browser launch rule, release-template rule) and CLAUDE.md first.
- Test scope (user): only this task's checks, the default/bit-identical check, the dev_mode editor build and the web
  `template_release` compile; smoke.sh only if a shader interface changes. Say what you skipped.
- Commits plain, no Co-Authored-By / Claude-Session trailers.
