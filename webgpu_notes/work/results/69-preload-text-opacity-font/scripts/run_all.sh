#!/usr/bin/env bash
# Waits for the gpu lease, runs every task-69 scenario of preload_text_test.mjs in Chrome and Firefox, drops it.
set -uo pipefail
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w69
S=$(dirname "$(realpath "$0")")
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_notes/work/lease.sh
until $L take gpu worker-69 1 >/dev/null 2>&1; do echo "waiting for gpu lease $(date +%T)"; sleep 10; done
echo "gpu lease taken $(date +%T)"
trap '$L drop gpu worker-69; echo "gpu lease dropped $(date +%T)"' EXIT
R=$W/${RUNS:-runs}
for b in ${BROWSERS:-chrome firefox}; do
  mkdir -p $R/$b
  run() { local name=$1 dir=$2 exp=$3; shift 3; timeout 300 node $S/preload_text_test.mjs $b $W/exp/$dir $R/$b $name $S/expect/$exp.json "$@" > $R/$b/$name.log 2>&1; echo "$b $name: $(tail -1 $R/$b/$name.log)"; }
  for c in ${CASES:-base default nokey base_nosize default_nosize exact_e0 styled styled_iframe wg_base wg_default wg_styled}; do
    case $c in
      base|wg_base) run $c $c base ;;
      base_nosize) run $c $c base --strict-hold ;;
      default|nokey|wg_default) run $c $c default ;;
      default_nosize) run $c $c default --strict-hold ;;
      styled_iframe) run $c styled styled --iframe ;;
      wg_styled) run $c $c styled ;;
      *) run $c $c $c ;;
    esac
  done
done
echo "done $(date +%T)"
