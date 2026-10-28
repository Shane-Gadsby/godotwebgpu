#!/usr/bin/env bash
# Exports every scenario of preload_text_test.mjs for task 69. $1 = new template zip, $2 = base-shell template zip (the
# new zip with webgpu-4.8's full-size.html as godot.html, made by base_zip.py). The project keeps the default boot splash
# background, the one the default line colour is chosen for.
set -euo pipefail
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w69
S=$(dirname "$(realpath "$0")")
E=/mnt/109313D2109313D2/godot-editors/godotwebgpu-69/bin/godot.linuxbsd.editor.x86_64
NEW=$1 BASE=$2
on='html/start_button/enabled=true'
p=html/start_button/preload_text
# What the base shells drew: #e0e0e0 at 85% opacity. The new editor's default is #c4c4c4, so the base exports set the
# old colour explicitly, as every preset saved before this task does.
e0="$p/color=Color(0.8784314, 0.8784314, 0.8784314, 1)"
styled=("$p/color=Color(1, 0.5019608, 0, 1)" "$p/font_family=\"'DejaVu Serif', serif\"" "$p/font_size=32.0")
x() { echo "== $1"; local n=$1 z=$2; shift 2; $S/export.sh $E $z $W/exp/$n "$on" "$@"; }
x base $BASE "$e0"
x default $NEW
x exact_e0 $NEW "$e0"
x styled $NEW "${styled[@]}"
x wg_base $NEW "$e0" "html/custom_html_shell=\"$W/base_webgpu-full-size.html\""
x wg_default $NEW "html/custom_html_shell=\"$W/new_webgpu-full-size.html\""
x wg_styled $NEW "${styled[@]}" "html/custom_html_shell=\"$W/new_webgpu-full-size.html\""
# Derived copies: nokey = default with preloadText removed from the config (an old or custom config); *_nosize = fileSizes
# removed, so the "Downloading..." state holds still (TASKS.md trap 96).
python3 -I $S/derive.py $W/exp/default $W/exp/nokey preloadText
python3 -I $S/derive.py $W/exp/base $W/exp/base_nosize fileSizes
python3 -I $S/derive.py $W/exp/default $W/exp/default_nosize fileSizes
