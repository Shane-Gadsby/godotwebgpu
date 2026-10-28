#!/usr/bin/env bash
# gpu_all.sh: under one gpu lease: pre-change and changed renders, Mobile re-calibration, the WebGPU capture.
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w55
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu-55/webgpu_notes/work/lease.sh
PY=$W/../venv/bin/python
until $L take gpu worker-55 2 >/dev/null; do sleep 20; done
echo "gpu lease $(date)"; nvidia-smi --query-gpu=memory.used --format=csv,noheader
rc=0
$PY $W/tools/render.py $W/editor_new1 $W/cal2/jobs.json 3 || rc=1
$PY $W/tools/render.py $W/editor_pre $W/ident_pre.json 3 || rc=1
$PY $W/tools/render.py $W/editor_new1 $W/ident_new.json 3 || rc=1
$PY $W/tools/render.py $W/editor_pre $W/ver_pre/jobs.json 3 || rc=1
$PY $W/tools/render.py $W/editor_new1 $W/ver_new/jobs.json 3 || rc=1
$PY $W/tools/render.py $W/editor_new1 $W/nat8.json 1 || rc=1
echo "== web"
timeout 1200 node $W/tools/shoot55.mjs $W/webexport $W/web $W/web_cases.json 2>&1 | tail -15 || rc=1
$L drop gpu worker-55
echo "GPU_ALL_DONE rc=$rc $(date)"
