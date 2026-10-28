#!/usr/bin/env bash
# Exports every scenario of preload_text_test.mjs. $1 = new template zip, $2 = base-shell template zip.
set -euo pipefail
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w61
E=/mnt/109313D2109313D2/godot-editors/godotwebgpu-61/bin/godot.linuxbsd.editor.x86_64
NEW=$1 BASE=$2
on='html/start_button/enabled=true'
p=html/start_button/preload_text
x() { echo "== $1"; local n=$1 z=$2; shift 2; $W/export.sh $E $z $W/exp/$n "$on" "$@"; }
x base $BASE
x default $NEW
x edited $NEW "$p/downloading_text=\"Fetching the game\"" "$p/progress_text=\"Got {percent}% ({percent} of 100)\"" "$p/ready_text=\"<b>Go!</b> & play\"" "$p/color=Color(1, 0.5019608, 0, 1)"
x nopercent $NEW "$p/progress_text=\"Please wait\""
x dl_off $NEW "$p/downloading=false"
x prog_off $NEW "$p/progress=false"
x ready_off $NEW "$p/ready=false"
x all_off $NEW "$p/downloading=false" "$p/progress=false" "$p/ready=false"
