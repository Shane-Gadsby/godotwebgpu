#!/usr/bin/env bash
# Waits for the gpu lease, runs every browser scenario, drops it. Logs in $W/runs/<browser>/<case>.log
set -uo pipefail
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w53
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu-53/webgpu_notes/work/lease.sh
until $L take gpu worker-53 1 >/dev/null 2>&1; do sleep 3; done
echo "gpu lease taken $(date +%T)"
for b in ${BROWSERS:-chrome firefox}; do
  R=$W/${RUNS:-runs2}/$b; mkdir -p $R
  run() { local name=$1; shift; timeout 400 node $W/preload_test.mjs $b "$@" > $R/$name.log 2>&1; echo "$b $name: $(tail -1 $R/$name.log)"; }
  for c in ${CASES:-ready ready_cache early savedata noprogress nopreload fail wg auto}; do
    case $c in
      ready) run ready $W/exp/pre $R ready ;;
      ready_cache) run ready_cache $W/exp/pre $R/cache ready --cache ;;
      early) run early $W/exp/pre $R early --mbps 4.5 ;;
      savedata) run savedata $W/exp/pre $R savedata ;;
      noprogress) run noprogress $W/exp/noprog $R noprogress --direct ;;
      nopreload) run nopreload $W/exp/off $R nopreload ;;
      fail) run fail $W/exp/pre $R fail --direct ;;
      wg) run wg $W/exp/wg $R/wg ready --direct ;;
      auto) run auto $W/exp/default $R auto ;;
    esac
  done
done
$L drop gpu worker-53
echo "done $(date +%T)"
