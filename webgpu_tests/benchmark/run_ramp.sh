#!/usr/bin/env bash
# Load-ramp benchmark: run godot/load_ramp on every leg and write a markdown summary (ramp_summary.md).
#
# Each ramp grows one kind of content until the frame rate falls to the target (default 20 fps) and reports how much
# content that took (N). Comparing N across legs gives "WebGPU carried 3.2x the draw-call load of WebGL at 20 fps",
# which the fps matrix (run_matrix.sh) cannot give: browsers hold WebGL at the display rate even with the limits lifted.
# Ramps and what one unit of N is: see the header of godot/load_ramp/ramp.gd.
#
# Usage: ./run_ramp.sh [options]
#   --legs a,b,..     native_vulkan_fp, native_compat, webgl, webgpu_mobile, webgpu_fp, firefox_webgpu_fp
#                     (default: all but firefox_webgpu_fp)
#   --ramps a,b,..    draws, nodes, fill, lit, particles (default: all)
#   --target-fps F    default 20
#   --max-s S         time cap per ramp, default 180 (a leg that cannot reach the target reports the cap)
#   --repeat K        run every leg K times (default 1); the summary shows the median and the spread
#   --out DIR         default results_ramp/ (logs in DIR/logs, summary in DIR/ramp_summary.md)
#   --note TEXT       line put in the summary's header (e.g. "functional pass, GPU busy, not a measurement")
#   --no-export       reuse the exports in exports/ramp/
#   --export-only     export the web legs and stop (no GPU needed)
#   --capped          keep the display cap (default lifts it: Chrome --disable-frame-rate-limit --disable-gpu-vsync,
#                     Firefox layout.frame_rate=0, native vsync off), so frame times are not quantized to vblanks
# Environment: GODOT_EDITOR_BIN, GODOT_WEBGPU_TEMPLATE_ZIP (this fork's), GODOT_WEBGL_BASELINE_ZIP and
#   GODOT_WEBGL_BASELINE_EDITOR (official 4.7.2 template + 4.7.1 editor, as in run_matrix.sh), RAMP_PLAYWRIGHT.
# Needs a real GPU (take the gpu lease first), system Chrome, a display.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
PROJECT="$HERE/godot/load_ramp"

EDITOR="${GODOT_EDITOR_BIN:-$REPO/bin/godot.linuxbsd.editor.x86_64}"
WEBGPU_ZIP="${GODOT_WEBGPU_TEMPLATE_ZIP:-$REPO/bin/godot.web.template_release.wasm32.nothreads.zip}"
WEBGL_ZIP="${GODOT_WEBGL_BASELINE_ZIP:-$HOME/.local/share/godot/export_templates/4.7.2.stable.official/web_nothreads_release.zip}"
WEBGL_EDITOR="${GODOT_WEBGL_BASELINE_EDITOR:-/mnt/109313D2109313D2/godot-editors/editors/Godot_v4.7.1-stable_linux.x86_64}"

LEGS="native_vulkan_fp,native_compat,webgl,webgpu_mobile,webgpu_fp"
RAMPS="all"
TARGET=20
MAX_S=180
REPEAT=1
OUT="$HERE/results_ramp"
NOTE=""
EXPORT=1
EXPORT_ONLY=0
CAPPED=0
while [[ $# -gt 0 ]]; do
	case "$1" in
		--legs) LEGS="$2"; shift 2 ;;
		--ramps) RAMPS="$2"; shift 2 ;;
		--target-fps) TARGET="$2"; shift 2 ;;
		--max-s) MAX_S="$2"; shift 2 ;;
		--repeat) REPEAT="$2"; shift 2 ;;
		--out) OUT="$2"; shift 2 ;;
		--note) NOTE="$2"; shift 2 ;;
		--no-export) EXPORT=0; shift ;;
		--export-only) EXPORT_ONLY=1; shift ;;
		--capped) CAPPED=1; shift ;;
		-h|--help) sed -n '2,26p' "$0"; exit 0 ;;
		*) echo "unknown option $1"; exit 1 ;;
	esac
done

NRAMPS=5
[[ "$RAMPS" != "all" ]] && NRAMPS=$(tr ',' '\n' <<<"$RAMPS" | grep -c .)
TIMEOUT_S=$(( NRAMPS * (MAX_S + 60) + 120 ))
EXP="$HERE/exports/ramp"
mkdir -p "$OUT/logs" "$EXP"

[[ -x "$EDITOR" ]] || { echo "no editor at $EDITOR"; exit 1; }

# Fresh copy of the project per leg, so the 4.7 and 4.8 editors never share a .godot/ import cache.
copy_project() {
	local dst="$EXP/proj_$1"
	rm -rf "$dst"
	mkdir -p "$dst"
	cp "$PROJECT/project.godot" "$PROJECT/main.tscn" "$PROJECT/ramp.gd" "$dst/"
	echo "$dst"
}

