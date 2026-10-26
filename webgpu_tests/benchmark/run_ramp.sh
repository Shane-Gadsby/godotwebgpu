#!/usr/bin/env bash
# Load-ramp benchmark: the source of the README's performance table (readme_perf_table.py turns a result into it).
#
# Each ramp grows one kind of content until the frame rate falls to the target (default 20 fps) and reports how much
# content that took (N). Comparing N across legs gives "WebGPU carried 3.2x the draw-call load of WebGL at 20 fps",
# which a plain fps reading cannot give: browsers hold WebGL at the display rate even with the limits lifted.
# Ramps and what one unit of N is: godot/load_ramp/ramp.gd (header) and webgpu_tests/README.md.
#
# Usage: ./run_ramp.sh [options]
#   --legs a,b,..     native_vulkan_fp, native_compat, webgl, webgpu_mobile, webgpu_fp, firefox_webgpu_fp
#                     (default: all; firefox_webgpu_fp only if Playwright's Firefox is installed)
#   --ramps a,b,..    draws, nodes, fill, lit, particles (default: all)
#   --repeat K        runs per leg (default 2); the summary shows every run, the median and the spread
#   --target-fps F    default 20
#   --max-s S         time cap per ramp, default 180 (a leg that cannot reach the target reports the cap as a bound)
#   --out DIR         default results_ramp/<YYYY-MM-DD>[-k]/ (logs/, shots/, env.txt, ramp_summary.md)
#   --note TEXT       line put in the summary's header
#   --capped          keep the display cap (default lifts it: Chrome --disable-frame-rate-limit --disable-gpu-vsync,
#                     Firefox layout.frame_rate=0, native vsync off), so frame times are not quantized to vblanks
#   --force           run even if the GPU looks busy (the summary says so)
#   --no-export       reuse the exports in exports/ramp/ (development only: the default exports fresh every run)
# Environment: GODOT_EDITOR_BIN, GODOT_WEBGPU_TEMPLATE_ZIP (default: this tree's bin/), GODOT_WEBGL_BASELINE_ZIP and
#   GODOT_WEBGL_BASELINE_EDITOR (official 4.7.2 template + 4.7.x editor), RAMP_PLAYWRIGHT (dir with
#   node_modules/playwright), RAMP_LEASE_OWNER (gpu lease owner name, default "run_ramp-$USER").
# Needs: a real, idle GPU, a display, system Chrome, node + Playwright, python3. Takes ~5 min per leg and run
# (all six legs x 2 runs: about an hour).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
PROJECT="$HERE/godot/load_ramp"

EDITOR="${GODOT_EDITOR_BIN:-$REPO/bin/godot.linuxbsd.editor.x86_64}"
WEBGPU_ZIP="${GODOT_WEBGPU_TEMPLATE_ZIP:-$REPO/bin/godot.web.template_release.wasm32.nothreads.zip}"
WEBGL_ZIP="${GODOT_WEBGL_BASELINE_ZIP:-$HOME/.local/share/godot/export_templates/4.7.2.stable.official/web_nothreads_release.zip}"
WEBGL_EDITOR="${GODOT_WEBGL_BASELINE_EDITOR:-/mnt/109313D2109313D2/godot-editors/editors/Godot_v4.7.1-stable_linux.x86_64}"
CHROME=/usr/bin/google-chrome-stable
LEASE="$REPO/webgpu_notes/work/lease.sh"
LEASE_OWNER="${RAMP_LEASE_OWNER:-run_ramp-$USER}"

LEGS=""
RAMPS="all"
TARGET=20
MAX_S=180
REPEAT=2
OUT=""
NOTE=""
EXPORT=1
CAPPED=0
FORCE=0
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
		--capped) CAPPED=1; shift ;;
		--force) FORCE=1; shift ;;
		-h|--help) sed -n '2,30p' "$0"; exit 0 ;;
		*) echo "unknown option $1 (--help)"; exit 1 ;;
	esac
