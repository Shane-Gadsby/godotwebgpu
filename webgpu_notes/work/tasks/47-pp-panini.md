# 47-pp-panini

State: open
Owner: worker-47
Claimed:
Needs: build, gpu
Branch: work/47
Results: webgpu_notes/work/results/47-pp-panini/

Read `webgpu_notes/work/README.md` (incl. the browser launch rule), `webgpu_notes/work/camera-batch.md` (conventions: default off and bit-identical, all renderers or documented limits, cited models, resolution-correct sizes, interface references, builds, verification pattern, docs, questions) and the survey `webgpu_notes/work/results/41-postprocess-survey/SURVEY.md` (the entry for this effect: design, evidence, web cost, where it belongs). User picked "Build it now" on the hand-offs page, 2026-10-28.

## Goal

Panini projection (SURVEY entry 4, `pp-panini`) on CameraAttributes: distance d (0 = rectilinear, 1 = cylindrical-ish)
and vertical compression s, applied in task 33's lens resample in the tonemap pass (CameraEffects UBO lens region; the
survey suggests `lens[7].zw`; keep `camera_attributes_uses_effects_buffer()` right) with an auto-crop/scale so no border
shows. **Camera3D's `unproject_position` / `project_ray_origin` / `project_ray_normal` / `project_position` (picking,
HUD markers) must follow it**, so a marker stays on its object. All renderers and web; default off and bit-identical.
Verification: straight vertical lines stay straight, horizontal stretch at the edges vs the Panini formula (Sharpless et
al. 2010), round-trip project/unproject error, WebGPU vs native, smoke.
