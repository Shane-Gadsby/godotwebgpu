#!/usr/bin/env bash
# Waits for the build lease, then builds the dev_mode editor, wgsl_convert_cli and both web templates in work/61's tree.
set -uo pipefail
T=/mnt/109313D2109313D2/godot-editors/godotwebgpu-61
cd $T
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_notes/work/lease.sh
LOG=$1
until $L take build worker-61 2 >/dev/null 2>&1; do echo "waiting for build lease $(date +%T)"; sleep 30; done
echo "lease taken $(date +%T)"
trap '$L drop build worker-61; echo "lease dropped"' EXIT
while pgrep -ax scons | grep -q godotwebgpu-61; do sleep 10; done
export PATH="$HOME/.cache/godotwebgpu-ccache-bin:$PATH" CCACHE_BASEDIR=$T
rc=0
if [[ "${SKIP_EDITOR:-0}" != 1 ]]; then
  scons platform=linuxbsd target=editor dev_mode=yes accesskit=yes webgpu=yes gdscript_braces=yes linker=mold -j24 > "$LOG/editor.log" 2>&1 || rc=1
  echo "editor rc=$rc $(date +%T)"
fi
[[ $rc == 0 && ! -x bin/wgsl_convert_cli ]] && { ./drivers/webgpu/wgsl_cli/build.sh > "$LOG/wgslcli.log" 2>&1 || rc=3; echo "wgslcli rc=$rc"; }
if [[ $rc == 0 ]]; then
  source ~/emsdk/emsdk_env.sh >/dev/null 2>&1
  export PATH="$HOME/.cache/godotwebgpu-ccache-bin:$PATH"
  for t in template_debug template_release; do
    scons platform=web target=$t dlink_enabled=no webgpu=yes opengl3=no threads=no gdscript_braces=yes module_raycast_enabled=yes -j24 > "$LOG/web_$t.log" 2>&1 || { rc=2; echo "$t FAILED"; break; }
    echo "$t ok $(date +%T)"
  done
fi
echo "done rc=$rc"