export_web() {
	local leg="$1" renderer="$2" template="$3" editor="$4"
	local proj out="$EXP/$leg"
	proj="$(copy_project "$leg")"
	sed -i "s|^renderer/rendering_method.web=.*|renderer/rendering_method.web=\"$renderer\"|" "$proj/project.godot"
	cat > "$proj/export_presets.cfg" <<PRESET
[preset.0]

name="Web"
platform="Web"
runnable=true
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path="$out/index.html"
script_export_mode=1

[preset.0.options]

custom_template/debug=""
custom_template/release="$template"
variant/extensions_support=false
variant/thread_support=false
vram_texture_compression/for_desktop=true
vram_texture_compression/for_mobile=false
html/export_icon=false
html/canvas_resize_policy=1
html/focus_canvas_on_start=true
progressive_web_app/enabled=false
PRESET
	rm -rf "$out"
	mkdir -p "$out"
	"$editor" --headless --path "$proj" --import >/dev/null 2>&1 || true
	"$editor" --headless --path "$proj" --export-release "Web" "$out/index.html" >"$EXP/export_$leg.log" 2>&1
	if [[ ! -f "$out/index.html" ]]; then
		echo "export of $leg FAILED (see $EXP/export_$leg.log)"
		return 1
	fi
	echo "exported $leg ($renderer) with $(basename "$editor") + $(basename "$template")"
}

IFS=',' read -r -a LEG_ARR <<<"$LEGS"
if [[ $EXPORT == 1 ]]; then
	for leg in "${LEG_ARR[@]}"; do
		case "$leg" in
			webgl) [[ -f "$WEBGL_ZIP" && -x "$WEBGL_EDITOR" ]] || { echo "webgl leg needs GODOT_WEBGL_BASELINE_ZIP/_EDITOR"; exit 1; }
				export_web webgl gl_compatibility "$WEBGL_ZIP" "$WEBGL_EDITOR" ;;
			webgpu_mobile) export_web webgpu_mobile mobile "$WEBGPU_ZIP" "$EDITOR" ;;
			webgpu_fp|firefox_webgpu_fp) [[ -d "$EXP/webgpu_fp" && "$leg" == firefox_webgpu_fp && " ${LEG_ARR[*]} " == *" webgpu_fp "* ]] && continue
				export_web webgpu_fp forward_plus "$WEBGPU_ZIP" "$EDITOR" ;;
		esac
	done
fi

[[ $EXPORT_ONLY == 1 ]] && exit 0

QUERY="ramps=$RAMPS&target_fps=$TARGET&max_s=$MAX_S"
UARGS=(-- "--ramps=$RAMPS" "--target-fps=$TARGET" "--max-s=$MAX_S")
VSYNC=()
[[ $CAPPED == 1 ]] && VSYNC=(--vsync-mode enabled)   # the project itself has vsync off
BROWSER_FLAGS=()
[[ $CAPPED == 1 ]] && BROWSER_FLAGS=(--capped)

rm -f "$OUT/logs"/*.log   # the summary reads every log in there
T_ALL=$(date +%s)
for ((r = 1; r <= REPEAT; r++)); do
	for leg in "${LEG_ARR[@]}"; do
		log="$OUT/logs/${leg}__r${r}.log"
		t0=$(date +%s)
		{
			echo "[BENCHMARK] leg=$leg run=$r date=$(date -Is) git=$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null)"
			case "$leg" in
				native_vulkan_fp|native_compat)
					proj="$(copy_project "$leg")"
					if [[ $leg == native_vulkan_fp ]]; then RA=(--rendering-driver vulkan --rendering-method forward_plus)
					else RA=(--rendering-driver opengl3 --rendering-method gl_compatibility); fi
					"$EDITOR" --headless --path "$proj" --import >/dev/null 2>&1 || true
					timeout "$TIMEOUT_S" "$EDITOR" "${RA[@]}" --resolution 1280x720 "${VSYNC[@]}" --path "$proj" "${UARGS[@]}" 2>&1
					;;
				webgl|webgpu_mobile|webgpu_fp)
					node "$HERE/ramp_browser.mjs" --dir "$EXP/$leg" --browser chrome --query "$QUERY" --timeout-s "$TIMEOUT_S" "${BROWSER_FLAGS[@]}" 2>&1 ;;
				firefox_webgpu_fp)
					node "$HERE/ramp_browser.mjs" --dir "$EXP/webgpu_fp" --browser firefox --query "$QUERY" --timeout-s "$TIMEOUT_S" "${BROWSER_FLAGS[@]}" 2>&1 ;;
				*) echo "unknown leg $leg" ;;
			esac
		} >"$log" 2>&1
		echo "$leg run $r: $(( $(date +%s) - t0 )) s -> $log"
		grep -E '^\[BENCHMARK\] ramp=' "$log" | sed 's/^/    /' | cut -c1-160
	done
done
echo "total $(( $(date +%s) - T_ALL )) s"

python3 "$HERE/summarize_ramp.py" --note "$NOTE" "$OUT/logs" >"$OUT/ramp_summary.md"
echo "summary: $OUT/ramp_summary.md"
cat "$OUT/ramp_summary.md"
