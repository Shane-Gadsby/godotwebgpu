# 57-pp-local-exposure

State: ready-for-integration
Owner: worker-57
Claimed: 2026-10-28 15:05
Needs: build, gpu
Branch: work/57
Results: webgpu_notes/work/results/57-pp-local-exposure/

Read `webgpu_notes/work/README.md` (incl. the browser launch rule), `webgpu_notes/work/camera-batch.md` (conventions:
default off and bit-identical, cited models, resolution-correct sizes, interface references, builds, verification
pattern, docs, questions) and the survey `webgpu_notes/work/results/41-postprocess-survey/SURVEY.md` (entry 2 and
"Budget facts"). User picked "Build it now" on the hand-offs page (`pp-local-exposure`), 2026-10-28.

## Goal

Local exposure on `CameraAttributes` (beside auto exposure, as Unreal puts it under Lens > Exposure), so a bright
window and the room behind it both stay readable: Unreal's **Bilateral** method (highlight contrast, shadow contrast,
detail strength, blurred-luminance blend, middle gray bias). Exposure Fusion (Mertens 2007, Unreal's second method)
is optional and later; say whether it fits.

- **How (survey):** luminance into a bilateral grid (compute, about (W/64) x (H/64) x 32), blur the grid, a blurred
  luminance mip (the auto exposure or glare chain may provide it), and the tonemap pass reduces base-layer contrast
  before tonemapping. Cite Unreal's documentation and the bilateral grid paper (Chen, Paris and Durand 2007).
- **Renderers:** Forward+ desktop and web. Mobile: not supported (RGB10A2 caps at 2.0), Compatibility: impossible (no
  pre-tonemap post); both documented with `[b]Note:[/b]` in the class docs.
- **Budget:** one new sampler + texture in Forward+ tonemap **set 1** (beside auto exposure), parameters in the
  CameraEffects UBO (`header.w` / `reserved`; a `header` bit for enabled; say exactly which components you take).
  No push-constant change, no fifth group.
- Resolution-correct: grid cell sizes and blurs defined relative to the frame, so the result is the same at every
  resolution.
- Defaults off and bit-identical (3 renderers). Works with auto exposure on and off, and with physical units.
- **Running alongside task 56 (custom tonemap)**, which adds an Environment parameter UBO at tonemap set 3 binding 2.
  Keep your changes in your own blocks; the lead merges both.

## Verification

1. A test scene with a bright window and a dark interior (and a high-contrast chart): the result against an
   independent numpy implementation of the same bilateral-grid model, per resolution (720p, 1080p, 1440p).
2. Each control does what its definition says (plots / crops); no halos at strong edges beyond what the model gives.
3. Defaults bit-identical against the pre-change editor (3 renderers).
4. WebGPU (Chrome, launch rule) against native Forward+; GPU cost on WebGPU at 1080p (`perf_guard/gpu_time.mjs`).
5. smoke.sh with the references regenerated; a Forward+ matrix arm `local_exposure`, run with `--parity`.
