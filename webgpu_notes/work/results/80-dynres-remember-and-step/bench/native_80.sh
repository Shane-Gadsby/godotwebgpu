#!/usr/bin/env bash
# Task 80, native (xvfb, --fixed-fps 60; deterministic checks only, trap 106): remember per window mode, the
# pre-mode-key fallback, and the step size on the way up.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w80
R=$S/out/native80${WHERE:+_$WHERE}; mkdir -p $R
CFG=$HOME/.local/share/godot/app_userdata/w75_dynres/scaling_3d_dynamic.cfg
run() { local name=$1; shift; $S/bench/run_native.sh new $R/$name.txt forward_plus ${WHERE:-xvfb} "$@" || echo "FAIL $name"; echo "--- $name"; grep -E '"kind":"start"' $R/$name.txt | grep -o '"start_scale":[^,]*,"user_scale":[^,]*,"wm":"[a-z_]*"'; grep -E 'remember|wm_change' $R/$name.txt; grep -o '"kind":"tick".*"wm":"[a-z_]*","size":\[[0-9,]*\]' $R/$name.txt | grep -o '"scale":[0-9.]*\|"wm":"[a-z_]*","size":\[[0-9,]*\]' | paste -sd" " | cut -c1-400 | sed 's/^.*\(Dynamic 3D resolution:\)/\1/' ; echo "cfg:"; cat $CFG 2>/dev/null | grep -v '^$'; }
L="mode=ctrl iters=64 target=1 remember=1"
rm -f $CFG
# Save: windowed settles at 0.70, fullscreen at 0.60 (min = max forces the value; target 1 fps keeps it STABLE).
run save_windowed $L min=0.7 max=0.7 dur=8 wm=windowed
run save_fullscreen $L min=0.6 max=0.6 dur=8 wm=fullscreen
# Load: 2 s runs end before the startup period, so nothing is saved over the values.
run load_windowed $L min=0.5 max=1.0 dur=2 wm=windowed
run load_fullscreen $L min=0.5 max=1.0 dur=2 wm=fullscreen
# In-session: windowed, then fullscreen at 1 s: the fullscreen value is loaded when the mode changes.
run switch_to_fullscreen $L min=0.5 max=1.0 dur=3 wm=windowed wm2=fullscreen wm2_at=1.0
# Fallback: a file written by task 75 (no mode in the key) seeds the first mode that runs, once, and is rewritten.
sec=$(grep -o '^\[.*\]' $CFG | head -1)
printf '%s\n\n/root=0.65\n' "$sec" > $CFG
run legacy_windowed $L min=0.5 max=1.0 dur=2 wm=windowed
run legacy_then_fullscreen $L min=0.5 max=1.0 dur=2 wm=fullscreen
[[ -n "$NOSTEP" ]] && exit 0
# Step: from 0.5 up to 1.0 with room (target 1 fps), each step one `step`.
rm -f $CFG
for st in 0.05 0.1 0.25; do
	run step_$st mode=ctrl iters=64 target=1 s0=0.5 min=0.5 max=1.0 dur=14 step=$st
	echo "changes: $(grep -o '"kind":"change","scale":[0-9.]*' $R/step_$st.txt | grep -o '[0-9.]*$' | tr '\n' ' ')"
done