done

fail() { echo "run_ramp: $*" >&2; exit 1; }

# --- Prerequisites ---------------------------------------------------------------------------------------------------
PW_DIR=""
for d in "${RAMP_PLAYWRIGHT:-}" "$HERE/../scene_smoketest"; do
	[[ -n "$d" && -f "$d/node_modules/playwright/index.mjs" ]] && { PW_DIR="$d"; break; }
done
FF_BUILD="$(ls -d "$HOME"/.cache/ms-playwright/firefox-* 2>/dev/null | sort -V | tail -1)"
if [[ -z "$LEGS" ]]; then
	LEGS="native_vulkan_fp,native_compat,webgl,webgpu_mobile,webgpu_fp"
	[[ -n "$FF_BUILD" ]] && LEGS="$LEGS,firefox_webgpu_fp"
fi
IFS=',' read -r -a LEG_ARR <<<"$LEGS"
has_leg() { [[ ",$LEGS," == *",$1,"* ]]; }
web_legs=0
for leg in "${LEG_ARR[@]}"; do
	case "$leg" in
		native_vulkan_fp|native_compat) ;;
		webgl|webgpu_mobile|webgpu_fp|firefox_webgpu_fp) web_legs=1 ;;
		*) fail "unknown leg '$leg'" ;;
	esac
done
command -v python3 >/dev/null || fail "python3 not found"
command -v nvidia-smi >/dev/null || fail "nvidia-smi not found: the GPU-idle check needs it (an NVIDIA GPU is assumed)"
[[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]] || fail "no display: every leg runs headed (headless browsers get no real adapter)"
[[ -x "$EDITOR" ]] || fail "editor not found: $EDITOR (build it, or set GODOT_EDITOR_BIN)"
if [[ $web_legs == 1 ]]; then
	command -v node >/dev/null || fail "node not found"
	[[ -n "$PW_DIR" ]] || fail "Playwright not found: run npm install in webgpu_tests/scene_smoketest, or set RAMP_PLAYWRIGHT"
fi
if has_leg webgpu_fp || has_leg webgpu_mobile || has_leg firefox_webgpu_fp; then
	[[ -f "$WEBGPU_ZIP" ]] || fail "WebGPU template not found: $WEBGPU_ZIP (build it, or set GODOT_WEBGPU_TEMPLATE_ZIP)"
fi
if has_leg webgl; then
	[[ -f "$WEBGL_ZIP" ]] || fail "official 4.7.2 WebGL template not found: $WEBGL_ZIP
  install the official 4.7.2 export templates, or set GODOT_WEBGL_BASELINE_ZIP to its web_nothreads_release.zip"
	[[ -x "$WEBGL_EDITOR" ]] || fail "official 4.7.x editor not found: $WEBGL_EDITOR (a 4.8 pck does not load in a 4.7 template;
  set GODOT_WEBGL_BASELINE_EDITOR)"
fi
if has_leg webgl || has_leg webgpu_mobile || has_leg webgpu_fp; then
	[[ -x "$CHROME" ]] || fail "system Chrome not found at $CHROME"
fi
if has_leg firefox_webgpu_fp; then
	[[ -n "$FF_BUILD" ]] || fail "Playwright's Firefox is not installed (npx playwright install firefox), or drop the firefox_webgpu_fp leg"
fi
pgrep -x scons >/dev/null && echo "run_ramp: WARNING: an scons build is running; its CPU load will lower every leg's N"

