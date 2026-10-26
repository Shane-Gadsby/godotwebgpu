# MetaHuman importer tests

Checks for `modules/metahuman` (the `.dna` scene importer and `MetaHumanRig`, `webgpu_notes/TASKS.md` §2.33) against a
real MetaHuman export. None ships here: MetaHumans are not ours to redistribute, so pass your own.

```bash
./run.sh ~/Downloads/Ava.zip            # import + verify.gd (headless) + nan_check.gd (lavapipe, Xvfb)
./run.sh ~/Downloads/Ava.zip --shots    # also render work/shots/*.png (lavapipe under Xvfb, no window)
./run.sh ~/Downloads/Ava.zip --export   # also export bench.tscn for the web (needs the WebGPU template)
node ../perf_guard/gpu_time.mjs --dir work/export --uncapped
```

`verify.gd` checks the structure (one `Skeleton3D`, one `Skin` shared by every mesh, LOD visibility ranges), bind
sanity (the eye meshes sit on their eye joints), that the face rig moves the jaw for `CTRL_expressions.jawOpen`, and
that bending the left elbow moves the body rig's corrective joints, and that the scalp hair simulator keeps idle hair
on its groomed shape, moves it on a head turn and brings it to rest.

`nan_check.gd` (always run, offscreen on lavapipe under Xvfb) renders the brows and lashes with every strand's coverage
forced to -1e-9 and counts non-finite pixels in the scene color with a read-only `CompositorEffect` (`nan_probe.gd`):
there must be none. Ava's lashes taper to a width of exactly 0, so their coverage really does interpolate to about
-1e-9 beside the tips; `pow()` of that was NaN, and the bokeh DOF spread it into a black hexagon over the face. A
control copy with the old unclamped `pow()` must produce NaN, or the check prints SKIP instead of passing.

`bench.gd` frames the head and torso (every rig and mesh at LOD 0), animates 40 expressions and both arms every frame,
sways the head so the hair simulation runs, and prints `MHBENCH` lines every 4 s, alternating the rig on and off, with
frame, process and render times.

The editor defaults to `bin/godot.linuxbsd.editor.dev.x86_64` (`GODOT_EDITOR_BIN`), the template to the nothreads
WebGPU release one (`GODOT_TEMPLATE_ZIP`). Build both from the same commit, or no baked shader matches.
