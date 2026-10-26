# For TASKS.md

§3 trap (proposed): **A parity capture must land on a fixed jitter phase.** Volumetric fog (Halton, frame % 16),
TAA and FSR2 jitter by frame number, and a settled temporal effect still differs by phase: native Vulkan measures
0.045 to 0.082 against itself one or two phases apart. The Forward+ fixture's capture phase used to depend on whether
the harness's first hash change landed in boot frame 1 or 2. Baked exports boot fast enough to straddle that frame,
so fog "flipped between two exact states" (0.046 / 0.082), and that was reported as a baked-path race (work task 16).
The fixture now aligns every capture to phase 0 mod 16 (`_aligned()` in `fp_scene.gd`). Fog is 0.001 from native,
10/10 SPIRV-Cross-baked and 10/10 Tint-baked. Lesson: when a result takes exactly two values, print the frame number
first.

## Questions for the user

None.