# GPU idle: no other process holding over 1 GB, and utilization <= 25% while nothing of ours runs (a desktop with a
# browser open idles at 10-15% here; a game reads 90+).
gpu_busy() {
	local util procs
	util=$(for _ in 1 2 3 4 5; do nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits | head -1; sleep 0.5; done | sort -n | sed -n 3p)
	procs=$(nvidia-smi --query-compute-apps=pid,process_name,used_memory --format=csv,noheader,nounits 2>/dev/null | awk -F', ' '$3 > 1024 { printf "%s(%s, %s MiB) ", $2, $1, $3 }')
	if [[ "${util:-0}" -gt 25 || -n "$procs" ]]; then
		echo "GPU busy: utilization ${util}% (median of 5), processes over 1 GB: ${procs:-none}"
		return 0
	fi
	return 1
}
GPU_NOTE=""
if msg=$(gpu_busy); then
	if [[ $FORCE == 1 ]]; then
		GPU_NOTE="--force: $msg at start; these numbers are contaminated"
		echo "run_ramp: WARNING: $GPU_NOTE"
	else
		fail "$msg -- close it and re-run (or --force for a functional pass)"
	fi
fi

# gpu lease (the shared-machine board), only where it exists.
LEASE_TAKEN=0
if [[ -x "$LEASE" ]]; then
	hours=$(( (${#LEG_ARR[@]} * REPEAT * 7 + 59) / 60 + 1 ))
	"$LEASE" take gpu "$LEASE_OWNER" "$hours" || fail "gpu lease is held (see above); set RAMP_LEASE_OWNER if it is yours"
	LEASE_TAKEN=1
fi
cleanup() { [[ $LEASE_TAKEN == 1 ]] && "$LEASE" drop gpu "$LEASE_OWNER" >/dev/null 2>&1; }
trap cleanup EXIT

# --- Output directory and environment record -------------------------------------------------------------------------
if [[ -z "$OUT" ]]; then
	OUT="$HERE/results_ramp/$(date +%F)"
	k=2
	while [[ -e "$OUT" ]]; do OUT="$HERE/results_ramp/$(date +%F)-$k"; k=$((k + 1)); done
fi
mkdir -p "$OUT/logs"
rm -f "$OUT/logs"/*.log
EXP="$HERE/exports/ramp"
mkdir -p "$EXP"
{
	echo "date: $(date '+%F %H:%M %Z')"
	echo "machine: $(hostname), $(lscpu | sed -n 's/^Model name: *//p' | head -1), $(free -g | awk '/Mem:/ {print $2}') GB RAM"
	echo "gpu: $(nvidia-smi --query-gpu=name,memory.total --format=csv,noheader | head -1), driver $(nvidia-smi --query-gpu=driver_version --format=csv,noheader | head -1)"
	echo "os: $(. /etc/os-release 2>/dev/null; echo "${PRETTY_NAME:-Linux}"), kernel $(uname -r), $(xrandr 2>/dev/null | awk '/\*/ {print $1 " @ " $2; exit}' | tr -d '*+')"
	echo "chrome: $("$CHROME" --version 2>/dev/null)"
	[[ -n "$FF_BUILD" ]] && echo "firefox: Playwright Firefox build $(basename "$FF_BUILD" | sed 's/firefox-//') ($(sed -n 's/^Version=//p' "$FF_BUILD"/firefox/application.ini 2>/dev/null | head -1))"
	echo "engine: $("$EDITOR" --version 2>/dev/null | tail -1) (native legs, WebGPU export)"
	has_leg webgl && echo "webgl: official $(basename "$(dirname "$WEBGL_ZIP")") template, exported with $("$WEBGL_EDITOR" --version 2>/dev/null | tail -1)"
	echo "tree: $(git -C "$REPO" rev-parse --short HEAD)$(git -C "$REPO" diff --quiet HEAD -- 2>/dev/null || echo ' (dirty)') $(git -C "$REPO" rev-parse --abbrev-ref HEAD)"
	echo "settings: target_fps=$TARGET max_s=$MAX_S repeat=$REPEAT capped=$CAPPED legs=$LEGS ramps=$RAMPS"
	[[ -n "$GPU_NOTE" ]] && echo "warning: $GPU_NOTE"
} >"$OUT/env.txt"
cat "$OUT/env.txt"

NRAMPS=5
[[ "$RAMPS" != "all" ]] && NRAMPS=$(tr ',' '\n' <<<"$RAMPS" | grep -c .)
TIMEOUT_S=$(( NRAMPS * (MAX_S + 60) + 120 ))

# --- Export ----------------------------------------------------------------------------------------------------------
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
	cat >"$proj/export_presets.cfg" <<PRESET
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
	[[ -f "$out/index.html" ]] || fail "export of $leg failed (see $EXP/export_$leg.log)"
	echo "exported $leg ($renderer) with $(basename "$editor") + $(basename "$template")"
}

if [[ $EXPORT == 1 ]]; then
	has_leg webgl && export_web webgl gl_compatibility "$WEBGL_ZIP" "$WEBGL_EDITOR"
	has_leg webgpu_mobile && export_web webgpu_mobile mobile "$WEBGPU_ZIP" "$EDITOR"
	{ has_leg webgpu_fp || has_leg firefox_webgpu_fp; } && export_web webgpu_fp forward_plus "$WEBGPU_ZIP" "$EDITOR"
fi

# --- Run -------------------------------------------------------------------------------------------------------------
QUERY="ramps=$RAMPS&target_fps=$TARGET&max_s=$MAX_S"
UARGS=(-- "--ramps=$RAMPS" "--target-fps=$TARGET" "--max-s=$MAX_S")
VSYNC=()
BROWSER_FLAGS=()
[[ $CAPPED == 1 ]] && { VSYNC=(--vsync-mode enabled); BROWSER_FLAGS=(--capped); }   # the project itself has vsync off

T_ALL=$(date +%s)
ABORT=""
for ((r = 1; r <= REPEAT; r++)); do
	for leg in "${LEG_ARR[@]}"; do
		# Between legs nothing of ours runs: anything on the GPU now is someone else's, and would skew this leg.
		if [[ $FORCE == 0 ]] && msg=$(gpu_busy); then
			ABORT="stopped before $leg run $r: $msg"
			echo "run_ramp: $ABORT"
			break 2
		fi
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
					RAMP_SHOTS_DIR="$OUT/shots/${leg}__r$r" timeout "$TIMEOUT_S" "$EDITOR" "${RA[@]}" --resolution 1280x720 "${VSYNC[@]}" --path "$proj" "${UARGS[@]}" 2>&1
					;;
				webgl|webgpu_mobile|webgpu_fp)
					RAMP_PLAYWRIGHT="$PW_DIR" node "$HERE/ramp_browser.mjs" --dir "$EXP/$leg" --browser chrome --query "$QUERY" --timeout-s "$TIMEOUT_S" --shots "$OUT/shots/${leg}__r$r" "${BROWSER_FLAGS[@]}" 2>&1 ;;
				firefox_webgpu_fp)
					RAMP_PLAYWRIGHT="$PW_DIR" node "$HERE/ramp_browser.mjs" --dir "$EXP/webgpu_fp" --browser firefox --query "$QUERY" --timeout-s "$TIMEOUT_S" --shots "$OUT/shots/${leg}__r$r" "${BROWSER_FLAGS[@]}" 2>&1 ;;
			esac
		} >"$log" 2>&1
		echo "$leg run $r: $(( $(date +%s) - t0 )) s -> $log"
		grep -E '^\[BENCHMARK\] ramp=' "$log" | sed 's/^\[BENCHMARK\] /    /' | cut -c1-110
	done
done
echo "total $(( $(date +%s) - T_ALL )) s"
[[ -n "$ABORT" ]] && echo "aborted: $ABORT" >>"$OUT/env.txt"

python3 "$HERE/summarize_ramp.py" --note "$NOTE" "$OUT" >"$OUT/ramp_summary.md"
echo "summary: $OUT/ramp_summary.md"
echo "README table: python3 $HERE/readme_perf_table.py $OUT [--write]"
[[ -n "$ABORT" ]] && exit 3
exit 0
