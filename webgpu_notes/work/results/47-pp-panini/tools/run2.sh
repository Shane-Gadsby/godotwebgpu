#!/usr/bin/env bash
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w47
WORK=$W/work/godot.linuxbsd.editor.x86_64
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu-47/webgpu_notes/work/lease.sh
rm -rf $W/native $W/native720 $W/webshots
$W/tools/render_all.sh $WORK $W/native 1600x900 target off d1s1 d1s0 d0.5s0.5
$W/tools/render_all.sh $WORK $W/native720 1280x720 target off d1s1 d0.5s0.5
until $L take gpu worker-47 1 >/dev/null 2>&1; do echo "waiting for gpu lease $(date +%T)"; sleep 30; done
node $W/tools/shoot.mjs $W/web $W/webshots off d1s1 d0.5s0.5
$L drop gpu worker-47 >/dev/null
echo RUN2 DONE
