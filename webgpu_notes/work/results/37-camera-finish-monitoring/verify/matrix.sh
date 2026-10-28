#!/usr/bin/env bash
# matrix.sh <renderer> <prefix>: every native check of task 37 for one rendering method; results in out/<prefix>_*.json
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w37
cd $W
E=/mnt/109313D2109313D2/godot-editors/godotwebgpu-37/bin/godot.linuxbsd.editor.x86_64
rm=$1 p=$2 o=$W/out
R() { ./render.sh "$@" >/dev/null || echo "RENDER FAILED: $*"; }
A() { echo "== $1"; shift; python3 analyze.py "$@"; }
R $E $rm 1280x720 $o/${p}_ramp_base.png scene=ramp
R $W/godot_base $rm 1280x720 $o/${p}_ramp_old.png scene=ramp
R $E $rm 1280x720 $o/${p}_ramp_zebra.png scene=ramp zebra=95 vio=1
R $E $rm 1280x720 $o/${p}_ramp_zebra2.png scene=ramp zebra2=70 vio=1
R $E $rm 1280x720 $o/${p}_ramp_fc.png scene=ramp false_color=1 vio=1
R $E $rm 1280x720 $o/${p}_ramp_hist.png scene=ramp histogram=0.5 vio=1
R $E $rm 1280x720 $o/${p}_ramp_histrgb.png scene=ramp histogram=0.5 hist_rgb=1 hist_pos=0 vio=1
R $E $rm 1280x720 $o/${p}_ramp_novio.png scene=ramp zebra=95 zebra2=70 false_color=1 histogram=0.5 peaking=0.5
R $E $rm 1280x720 $o/${p}_ramp_hint.png --editor -- scene=ramp false_color=1 sub=1280x720
R $E $rm 1280x720 $o/${p}_ramp_hintbase.png --editor -- scene=ramp sub=1280x720
R $E $rm 1280x720 $o/${p}_step_base.png scene=step
R $W/godot_base $rm 1280x720 $o/${p}_step_old.png scene=step
A "default vs pre-change editor (ramp)" diff $o/${p}_ramp_base.png $o/${p}_ramp_old.png
A "default vs pre-change editor (step)" diff $o/${p}_step_base.png $o/${p}_step_old.png
A "zebra 95" zebra $o/${p}_ramp_base.png $o/${p}_ramp_zebra.png level=95
A "zebra2 70 window" zebra $o/${p}_ramp_base.png $o/${p}_ramp_zebra2.png level=70 window=1
A "false color" false_color $o/${p}_ramp_base.png $o/${p}_ramp_fc.png
A "histogram luma" histogram $o/${p}_ramp_base.png $o/${p}_ramp_hist.png size=0.5 pos=3
A "histogram rgb" histogram $o/${p}_ramp_base.png $o/${p}_ramp_histrgb.png size=0.5 pos=0 rgb=1
A "game run, overlays on, Visible in Output off (expect 0 diff)" diff $o/${p}_ramp_base.png $o/${p}_ramp_novio.png
grep -h EDITOR_HINT $o/${p}_ramp_hint.png.log
A "editor-hint run, Visible in Output off (expect overlays drawn: false color matches the ARRI table)" false_color $o/${p}_ramp_hintbase.png $o/${p}_ramp_hint.png
# Noise reduction on digital noise at ISO 6400, 1080p.
for g in 4; do
	R $E $rm 1920x1080 $o/${p}_nr_g${g}_s0.png scene=step grain_iso=6400 grain_int=$g
	for s in 0.5 1 2 4; do R $E $rm 1920x1080 $o/${p}_nr_g${g}_s$s.png scene=step grain_iso=6400 grain_int=$g nr=$s; done
	R $E $rm 1920x1080 $o/${p}_nr_g${g}_chroma.png scene=step grain_iso=6400 grain_int=$g nr=1 nr_chroma=1
	A "NR vs strength (grain_intensity $g)" nr $o/${p}_nr_g${g}_s0.png $o/${p}_nr_g${g}_s0.5.png $o/${p}_nr_g${g}_s1.png $o/${p}_nr_g${g}_s2.png $o/${p}_nr_g${g}_s4.png $o/${p}_nr_g${g}_chroma.png
done
# Sharpening: overshoot vs amount (halo 1 = plain unsharp mask, halo 0 = clamped), and radius vs resolution.
R $E $rm 1920x1080 $o/${p}_sh_base1080.png scene=step
R $E $rm 3840x2160 $o/${p}_sh_base2160.png scene=step
for a in 0.5 1 2; do
	R $E $rm 1920x1080 $o/${p}_sh_a${a}_h1.png scene=step sharpen=$a radius=2 halo=1
	A "sharpen amount $a radius 2 halo 1 (1080p, sigma 2 px)" sharpen $o/${p}_sh_base1080.png $o/${p}_sh_a${a}_h1.png amount=$a sigma=2 halo=1
done
R $E $rm 1920x1080 $o/${p}_sh_a1_h0.png scene=step sharpen=1 radius=2 halo=0
A "sharpen amount 1 halo 0" sharpen $o/${p}_sh_base1080.png $o/${p}_sh_a1_h0.png amount=1 sigma=2 halo=0
R $E $rm 1920x1080 $o/${p}_sh_a1_h025.png scene=step sharpen=1 radius=2 halo=0.25
A "sharpen amount 1 halo 0.25" sharpen $o/${p}_sh_base1080.png $o/${p}_sh_a1_h025.png amount=1 sigma=2 halo=0.25
R $E $rm 3840x2160 $o/${p}_sh_a1_h1_2160.png scene=step sharpen=1 radius=2 halo=1
A "sharpen amount 1 radius 2 at 2160p (sigma 4 px: halo width should double)" sharpen $o/${p}_sh_base2160.png $o/${p}_sh_a1_h1_2160.png amount=1 sigma=4 halo=1
# Focus peaking: checker in focus (left, 3 m) and behind the far DOF (right, 12 m).
R $E $rm 1280x720 $o/${p}_checker_base.png scene=checker
R $E $rm 1280x720 $o/${p}_checker_peak.png scene=checker peaking=0.5 vio=1
A "focus peaking vs CPU Sobel" peaking $o/${p}_checker_base.png $o/${p}_checker_peak.png sens=0.5
R $E $rm 1280x720 $o/${p}_all.png scene=checker peaking=0.5 zebra=80 histogram=0.3 hist_rgb=1 vio=1
