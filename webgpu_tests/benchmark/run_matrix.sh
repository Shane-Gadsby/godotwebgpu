#!/usr/bin/env bash
# Run every benchmark scene on every renderer leg and write results/matrix.tsv (TASKS.md 2.17).
#   legs: native (Vulkan Forward+), webgl (official template), webgpu_mobile, webgpu_forward_plus
# Needs a real GPU, system Chrome, and GODOT_WEBGL_BASELINE_ZIP (defaults to the 4.7.2 official template).
# Usage: ./run_matrix.sh [scene ...]   (default: the 7 README scenes)
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SCENES=("$@")
[[ ${#SCENES[@]} -eq 0 ]] && SCENES=(scene_a_sprites scene_b_pbr scene_c_instances scene_d_particles scene_e_animated scene_f_postfx scene_g_shadows)
export GODOT_WEBGL_BASELINE_ZIP="${GODOT_WEBGL_BASELINE_ZIP:-$HOME/.local/share/godot/export_templates/4.7.2.stable.official/web_nothreads_release.zip}"
export GODOT_WEBGL_BASELINE_EDITOR="${GODOT_WEBGL_BASELINE_EDITOR:-/mnt/109313D2109313D2/godot-editors/editors/Godot_v4.7.1-stable_linux.x86_64}"
OUT="$HERE/results"; mkdir -p "$OUT/logs"
TSV="$OUT/matrix.tsv"
printf 'scene\tleg\tfps_avg\tframe_p95_ms\tcpu_avg_ms\tdraw_calls\n' > "$TSV"
field() { grep -o "\[BENCHMARK\] $1=[0-9.]*" "$2" | tail -1 | sed 's/.*=//'; }
for s in "${SCENES[@]}"; do
  for leg in native webgl webgpu_mobile webgpu_forward_plus; do
    log="$OUT/logs/${s}_${leg}.log"
    case "$leg" in
      native) BENCH_AUTOQUIT=1 "$HERE/run_benchmark.sh" native "$HERE/godot/$s" >"$log" 2>&1 ;;
      webgl) "$HERE/run_benchmark.sh" webgl "$HERE/godot/$s" >"$log" 2>&1 ;;
      webgpu_mobile) BENCH_RENDERER=mobile "$HERE/run_benchmark.sh" webgpu "$HERE/godot/$s" >"$log" 2>&1 ;;
      webgpu_forward_plus) BENCH_RENDERER=forward_plus "$HERE/run_benchmark.sh" webgpu "$HERE/godot/$s" >"$log" 2>&1 ;;
    esac
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$s" "$leg" "$(field fps_avg "$log")" "$(field frame_p95 "$log")" "$(field cpu_avg "$log")" "$(field draw_calls_avg "$log")" | tee -a "$TSV"
  done
done
