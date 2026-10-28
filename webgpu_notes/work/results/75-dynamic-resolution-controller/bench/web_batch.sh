#!/usr/bin/env bash
# Web controller runs (needs the gpu lease). web_batch.sh [name-filter]
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w75
R=$S/out/web; mkdir -p $R; cd $S
run() { local name=$1; shift; [[ -n "$F" && "$name" != *$F* ]] && return; echo "== $name"; node bench/run_web.mjs --size 1920x1080 --timeout 200 --out $R/$name.json "$@" > $R/$name.log 2>&1; echo "$name rc=$?"; }
F=${1:-}
GPU="mode=ctrl&iters=4500&dur=90&load2_at=45&iters2=1500"
CPU="mode=ctrl&iters=64&cpu_ms=24&dur=60"
run chrome_gpu --dir exp_release --query "$GPU"
run chrome_cpu --dir exp_release --query "$CPU"
run firefox_gpu --dir exp_release --query "$GPU" --browser firefox
run firefox_cpu --dir exp_release --query "$CPU" --browser firefox
run chrome_gpu_withheld --dir exp_release --query "$GPU" --withhold
run chrome_cpu_withheld --dir exp_release --query "$CPU" --withhold
run firefox_gpu_withheld --dir exp_release --query "$GPU" --browser firefox --withhold
run firefox_cpu_withheld --dir exp_release --query "$CPU" --browser firefox --withhold
run chrome_remember --dir exp_release --query "mode=ctrl&iters=4500&dur=30&remember=1" --reload
run chrome_gpu_debug --dir exp_debug --query "mode=ctrl&iters=4500&dur=40"
echo WEB BATCH DONE
