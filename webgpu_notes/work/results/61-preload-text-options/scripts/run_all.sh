#!/usr/bin/env bash
# Waits for the gpu lease, runs every scenario of preload_text_test.mjs in Chrome and Firefox, drops it.
set -uo pipefail
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w61
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_notes/work/lease.sh
until $L take gpu worker-61 1 >/dev/null 2>&1; do echo "waiting for gpu lease $(date +%T)"; sleep 10; done
echo "gpu lease taken $(date +%T)"
trap '$L drop gpu worker-61; echo "gpu lease dropped $(date +%T)"' EXIT
R=$W/${RUNS:-runs}
for b in ${BROWSERS:-chrome firefox}; do
  mkdir -p $R/$b
  run() { local name=$1 dir=$2 exp=$3; shift 3; timeout 300 node $W/preload_text_test.mjs $b $W/exp/$dir $R/$b $name $W/expect/$exp.json "$@" > $R/$b/$name.log 2>&1; echo "$b $name: $(tail -1 $R/$b/$name.log)"; }
  for c in ${CASES:-base default nokey edited edited_iframe nopercent dl_off prog_off ready_off all_off wg_base wg_default wg_edited base_nosize default_nosize edited_nosize dl_off_nosize}; do
    case $c in
      edited_iframe) run edited_iframe edited edited --iframe ;;
      base|default|nokey|wg_base|wg_default) run $c $c default ;;
      base_nosize|default_nosize) run $c $c default --strict-hold ;;
      edited_nosize|dl_off_nosize) run $c $c ${c%_nosize} --strict-hold ;;
      wg_edited) run $c $c edited ;;
      *) run $c $c $c ;;
    esac
  done
done
echo "done $(date +%T)"
