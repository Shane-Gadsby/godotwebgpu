# 44-camera-followups-2: longitudinal chromatic aberration; Forward+ matrix arms for the camera features; Mobile physical DOF (if approved)

State: ready-for-integration
Owner: worker-44
Claimed: 2026-10-28 11:22
Needs: build, gpu
Branch: work/44
Results: webgpu_notes/work/results/44-camera-followups-2/

Read `webgpu_notes/work/camera-batch.md` and TASKS.md §4.52-4.55 first.

## Goal

1. **Longitudinal chromatic aberration** (user answer `ca-aperture`: lateral CA stays physical; longitudinal CA "as a
   later DOF task"): colored fringes on out-of-focus edges (purple in front of focus, green behind, Bokeh-fringing),
   from the per-wavelength focus shift; shrinks when stopping down. In the DOF pass (Forward+ and Mobile where DOF runs),
   CameraAttributes properties (enable, strength; physical: from aperture), default off, bit-identical when off. Model
   cited; verify fringe color/width vs the model on a defocused edge at two apertures.
2. **Forward+ matrix arms** for the deterministic camera features (no grain, no animated zebras, no motion-dependent
   effects): at least lens optics (vignetting + distortion + CA), bokeh shape/cat's eye, starburst/halation, camera LUT,
   white balance exists already. Follow `webgpu_tests/forward_plus/` conventions (features.mjs, fp_scene.gd); each arm
   must pass `run_forward_plus.mjs --parity --feature <arm>` (native Vulkan reference) unbaked and baked; update the arm
   count where docs state it (lead edits CLAUDE.md/README; put the new count in for-tasks.md).
3. **Mobile physical DOF** (hand-offs question `mobile-dof-physical`, default "Fix it"; the lead tells you the answer,
   or build the default): `bokeh_dof_raster()` never sets `use_physical_far/near` from the camera, so a
   CameraAttributesPhysical camera gets wrong/no DOF on Mobile. Make it match the compute path; measure Mobile vs Forward+
   CoC on a physical camera.
