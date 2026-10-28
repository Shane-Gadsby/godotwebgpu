#!/usr/bin/env bash
# native_job.sh <phase...>: task 57 native renders under the gpu lease (waits for it). Phases:
#   ident   defaults with the pre-change and the new editor, 3 renderers (LE never touched), plus LE on in
#           Mobile/Compatibility (must equal off there)
#   model   off/on HDR pairs at 720p, 1080p, 1440p (room and chart layouts)
#   controls  one HDR pair per control at 1080p, AE on, physical units
set -uo pipefail
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w57
R=/mnt/109313D2109313D2/godot-editors/godotwebgpu-57
L=$R/webgpu_notes/work/lease.sh
NEW=$R/bin/godot.linuxbsd.editor.x86_64
PRE=$S/godot_pre
until $L take gpu worker-57 2 >/dev/null 2>&1; do echo "waiting for gpu lease $(date +%T): $($L show | grep gpu)"; sleep 60; done
nvidia-smi --query-gpu=memory.used,memory.total --format=csv,noheader
run() { # editor method WxH name outdir args...
	local ed=$1 m=$2 res=$3 name=$4 out=$5; shift 5
	mkdir -p "$out"
	timeout 180 xvfb-run -a -s "-screen 0 2600x1500x24" "$ed" --path $S/proj --rendering-method $m --resolution $res -s le_test.gd -- "$name" "$out" "$@" > "$out/$name.log" 2>&1
	grep -q "^captured" "$out/$name.log" && echo "ok $m $res $name" || { echo "FAILED $m $res $name"; grep -E "ERROR|error" "$out/$name.log" | head -3; }
}
for phase in "$@"; do
case $phase in
ident)
	for m in forward_plus mobile gl_compatibility; do
		for ed in pre new; do
			E=$PRE; [[ $ed == new ]] && E=$NEW
			O=$S/out/ident/$m/$ed
			run $E $m 1280x720 filmic_glow $O tonemap=filmic glow=1
			run $E $m 1280x720 filmic_ae $O tonemap=filmic ae=1
			run $E $m 1280x720 linear $O tonemap=linear
			run $E $m 1280x720 phys_ae $O tonemap=filmic phys=1 ae=1
		done
		if [[ $m != forward_plus ]]; then
			run $NEW $m 1280x720 filmic_glow_le_on $S/out/ident/$m/new tonemap=filmic glow=1 le=1 hc=0.3
		fi
	done
	;;
model)
	for res in 1280x720 1920x1080 2560x1440; do
		O=$S/out/model/$res
		for lay in room chart; do
			run $NEW forward_plus $res ${lay}_off $O hdr=1 layout=$lay
			run $NEW forward_plus $res ${lay}_on $O hdr=1 layout=$lay le=1
		done
	done
	;;
controls)
	O=$S/out/controls
	run $NEW forward_plus 1920x1080 off $O hdr=1
	run $NEW forward_plus 1920x1080 identity $O hdr=1 le=1 hc=1 sc=1 detail=1
	run $NEW forward_plus 1920x1080 hc0.3 $O hdr=1 le=1 hc=0.3
	run $NEW forward_plus 1920x1080 sc0.4 $O hdr=1 le=1 sc=0.4
	run $NEW forward_plus 1920x1080 detail0.5 $O hdr=1 le=1 detail=0.5
	run $NEW forward_plus 1920x1080 detail2 $O hdr=1 le=1 detail=2
	run $NEW forward_plus 1920x1080 blend0 $O hdr=1 le=1 blend=0
	run $NEW forward_plus 1920x1080 blend1 $O hdr=1 le=1 blend=1
	run $NEW forward_plus 1920x1080 kernel10 $O hdr=1 le=1 kernel=10
	run $NEW forward_plus 1920x1080 kernel100 $O hdr=1 le=1 kernel=100
	run $NEW forward_plus 1920x1080 bias2 $O hdr=1 le=1 bias=2
	run $NEW forward_plus 1920x1080 bias-2 $O hdr=1 le=1 bias=-2
	run $NEW forward_plus 1920x1080 ae_off $O hdr=1 ae=1
	run $NEW forward_plus 1920x1080 ae_on $O hdr=1 ae=1 le=1
	run $NEW forward_plus 1920x1080 phys_off $O hdr=1 ae=1 phys=1
	run $NEW forward_plus 1920x1080 phys_on $O hdr=1 ae=1 phys=1 le=1
	# For looking at: the filmic look, off and on (default settings and strong).
	run $NEW forward_plus 1920x1080 look_off $O tonemap=filmic
	run $NEW forward_plus 1920x1080 look_on $O tonemap=filmic le=1
	run $NEW forward_plus 1920x1080 look_strong $O tonemap=filmic le=1 hc=0.3 sc=0.5
	;;
webnative)
	O=$S/out/web_native
	run $NEW forward_plus 1280x720 look_off $O tonemap=filmic
	run $NEW forward_plus 1280x720 look_on $O tonemap=filmic le=1
	run $NEW forward_plus 1280x720 look_strong $O tonemap=filmic le=1 hc=0.3 sc=0.5
	run $NEW forward_plus 1280x720 lin_off $O tonemap=linear
	run $NEW forward_plus 1280x720 lin_on $O tonemap=linear le=1
	run $NEW forward_plus 1280x720 chart_on $O tonemap=filmic layout=chart le=1
	run $NEW forward_plus 1280x720 detail2 $O tonemap=filmic le=1 detail=2
	run $NEW forward_plus 1280x720 blend0 $O tonemap=filmic le=1 blend=0
	run $NEW forward_plus 1280x720 kernel10 $O tonemap=filmic le=1 kernel=10
	run $NEW forward_plus 1280x720 ae_on $O tonemap=filmic le=1 ae=1
	;;
web)
	node $S/tools/shoot.mjs $S/web $S/out/web look_off="tonemap=filmic" look_on="tonemap=filmic&le=1" look_strong="tonemap=filmic&le=1&hc=0.3&sc=0.5" lin_off="tonemap=linear" lin_on="tonemap=linear&le=1" chart_on="tonemap=filmic&layout=chart&le=1" detail2="tonemap=filmic&le=1&detail=2" blend0="tonemap=filmic&le=1&blend=0" kernel10="tonemap=filmic&le=1&kernel=10" ae_on="tonemap=filmic&le=1&ae=1" 2>&1 | grep -v "Using Device"
	;;
gputime)
	for v in off on; do
		echo "== gpu_time le=$v 1920x1080"
		(cd /mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/perf_guard && timeout 600 node gpu_time.mjs --dir $S/web_$v --size 1920x1080 --passes --top 40 2>&1 | grep -v "Using Device" > $S/out/gputime_$v.txt; tail -3 $S/out/gputime_$v.txt)
	done
	;;
matrix)
	cd $R/webgpu_tests/forward_plus
	BAKE=1 GODOT_TEMPLATE_ZIP=$R/bin/godot.web.template_debug.wasm32.nothreads.zip ./export.sh > $S/out/matrix_export.log 2>&1; tail -2 $S/out/matrix_export.log
	WEBGPU_REAL_GPU=1 timeout 1500 node run_forward_plus.mjs --baked --parity --feature local_exposure,white_balance 2>&1 | grep -v "Using Device" > $S/out/matrix.txt; tail -15 $S/out/matrix.txt
	cd - >/dev/null
	;;
esac
done
$L drop gpu worker-57
