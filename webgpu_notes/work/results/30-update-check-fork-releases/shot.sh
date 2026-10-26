#!/usr/bin/env bash
# shot.sh <editor> <out.png> <network_mode 0|1> [GODOT_UPDATE_CHECK_AS value]
# Opens the project manager windowed under Xvfb with scratch editor settings and screenshots it after the update
# check had time to answer. GODOT_UPDATE_CHECK_AS (<hash>:<commit UNIX time>) poses a dev build as another build.
set -euo pipefail
editor="$1" out="$2" mode="$3" as="${4:-}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/config/godot" "$work/data" "$work/cache"
cat >"$work/config/godot/editor_settings-4.8.tres" <<TRES
[gd_resource type="EditorSettings" format=3]

[resource]
network/connection/network_mode = $mode
TRES
XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache" GODOT_UPDATE_CHECK_AS="$as" \
	xvfb-run -a -s "-screen 0 1280x800x24" bash -c '
		"$0" --project-manager --rendering-driver opengl3 >"$1.log" 2>&1 &
		pid=$!
		sleep 15
		import -window root "$1"
		kill $pid; wait $pid 2>/dev/null || true
	' "$editor" "$out"
echo "wrote $out"
