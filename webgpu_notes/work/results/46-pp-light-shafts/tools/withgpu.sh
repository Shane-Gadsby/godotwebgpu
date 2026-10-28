#!/usr/bin/env bash
# withgpu.sh <cmd...>: waits for the gpu lease, runs the command, drops the lease.
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_notes/work/lease.sh
until $L take gpu worker-46 1 >/dev/null 2>&1; do sleep 3; done
echo "gpu lease taken $(date)"
nvidia-smi --query-gpu=memory.used,memory.total --format=csv,noheader
"$@"; rc=$?
$L drop gpu worker-46
echo "done rc=$rc $(date)"
exit $rc
