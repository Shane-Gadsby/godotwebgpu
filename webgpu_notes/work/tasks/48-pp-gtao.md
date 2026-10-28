# 48-pp-gtao

State: ready-for-integration
Owner: worker-48
Claimed: 2026-10-28
Needs: build, gpu
Branch: work/48
Results: webgpu_notes/work/results/48-pp-gtao/

Read `webgpu_notes/work/README.md` (incl. the browser launch rule), `webgpu_notes/work/camera-batch.md` (conventions: default off and bit-identical, all renderers or documented limits, cited models, resolution-correct sizes, interface references, builds, verification pattern, docs, questions) and the survey `webgpu_notes/work/results/41-postprocess-survey/SURVEY.md` (the entry for this effect: design, evidence, web cost, where it belongs). User picked "Build it now" on the hand-offs page, 2026-10-28.

## Goal

GTAO (SURVEY entry 6, `pp-gtao`): ground-truth-based ambient occlusion (Jimenez et al. 2016; XeGTAO as the reference
implementation, MIT) as an alternative to Godot's SSAO, selected by a project setting (e.g.
`rendering/environment/ssao/method`: SSAO default / GTAO), using the Environment's existing SSAO controls where they
map and writing the same AO texture the scene and SSIL read (the planned RT AO of TASKS §2.34 will write it too: keep the
interface clean for it). Forward+ and web (document Mobile/Compatibility). Default unchanged and bit-identical.
Verification: AO on a test scene vs a ray-traced reference (Python or an offline render of simple geometry), cost vs
SSAO per quality, temporal stability, WebGPU vs native, smoke, the Forward+ matrix's SSAO-related arms still at parity.
