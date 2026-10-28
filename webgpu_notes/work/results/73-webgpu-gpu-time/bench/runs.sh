#!/usr/bin/env bash
# Task 73 verification runs (needs the gpu lease). Results in out/.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w73
cd $S; mkdir -p out
R="node bench/run_web.mjs --size 1920x1080 --timeout 150"
run() { local name=$1; shift; echo "== $name"; $R --out out/$name.json "$@" > out/$name.log 2>&1; echo "$name rc=$?"; }
run chrome_sweep_hook --dir exp_release --query "mode=sweep&iters=3000"
run firefox_sweep_hook --dir exp_release --query "mode=sweep&iters=3000" --browser firefox
run chrome_nohook_m1 --dir exp_release --query "mode=sweep&iters=3000&measure=1" --nohook
run chrome_nohook_m0 --dir exp_release --query "mode=sweep&iters=3000&measure=0" --nohook
run firefox_nohook_m1 --dir exp_release --query "mode=sweep&iters=3000&measure=1" --nohook --browser firefox
run firefox_nohook_m0 --dir exp_release --query "mode=sweep&iters=3000&measure=0" --nohook --browser firefox
run chrome_withhold --dir exp_release --query "mode=sweep&iters=3000" --withhold
run firefox_withhold --dir exp_release --query "mode=sweep&iters=3000" --withhold --browser firefox
run chrome_gpuprof --dir exp_prof --query "mode=sweep&iters=3000" --nohook
run chrome_debug_hook --dir exp_debug --query "mode=sweep&iters=3000"
echo ALLDONE
