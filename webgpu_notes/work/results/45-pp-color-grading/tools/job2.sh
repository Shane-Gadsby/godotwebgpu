#!/bin/bash
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w45
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_notes/work/lease.sh
until $L take gpu worker-45 1 >/dev/null 2>&1; do sleep 20; done
for m in forward_plus mobile gl_compatibility; do $W/native.sh $W/work_editor2 $m $W/out2/$m off:lattice neutral:lattice vibrance_neg:lattice vibrance_min:lattice all:lattice; done
$L drop gpu worker-45
echo JOB2_DONE
