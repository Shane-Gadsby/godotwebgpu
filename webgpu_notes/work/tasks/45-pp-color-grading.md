# 45-pp-color-grading

State: integrated (TASKS §4.65)
Owner: worker-45
Claimed: 2026-10-28
Needs: build, gpu
Branch: work/45
Results: webgpu_notes/work/results/45-pp-color-grading/

Read `webgpu_notes/work/README.md` (incl. the browser launch rule), `webgpu_notes/work/camera-batch.md` (conventions: default off and bit-identical, all renderers or documented limits, cited models, resolution-correct sizes, interface references, builds, verification pattern, docs, questions) and the survey `webgpu_notes/work/results/41-postprocess-survey/SURVEY.md` (the entry for this effect: design, evidence, web cost, where it belongs). User picked "Build it now" on the hand-offs page, 2026-10-28.

## Goal

Color grading controls on **Environment** (SURVEY entry 1, `pp-grading`): lift/gamma/gain, shadows-midtones-highlights
(with ranges), channel mixer, split toning, color filter / hue shift / vibrance, curves (master, R, G, B, hue-vs-hue,
hue-vs-sat, sat-vs-sat, lum-vs-sat; Curve / Gradient resources as fits Godot). Display-referred, after the tonemapper,
where Godot's adjustments are (the HDR log-domain mode is a later task). Baked on the CPU (or a small compute pass) into
**one internal 33³ 3D LUT** that goes through the existing color-correction slot, composed with a user
`adjustment_color_correction` LUT if both are set (order: grading, then the user LUT; document it). All three renderers
and web, no new binding, no shader interface change if at all possible. Rebake only when a grading value changes.
Verification: each control against a Python reference on a color chart (numbers), defaults bit-identical, WebGPU vs
native, smoke; frames for a by-eye look.
