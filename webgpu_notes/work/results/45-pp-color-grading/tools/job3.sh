#!/bin/bash
# Final: rebuild (parallel bake) editor + web template, re-export, re-time, re-render, re-shoot web.
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w45
WT=/mnt/109313D2109313D2/godot-editors/godotwebgpu-45
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_notes/work/lease.sh
$W/build.sh editor_dev || exit 1
$W/build.sh web || exit 1
cp $WT/bin/godot.linuxbsd.editor.x86_64 $W/work_editor3
rm -rf $W/webexport; mkdir -p $W/webexport
(cd $W/webproj && $W/work_editor3 --headless --path . --import >/dev/null 2>&1; $W/work_editor3 --headless --path . --export-debug "Web" $W/webexport/index.html >/dev/null 2>&1)
ls $W/webexport/index.wasm || exit 1
until $L take gpu worker-45 1 >/dev/null 2>&1; do sleep 20; done
for m in forward_plus gl_compatibility; do timeout 120 xvfb-run -a -s "-screen 0 1400x1200x24" $W/work_editor3 --verbose --path $W/proj --rendering-method $m --resolution 1080x1080 -s anim.gd > $W/anim3_$m.log 2>&1; done
for m in forward_plus mobile gl_compatibility; do $W/native.sh $W/work_editor3 $m $W/out3/$m off:lattice neutral:lattice all:lattice vibrance_min:lattice all_user3d:lattice; done
mkdir -p $W/out/web_old; mv $W/out/web/* $W/out/web_old/ 2>/dev/null
cp $W/out2/forward_plus/vibrance_min_lattice.png $W/out/forward_plus/
(cd $W && node adapter_probe.mjs && timeout 2400 node shoot.mjs $W/webexport $W/out/web 2>&1 | grep -v "Using Device" | tail -40)
$L drop gpu worker-45
echo JOB3_DONE
