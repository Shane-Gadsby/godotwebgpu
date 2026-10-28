# 49-pp-micro-shadows

State: ready-for-integration
Owner: worker-49
Claimed: 2026-10-28 12:05
Needs: build, gpu
Branch: work/49
Results: webgpu_notes/work/results/49-pp-micro-shadows/

Read `webgpu_notes/work/README.md` (incl. the browser launch rule), `webgpu_notes/work/camera-batch.md` (conventions: default off and bit-identical, all renderers or documented limits, cited models, resolution-correct sizes, interface references, builds, verification pattern, docs, questions) and the survey `webgpu_notes/work/results/41-postprocess-survey/SURVEY.md` (the entry for this effect: design, evidence, web cost, where it belongs). User picked "Build it now" on the hand-offs page, 2026-10-28.

## Goal

Micro shadows (SURVEY entry 9, `pp-micro-shadows`), HDRP style: directional-light shadowing from the material's
normal map and AO (the "micro" self-shadowing a texture's baked detail implies), a few instructions in the scene shaders,
Environment (or project) opacity. All three renderers and web; default off and bit-identical (watch the shader variant
count and the precompile/bake: no new variants unless needed). Verification: shadowing vs the HDRP formula on a test
material at several light angles, cost, WebGPU vs native, smoke, the Forward+ matrix still at parity for lighting arms.
