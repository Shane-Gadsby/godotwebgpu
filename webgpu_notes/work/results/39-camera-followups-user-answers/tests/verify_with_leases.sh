#!/usr/bin/env bash
W=/mnt/109313D2109313D2/godot-editors/godotwebgpu-39
L=$W/webgpu_notes/work/lease.sh
S=$(dirname "$0")
until $L take gpu worker-39 1; do sleep 60; done
trap '$L drop gpu worker-39' EXIT
BASE=$S/base_editor $W/webgpu_notes/work/results/39-camera-followups-user-answers/tests/run.sh $W/bin/godot.linuxbsd.editor.x86_64 $S/v39 > $S/verify.log 2>&1
echo "== task 38 tests (copy with shake_visible_in_editor ticked in its edproj)" >> $S/verify.log
rm -rf $S/t38; cp -r $W/webgpu_notes/work/results/38-camera3d-af-stabilization/tests $S/t38
sed -i 's/^shake_enabled = true$/shake_enabled = true\nshake_visible_in_editor = true/' $S/t38/edproj/main.tscn
BASE=$S/base_editor $S/t38/run.sh $W/bin/godot.linuxbsd.editor.x86_64 $S/v38 >> $S/verify.log 2>&1
echo "== task 38 tests, unmodified" >> $S/verify.log
BASE=$S/base_editor $W/webgpu_notes/work/results/38-camera3d-af-stabilization/tests/run.sh $W/bin/godot.linuxbsd.editor.x86_64 $S/v38u >> $S/verify.log 2>&1
