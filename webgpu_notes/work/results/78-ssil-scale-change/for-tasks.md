# Task 78: for TASKS.md (lead copies in)

## §4 entry

**4.x A 3D scale change keeps SSIL's (and SSR's) last frame** (2026-10-29, work task 78; user answer
`ssil-scale-change`: carry it over). `RenderSceneBuffersRD::configure()` now also carries `rb_sslf/last_frame` (the
previous frame's color that SSIL and SSR sample, with SSIL's 6 mips), and `SSEffects::allocate_last_frame_buffer()`
resamples it bilinearly into the new size (`copy_to_fb_rect()`, as TAA's history in §4.86) and rebuilds the mip chain,
instead of clearing it to black. The texture gets `COLOR_ATTACHMENT` only when it is resampled, so a viewport that never
reconfigures is unchanged; a carried frame that SSIL/SSR no longer want is freed on the next frame. Native bench (task
74's, SSIL on, HDR float, `--fixed-fps 60`): the frame-to-frame difference on the change frame drops from 0.00389 to
0.00372, exactly the bare resolution change (SSIL and SSR off: 0.00372), and the next frame's 0.00038 residual is gone
(0.00000), 1.0 -> 0.6 and 0.6 -> 1.0; SSIL + SSR 0.0040/0.00055 -> 0.00372/0; SSR alone 0.0038/0.00018 -> 0.00372/0.
Under TAA and FSR 2 the gain is in the noise (0.00209 -> 0.00202). No-change renders bit-identical (7 configs, 12/12
frames). Matrix `ssil` (delta 1.744, native diff 0.005) and `scaling_3d` (1.388, 0.002) pass `--parity`. Results: `results/78-ssil-scale-change/`.

## Trap candidates (§3)

- None new. (Reminder of an existing one: `RenderSceneBuffersRD::_carry_texture()` names the texture as strings,
  `"rb_sslf"` / `"last_frame"`, because render_scene_buffers_rd.cpp does not include ss_effects.h; renaming
  `RB_SCOPE_SSLF` / `RB_LAST_FRAME` must update it, or the carry silently stops and the old one-frame reset is back.)

## Questions for the user (with defaults)

- None. Side effect to note on the page if wanted: SSR alone (no SSIL) benefits too, since it reads the same buffer.
