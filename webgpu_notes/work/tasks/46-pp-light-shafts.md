# 46-pp-light-shafts

State: claimed
Owner: worker-46
Claimed: 2026-10-28 12:13
Needs: build, gpu
Branch: work/46
Results: webgpu_notes/work/results/46-pp-light-shafts/

Read `webgpu_notes/work/README.md` (incl. the browser launch rule), `webgpu_notes/work/camera-batch.md` (conventions: default off and bit-identical, all renderers or documented limits, cited models, resolution-correct sizes, interface references, builds, verification pattern, docs, questions) and the survey `webgpu_notes/work/results/41-postprocess-survey/SURVEY.md` (the entry for this effect: design, evidence, web cost, where it belongs). User picked "Build it now" on the hand-offs page, 2026-10-28.

## Goal

Screen-space light shafts (god rays) (SURVEY entry 3, `pp-light-shafts`): a DirectionalLight3D opts in; Environment
intensity / threshold / decay / tint (and samples per project quality). Occlusion from depth (sky = unoccluded), radial
blur toward the sun's screen position, fade when the sun leaves the frame or is behind the camera. All three renderers
and web; resolution-correct; default off and bit-identical. Verification: shaft direction and falloff vs the sun's
projected position and the decay model, behavior at frame edges, cost per renderer, WebGPU vs native, smoke, frames.
