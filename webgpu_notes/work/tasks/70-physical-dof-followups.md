# 70-physical-dof-followups

State: ready-for-integration
Owner: worker-70
Claimed: 2026-10-28 22:04
Needs: build, gpu
Branch: work/70
Results: webgpu_notes/work/results/70-physical-dof-followups/

User answers on task 63's questions (hand-offs page, 2026-10-28): `dof-physical-max-blur` **"6% of image height"** and
`camera-sim-hexagon` **"Keep a legacy-look option"**. Read TASKS §4.78 and `results/63-physical-dof-blur-size/` first.

1. `rendering/camera/depth_of_field/depth_of_field_physical_max_blur_size` default **6%** (was 3%). It only affects
   cameras with `dof_physical_blur_size` on, so nothing else changes; re-measure the WebGPU cost at 1440p for Hexagon
   Low/High and Circle Medium at 6% (task 63 could not get a Circle number at 6% within 300 s: find out why, and if
   Circle at 6% is pathologically slow, say so in the class docs and the setting's description).
2. A **legacy-look option** for physical-blur cameras: the old diluted Box/Hexagon gather (the upstream behavior that
   camera-sim's look was tuned through, §3 trap 97) as an opt-in, e.g. `CameraAttributesPhysical.dof_legacy_gather`
   (bool, default false) or an enum, so a project can keep its tuned look while using the thin-lens size. Name and
   place it as fits the existing group; document what it does and that it is not physically correct.

Verification: opt-in off and defaults bit-identical (Forward+, Mobile, web); a 6% case unclamped where 3% clamped;
legacy option on reproduces task 63's pre-fix hexagon look for the same size (compare with a pre-63 render through the
size-matched settings, or document the comparison you used); WebGPU cost table.

## Rules (lead)

- Read `webgpu_notes/work/README.md` (browser launch rule, release-template rule) and CLAUDE.md first.
- **Test scope (user, 2026-10-28):** only the tests directly related to this task, the default/bit-identical check, the
  dev_mode editor build and the web `template_release` compile. No full Forward+ matrix, scene smoketest or perf_guard;
  smoke.sh only if a shader interface changes. Say in SUMMARY.md what you skipped.
- camera-sim (`/mnt/109313D2109313D2/godot-game-dev/camera-sim`) is read-only; scratch copies only.
