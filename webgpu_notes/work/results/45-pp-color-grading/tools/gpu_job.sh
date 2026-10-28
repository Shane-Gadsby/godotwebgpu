#!/bin/bash
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w45
$W/native_job.sh
echo "== web"
(cd $W && timeout 2400 node shoot.mjs $W/webexport $W/out/web 2>&1 | tail -45)
/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_notes/work/lease.sh drop gpu worker-45
echo GPU_JOB_DONE
