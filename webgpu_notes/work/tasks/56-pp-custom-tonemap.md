# 56-pp-custom-tonemap

State: open
Owner: worker-56
Claimed:
Needs: build, gpu
Branch: work/56
Results: webgpu_notes/work/results/56-pp-custom-tonemap/

Read `webgpu_notes/work/README.md` (incl. the browser launch rule), `webgpu_notes/work/camera-batch.md` (conventions:
default off and bit-identical, all renderers or documented limits, cited models, interface references, builds,
verification pattern, docs, questions) and the survey `webgpu_notes/work/results/41-postprocess-survey/SURVEY.md`
(entry 4 and "Budget facts"). User picked "Build it now" on the hand-offs page (`pp-custom-tonemap`), 2026-10-28.

## Goal

A **Custom** tonemapper on `Environment` (`TONE_MAPPER_CUSTOM` + `tonemap_custom_*`): toe, shoulder, slope, black
clip, white clip (Unreal Film's parameters; HDRP Custom's toe/shoulder strength/length/angle are the other reference).
Hable's piecewise power curve (2017) or a baked log2-domain 1D LUT, your call with the reason; cite the model. All
three renderers and the web; existing tonemappers bit-identical.

- **Budget:** the Forward+ tonemap push constant is full (task 45 took flag bit 24, task 47 nothing there). This is the
  **first Environment-owned tonemap parameter buffer**: add it in Forward+ tonemap **set 3, binding 2** (never a fifth
  group; on WebGPU group 3 also carries the push-constant ring at binding 120), Mobile set 0 at the next free binding,
  Compatibility a uniform in its post pass / scene tonemap. Design it as the place later Environment post parameters
  go (an HDR grading mode, `grading-hdr`, would use it): a small std140 UBO with a header and reserved space, filled
  from `RendererEnvironmentStorage` with its own `/* Custom tonemap */` block.
- Compatibility tonemaps in the scene shader: apply the curve there as its other tonemappers do.
- Defaults: the existing tonemappers and the default (Linear) must render bit-identically; a Custom curve with neutral
  settings should match one documented reference point (say which: e.g. the curve equals Unreal Film's defaults, or
  ACES-like).
- **Running alongside task 57 (local exposure)**, which adds a texture to tonemap set 1 and fields to the CameraEffects
  UBO. Keep your changes in your own blocks; the lead merges both.

## Verification

1. The curve read back from renders (a linear-ramp or grey-card chart, HDR input) against an independent numpy
   implementation of the model, per renderer: within 1/255 in Forward+ (say what Mobile's 2.0 cap and Compatibility's
   in-shader tonemap do).
2. Each parameter moves the curve the way its definition says (toe, shoulder, slope, clips), with plots.
3. Existing tonemappers bit-identical against the pre-change editor (3 renderers).
4. WebGPU (Chrome, launch rule) against native Forward+; smoke.sh with the references regenerated (one new binding).
5. A Forward+ matrix arm `tonemap_custom`, run with `--parity`.
