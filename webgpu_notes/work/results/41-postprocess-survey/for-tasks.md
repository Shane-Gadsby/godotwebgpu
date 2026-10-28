# For TASKS.md (lead)

§2.48 → done as a survey: move to §4 with a pointer to `webgpu_notes/work/results/41-postprocess-survey/SURVEY.md`.
Each "Build it" answer below becomes its own §2 item / work task with the effort, renderer list and plumbing from
SURVEY.md. Suggested §4 text:

> **Post-processing survey (§2.48, work task 41).** Gaps against Unreal, Unity URP/HDRP and Blender, each checked in
> this tree: color grading controls (none beyond BCS + LUT), local exposure, screen-space light shafts, a custom tonemap
> curve, Panini projection, GTAO, web HDR output, color vision deficiency, micro shadows. Already present and so not
> gaps: contact shadows (Forward+, upstream), sharpening (§4.50), lens dirt (glow map), desktop HDR output, material
> refraction. Two budget facts for any new tonemap input: Forward+ tonemap uses all four sets (add to set 3, binding 2
> is free; WebGPU group 3 also holds binding 120) and its push constant is full; Environment-owned parameters have no
> UBO yet (CameraEffects is CameraAttributes-owned). Choices on the hand-offs page (`pp-*`).

## Questions for the user

Each is a choice with three options: **Build it now** / **Later (keep on the list)** / **Skip**. Defaults marked.

1. **pp-grading** — Color grading controls on Environment: lift/gamma/gain, shadows-midtones-highlights, channel mixer,
   split toning, color filter/hue shift/vibrance, curves (master, RGB, hue-vs-hue, hue-vs-sat, sat-vs-sat, lum-vs-sat),
   as in Unity URP/HDRP, Unreal and Blender. Baked into one internal 3D LUT, so all three renderers and web, no extra
   pass or binding. ~2 sessions. **Default: Build it now.**
   - Sub-choice **pp-grading-domain**: display-referred after the tonemapper (where Godot's adjustments are; same look in
     every renderer) / also an HDR log-domain mode before the tonemapper (Forward+ only, a later task). **Default:
     display-referred only, HDR mode later.**
2. **pp-local-exposure** — Local exposure (Unreal's Bilateral method; exposure fusion later): keeps a window and the
   room behind it both readable. Forward+ desktop and web only (Mobile's range is too small, Compatibility has no
   pre-tonemap post). On CameraAttributes beside auto exposure. ~2-3 sessions. **Default: Build it now** (after
   grading).
   - Sub-choice **pp-local-exposure-home**: CameraAttributes (exposure group, uses the CameraEffects buffer) /
     Environment (tonemap group). **Default: CameraAttributes.**
3. **pp-light-shafts** — Screen-space light shafts (god rays) from a DirectionalLight3D that opts in; Environment
   intensity/threshold/decay/tint. Works on Mobile, Compatibility and phones, where volumetric fog is missing or too
   expensive. ~1.5-2 sessions. **Default: Build it now.**
4. **pp-panini** — Panini projection for wide-FOV cameras (Unity, Unreal), in the existing lens resample: all
   renderers, no extra pass. On CameraAttributes; Camera3D's project/unproject (picking, HUD markers) made aware of
   it. ~1-1.5 sessions. **Default: Build it now.**
5. **pp-custom-tonemap** — A Custom tonemapper with toe/shoulder/slope/clip controls (HDRP Custom, Unreal Film). All
   renderers; needs the first Environment-owned tonemap buffer (the push constant is full). ~1-1.5 sessions.
   **Default: Later** (AgX + grading covers most needs).
6. **pp-gtao** — GTAO (XeGTAO-style) as an alternative to Godot's ASSAO, project setting `ssao/method`. Forward+ and
   web. Overlaps §2.34's RT AO, which writes the same AO texture. ~2-3 sessions. **Default: Later** (decide with RT AO).
7. **pp-web-hdr** — HDR output on web (WebGPU extended canvas tone mapping, Chrome first), behind the existing
   `Window.hdr_output_requested`. Needs an HDR display to verify. ~1-1.5 sessions. **Default: Later.**
8. **pp-color-vision** — Color vision deficiency simulation and correction (accessibility; Unreal has it): project
   settings + runtime setter, applied to the whole window including UI at the final blit. Nearly free at run time.
   ~0.5-1 session. **Default: Build it now.**
9. **pp-micro-shadows** — HDRP-style micro shadows (sun shadows from normal/AO maps), a few instructions in the scene
   shaders, all renderers. ~0.5-1 session. **Default: Later.**
10. **pp-not-recommended** — Contact shadows on Mobile/Compatibility, a distortion buffer, built-in outlines/Kuwahara/
    pixelate, underwater: leave to CompositorEffect/materials (a sample outline effect in the docs at most).
    Options: agree / pick one to build (say which in the note). **Default: agree.**
