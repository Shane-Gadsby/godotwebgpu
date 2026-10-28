#!/usr/bin/env bash
# Task 80, Chrome (needs the gpu lease): defaults as task 75, the step's change size, remember per mode across reloads.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w80
R=$S/out/web80; mkdir -p $R; cd $S
run() { local name=$1; shift; [[ -n "$F" && "$name" != *$F* ]] && return; echo "== $name"; node bench/run_web.mjs --size 1920x1080 --timeout 400 --out $R/$name.json "$@" > $R/$name.log 2>&1; echo "$name rc=$?"; }
F=${1:-}
GPU="mode=ctrl&iters=4500&dur=90&load2_at=45&iters2=1500"
CPU="mode=ctrl&iters=64&cpu_ms=24&dur=60"
run chrome_gpu_default --dir exp_release --query "$GPU"
run chrome_cpu_default --dir exp_release --query "$CPU"
run chrome_gpu_step010 --dir exp_release --query "$GPU&step=0.1"
run chrome_gpu_step020 --dir exp_release --query "$GPU&step=0.2"
L="mode=ctrl&iters=64&target=1&remember=1"
run chrome_remember_modes --dir exp_release --queries "$L&min=0.7&max=0.7&dur=10&wm=windowed|$L&min=0.6&max=0.6&dur=12&wm=fullscreen|$L&min=0.5&max=1.0&dur=2&wm=windowed|$L&min=0.5&max=1.0&dur=4&wm=fullscreen"
echo WEB80 DONE
