#!/usr/bin/env bash
# Takes the gpu lease (waits), runs every browser check, drops the lease. Results in $W/runs/<browser>/<case>.log
set -uo pipefail
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w51
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu-51/webgpu_notes/work/lease.sh
until $L take gpu worker-51 1 >/dev/null 2>&1; do sleep 30; done
echo "gpu lease taken $(date +%T)"
for b in ${BROWSERS:-chrome firefox}; do
  R=$W/runs/$b; mkdir -p $R
  run() { local name=$1; shift; timeout 300 node $W/start_test.mjs $b "$@" > $R/$name.log 2>&1; echo "$b $name: $(tail -1 $R/$name.log)"; }
  run base_default $W/exp/base_default $R/base_default auto --direct
  run new_default $W/exp/new_default $R/new_default auto --direct
  run start_default $W/exp/start_default $R/start_default start
  run start_custom $W/exp/start_custom $R/start_custom start --key Enter
  run start_states $W/exp/start_states $R/start_states start --direct --key Space
  run wg_default $W/exp/wg_default $R/wg_default auto --direct
  run wg_start $W/exp/wg_start $R/wg_start start
  run oldshell_start $W/exp/oldshell_start $R/oldshell_start auto --direct
done
$L drop gpu worker-51
echo "done $(date +%T)"
