#!/bin/bash
# Second native batch (after the quality remap): accuracy per quality and per-pass GPU cost (--gpu-profile).
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w48
ED=/mnt/109313D2109313D2/godot-editors/godotwebgpu-48/bin/godot.linuxbsd.editor.x86_64
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_notes/work/lease.sh
until $L take gpu worker-48 1 >/dev/null 2>&1; do sleep 20; done
echo "gpu lease $(date +%T)"
mkdir -p $S/out2
r() { local res=$1 out=$2; shift 2
	xvfb-run -a -s "-screen 0 2600x1500x24" $ED --path $S/proj --rendering-method forward_plus --resolution $res $PROF -s gtao_test.gd -- out=$S/out2/$out "$@" > $S/out2/$out.log 2>&1
	grep -E "SCRIPT ERROR|^ERROR" $S/out2/$out.log | head -2 | sed "s/^/$out: /"; }
PROF=""
for q in 0 1 2 3 4; do r 960x540 gtao_q${q}_full method=gtao quality=$q half=0; r 960x540 gtao_q${q}_half method=gtao quality=$q half=1; done
for q in 0 1 2 3 4; do r 960x540 assao_q${q}_full method=assao quality=$q half=0; r 960x540 assao_q${q}_half method=assao quality=$q half=1; done
r 960x540 gtao_q2_full_neutral method=gtao quality=2 half=0 intensity=1.0 power=1.0
r 960x540 taa_gtao_q0 method=gtao quality=0 half=0 view=final taa=1 frames=64 save=16
PROF="--gpu-profile"
r 1920x1080 time_off method=off view=final frames=600 save=0
for h in 0 1; do for q in 0 1 2 3 4; do
	r 1920x1080 time_gtao_q${q}_h$h method=gtao quality=$q half=$h view=final frames=600 save=0
	r 1920x1080 time_assao_q${q}_h$h method=assao quality=$q half=$h view=final frames=600 save=0
done; done
$L drop gpu worker-48
echo "done $(date +%T)"
