#!/bin/bash
# Final verification on the last build: native accuracy, bit-identical default, web captures, matrix arms.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w48
W=/mnt/109313D2109313D2/godot-editors/godotwebgpu-48
ED=$W/bin/godot.linuxbsd.editor.x86_64
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_notes/work/lease.sh
cd $S/proj && $ED --headless --path . --export-debug Web $S/web/index.html > $S/export.log 2>&1; echo "export rc=$?"
until $L take gpu worker-48 1 >/dev/null 2>&1; do sleep 20; done
echo "gpu lease $(date +%T)"
rm -rf $S/out3; mkdir -p $S/out3 $S/webout3
r() { local ed=$1 out=$2; shift 2
	xvfb-run -a -s "-screen 0 2600x1500x24" $ed --path $S/proj --rendering-method forward_plus --resolution 960x540 -s gtao_test.gd -- out=$S/out3/$out "$@" > $S/out3/$out.log 2>&1
	grep -E "SCRIPT ERROR|^ERROR" $S/out3/$out.log | head -2 | sed "s/^/$out: /"; }
for q in 0 1 2 3 4; do r $ED gtao_q${q}_full method=gtao quality=$q half=0; r $ED gtao_q${q}_half method=gtao quality=$q half=1; done
r $ED assao_q2_full method=assao quality=2 half=0
for v in ao final; do r $ED new_default_$v method=default quality=2 half=1 view=$v; r $S/godot_base_editor base_default_$v method=default quality=2 half=1 view=$v; done
r $ED stat_gtao method=gtao quality=2 half=0 view=final frames=20 save=2
r $ED taa_gtao method=gtao quality=2 half=0 view=final taa=1 frames=64 save=16
r $ED taa_assao method=assao quality=2 half=0 view=final taa=1 frames=64 save=16
cd $S && timeout 600 node web_capture.mjs web webout3/web "method=gtao&quality=2&half=0" "method=gtao&quality=2&half=1" "method=gtao&quality=0&half=0" "method=assao&quality=2&half=0" 2>&1 | cut -c1-250
cd $W/webgpu_tests/forward_plus && GODOT_TEMPLATE_ZIP=$W/bin/godot.web.template_debug.wasm32.nothreads.zip ./export.sh > $S/fp_export3.log 2>&1; echo "fp export rc=$?"
WEBGPU_REAL_GPU=1 GODOT_TEMPLATE_ZIP=$W/bin/godot.web.template_debug.wasm32.nothreads.zip node run_forward_plus.mjs --parity --feature ssao,gtao,ssil > $S/fp_run3.log 2>&1; echo "fp rc=$?"
$L drop gpu worker-48
echo "done $(date +%T)"
