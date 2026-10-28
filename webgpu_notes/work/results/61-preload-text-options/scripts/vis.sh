#!/usr/bin/env bash
# Prints which html/start_button/* options the export dialog shows (EditorExportPreset::_get_property_list(), through
# vis_addon) for each combination of switches. Needs export.sh's scratch layout ($W/proj, $W/vis_addon).
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w61
E=/mnt/109313D2109313D2/godot-editors/godotwebgpu-61/bin/godot.linuxbsd.editor.x86_64
Z=/mnt/109313D2109313D2/godot-editors/godotwebgpu-61/bin/godot.web.template_release.wasm32.nothreads.zip
p=html/start_button/preload_text; mkdir -p $W/vis
v() { local n=$1; shift; VIS=1 $W/export.sh $E $Z $W/vis/$n "$@" >/dev/null 2>&1; echo "$n: $(grep -o 'VISIBLE_START_OPTIONS.*' $W/vis/$n.export.log | cut -d' ' -f2)"; }
v off
v on html/start_button/enabled=true
v preload_off html/start_button/enabled=true html/start_button/preload=false
v progress_off html/start_button/enabled=true html/start_button/preload_progress=false
v dl_off html/start_button/enabled=true $p/downloading=false
v prog_off html/start_button/enabled=true $p/progress=false
v ready_off html/start_button/enabled=true $p/ready=false
v all_off html/start_button/enabled=true $p/downloading=false $p/progress=false $p/ready=false
v off_with_keys $p/downloading=false "$p/ready_text=\"x\""
