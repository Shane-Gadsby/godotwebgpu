# 79-dof-circle-reach

State: ready-for-integration
Owner: worker-79
Claimed: 2026-10-29 06:55
Needs: build, gpu, browser
Branch: work/79
Results: webgpu_notes/work/results/79-dof-circle-reach/

User answer `dof-circle-6pct` (2026-10-29): **"Circle reach 3%"**. Task 70 (TASKS §4.90) set the physical DOF max blur
default to 6%; Circle Medium then costs 34.7 ms at 1440p on WebGPU. Cap the **Circle** shape's (and the custom
aperture's, which costs the same way) reach at 3% of the image height while Box and Hexagon keep the setting's value
(6% default). Decide whether it is a fixed 3% cap or half the setting, say which and why (the user's answer: 3%), and
document it in the setting and `CameraAttributesPhysical` docs. Verification: a large Circle blur clamps at 3% (measured
with task 63/70's `edge.py`), Box/Hexagon unchanged at 6%, opt-in off bit-identical, WebGPU Circle Medium cost at 1440p
back near task 70's 3% figure (7.8 ms), legacy gather unchanged.

## Rules (lead)

- Read `webgpu_notes/work/README.md` (browser launch rule, release-template rule) and CLAUDE.md first.
- Test scope (user): only this task's checks, the default/bit-identical check, the dev_mode editor build and the web
  `template_release` compile; smoke.sh only if a shader interface changes. Say what you skipped.
- Commits plain, no Co-Authored-By / Claude-Session trailers.
