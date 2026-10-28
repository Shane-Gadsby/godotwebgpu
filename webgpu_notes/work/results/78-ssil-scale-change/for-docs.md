# Task 78: for the docs (lead copies in)

Nothing user-facing changes: no new setting, API or shader. If a doc lists what a 3D scale change keeps (TASKS §4.86
and wherever dynamic resolution is documented, e.g. `webgpu_site/` or the wiki's dynamic resolution page), add:

> The last frame that SSIL and SSR read (`rb_sslf/last_frame`) is also kept: it is resampled bilinearly into the new
> internal size, so neither effect shows a frame without indirect light or reflections after the change.

And take "SSIL's last-frame buffer" off the "still resets" list (left: volumetric fog, FSR 2 on a target size change,
MetalFX temporal, TAA when the view count changes).
