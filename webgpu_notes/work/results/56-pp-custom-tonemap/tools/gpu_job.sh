#!/bin/bash
# Task 56 GPU job: waits for the gpu lease, renders natively (new and pre-change editors, 3 renderers), exports and
# shoots the web cases, runs the Forward+ matrix arms with --parity, drops the lease.
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w56
WT=/mnt/109313D2109313D2/godot-editors/godotwebgpu-56
L=$WT/webgpu_notes/work/lease.sh
E=$W/godot_new
P=$W/godot_pre
until $L take gpu worker-56 2 >/dev/null 2>&1; do sleep 30; done
echo "gpu lease taken $(date +%T)"
nvidia-smi --query-gpu=memory.used,memory.total --format=csv,noheader
if [ "${SKIP_NATIVE:-0}" != 1 ]; then
for m in forward_plus mobile gl_compatibility; do
	$W/render.sh $E $m $W/out/new_$m $W/cases_custom.txt
	$W/render.sh $E $m $W/out/new_$m $W/cases_existing.txt
	$W/render.sh $P $m $W/out/pre_$m $W/cases_existing.txt
done
echo "native done $(date +%T)"
fi
if [ "${SKIP_WEB:-0}" != 1 ]; then
until grep -q "web exit" $W/buildall.log; do sleep 20; done; grep "web exit" $W/buildall.log
rm -rf $W/webexport; mkdir -p $W/webexport
timeout 600 $E --headless --path $W/webproj --import > $W/web_import.log 2>&1
timeout 900 xvfb-run -a $E --rendering-driver vulkan --path $W/webproj --export-debug Web $W/webexport/index.html > $W/web_export.log 2>&1; echo "export exit $?"
ls -la $W/webexport | head
cat $W/cases_custom.txt $W/cases_existing.txt > $W/cases_web.txt
(cd $W && timeout 2400 node shoot.mjs $W/webexport $W/out/web $W/cases_web.txt 2>&1 | tail -50)
echo "web done $(date +%T)"
fi
if [ "${SKIP_MATRIX:-0}" != 1 ]; then
cd $WT/webgpu_tests/forward_plus
export GODOT_EDITOR_BIN=$E GODOT_TEMPLATE_ZIP=$WT/bin/godot.web.template_debug.wasm32.nothreads.zip
BAKE=1 timeout 1200 ./export.sh 2>&1 | tail -4
WEBGPU_REAL_GPU=1 timeout 3000 node run_forward_plus.mjs --baked --parity --feature tonemap_custom,tonemap,tonemap_exposure --save-dir $W/out/matrix --json $W/out/matrix.json 2>&1 | tail -40
echo "matrix done $(date +%T)"
fi
$L drop gpu worker-56
echo GPU_JOB_DONE
