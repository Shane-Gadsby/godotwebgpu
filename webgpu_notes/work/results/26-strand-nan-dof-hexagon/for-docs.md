# For the shared docs (task 26)

## CLAUDE.md, "MetaHuman importer" rules that cost time — add

- Strand shaders clamp `v_coverage` before `pow()`: lash tips have width 0 and interpolate to -1e-9 (NaN on NVIDIA).
  The importer's strand variants replace `HAIR_STRANDS_ALPHA_LINE`; change the line and the constant together.

## webgpu_tests/README.md — add

- `webgpu_tests/dof_nan/run.sh [editor]` — one NaN pixel through every bokeh DOF mode natively, Forward+ (compute) and
  Mobile (raster); needs a display. Exit 0 when every mode keeps it to the quad's own pixels.
- Forward+ matrix: 83 arms (new `dof_nan`, a `maxDelta` arm).
- `webgpu_tests/metahuman/run.sh` now also runs `nan_check.gd` (lavapipe under Xvfb).

## webgpu_site/CORRECTNESS_AND_COMPATIBILITY.md — optional note

- The bokeh DOF treats non-finite scene colors as black (`dof_finite()` in `bokeh_dof_inc.glsl`), so a stray NaN from a
  user shader shows as one black pixel instead of a black bokeh shape. A deliberate divergence from upstream Godot.
