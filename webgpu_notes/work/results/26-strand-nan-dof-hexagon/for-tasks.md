# For TASKS.md (task 26)

## §2.33 (MetaHuman), add under Strands

- Strand `ALPHA` clamps `v_coverage` before `pow()` (2026-10-26, task 26): grooms that taper to a width of exactly 0
  (Ava's eyelashes: 1574 tip vertices) interpolate coverage to about -1e-9 beside the tips, and `pow()` of that is NaN
  on NVIDIA and lavapipe. The opaque and hashed strand variants edit the shader through `HAIR_STRANDS_ALPHA_LINE`.
  Checked by `webgpu_tests/metahuman/nan_check.gd` (in `run.sh`).

## §4 archive entry

- **Strand NaN and the DOF black hexagon (task 26, 2026-10-26).** A black hexagon over Ava's face at 4.80 m / 1.63–1.64 m
  (camera-sim, native and WebGPU-capable). Source proven by a read-only `CompositorEffect` read-back (one pixel, NaN in all
  four channels, so `ALPHA` was NaN) and an additive same-vertex probe (`v_coverage` = -1.07e-9 at that pixel, at a lash
  tip). Fixed at the source (clamp) and in the bokeh DOF (`dof_finite()`: non-finite counts as black; once per pixel in
  the compute `GEN_BLUR_SIZE`, per tap in the raster passes). 42-position sweep native 3 → 0, web 0 → 0. New:
  `webgpu_tests/dof_nan/run.sh`, Forward+ arm `dof_nan` (83 arms), `metahuman/nan_check.gd`.

## §3 trap (new)

- **One NaN pixel becomes a black bokeh shape (NaN x 0 = NaN).** The bokeh DOF weights every tap with
  `mix(center, sample, m)`; with `m == 0` a NaN sample still poisons the sum, so a single non-finite pixel blanks
  everything within the blur radius in the bokeh's shape (hexagon by default; 32 px with physical transitions). When a
  black shape appears, look for one NaN pixel first, and find it with a **read-back**, not a shader probe: a probe
  rewrites the expression that made the NaN, and a blended marker is buried by the layers drawn after it (92 lash layers
  at the pixel here). What worked: a `CompositorEffect` at `POST_TRANSPARENT` that scans the color image for exponent
  bits 0x7f800000 (alpha NaN too ⇒ the material's `ALPHA` was NaN, since blending can only put NaN in alpha from the
  source alpha), then the material swapped for one with the same vertex stage and an **additive** fragment that counts
  suspect values. Test NaN on the bits, never `isnan()`: compilers may fold it (WGSL explicitly allows that). And
  `pow(x, y)` of an interpolated value that is 0 at some vertices needs a clamp: interpolation lands at -1e-9.
  Mobile hides the whole class unless the viewport is HDR (its RGB10A2 buffer stores NaN as 0).

## Questions for the user

- Look (no decision needed): `looks/native_4.80m_1.64m_before_after.png` (the hexagon and the fix, native),
  `looks/native_4.80m_1.64m_dof_fix_only_vs_both.png` (DOF hardening alone vs both fixes: should look the same),
  `looks/web_fp_dof_nan_before_after.png` (the Forward+ fixture on WebGPU: NaN quad in the far blur, before/after).
  Default: none needed — the numbers above already pass.
- Choice: should the other screen-space passes that spread a NaN (glow, SSR, auto-exposure) get the same
  `dof_finite()`-style guard? Default: **no**, only on the next report (each costs a little in every frame).
