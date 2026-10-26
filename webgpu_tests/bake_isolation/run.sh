#!/usr/bin/env bash
# Shader-bake isolation: the export-time shader baker must not change what the editor itself renders.
#
# While a WebGPU export bakes, RenderingDevice::has_feature() answers as the export target (no image atomics, no
# FP16, ...), so the baked shaders get the target's defines. The editor keeps drawing frames between the export's
# steps, and until 2026-10-26 those frames saw the target's answers too: with FSR 2 on in the project, every editor
# frame during an export logged "Shader 'Fsr2...PassShaderRD:N' Mismatch uniform type ... Expected 'Image',
# supplied: 'StorageBuffer'" (work task 27). This runs an interactive export (plugin in project/) from a windowed
# editor under Xvfb whose 3D viewport has FSR 2 + TAA on, then checks:
#   - the editor drew frames during the export (otherwise the run proves nothing, and fails as inconclusive);
#   - no "Mismatch uniform type" / "Uniforms were never supplied" anywhere in the editor's log;
#   - every FSR 2 pass was baked, and none of them in the editor's flavor (an FP16 or image-atomics stage the
#     WebGPU bake has to drop: "not baking one shader stage [Fsr2...").
# Needs a Vulkan device under Xvfb (the NVIDIA driver provides one). EDITOR_BIN overrides the editor, TEMPLATE the
# web template zip (any web template works: nothing loads the export). KEEP=1 keeps the scratch dir.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EDITOR="${EDITOR_BIN:-$ROOT/bin/godot.linuxbsd.editor.x86_64}"
TEMPLATE="${TEMPLATE:-$ROOT/bin/godot.web.template_release.wasm32.nothreads.zip}"
command -v xvfb-run >/dev/null || { echo "SKIP -- xvfb-run not installed"; exit 2; }
[[ -x "$EDITOR" ]] || { echo "FAIL -- no editor at $EDITOR"; exit 1; }
[[ -f "$TEMPLATE" ]] || { echo "FAIL -- no web template at $TEMPLATE"; exit 1; }
WORK="$(mktemp -d)"
if [[ "${KEEP:-0}" == 1 ]]; then echo "scratch: $WORK"; else trap 'rm -rf "$WORK"' EXIT; fi
cp -r "$ROOT/webgpu_tests/bake_isolation/project" "$WORK/project"
mkdir -p "$WORK/export"
cat > "$WORK/project/export_presets.cfg" <<PRESET
[preset.0]

name="Web"
platform="Web"
runnable=true
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path="$WORK/export/index.html"
script_export_mode=1

[preset.0.options]

custom_template/debug="$TEMPLATE"
custom_template/release="$TEMPLATE"
variant/extensions_support=false
variant/thread_support=false
html/export_icon=false
PRESET
LOG="$WORK/editor.log"
"$EDITOR" --headless --path "$WORK/project" --import >/dev/null 2>&1 || true
BAKE_ISO_OUT="$WORK/export/index.html" timeout 400 xvfb-run -a -s "-screen 0 1600x900x24" \
	"$EDITOR" --editor --verbose --rendering-driver vulkan --path "$WORK/project" >"$LOG" 2>&1
rc=$?
grep -E "\[BAKE-ISO\]" "$LOG"
fail=0
bad() { echo "FAIL -- $*"; fail=1; }

[[ $rc -eq 0 ]] || bad "editor exited with $rc"
grep -q "Vulkan .* Using Device" "$LOG" || bad "the editor did not get a Vulkan device"
grep -q "\[BAKE-ISO\] export err=0 " "$LOG" || bad "the export did not succeed"
grep -q "\[BAKE-ISO\] viewport scaling_3d_mode=2 use_taa=true" "$LOG" || bad "the editor's 3D viewport is not running FSR 2 + TAA"
during="$(sed -n 's/.*frames_during_export=\([0-9]*\).*/\1/p' "$LOG" | head -1)"
[[ "${during:-0}" -gt 0 ]] || bad "inconclusive: the editor drew no frame during the export"
grep -q "Shader baker: baking Forward+'s advanced shader variants" "$LOG" || bad "the shader baker did not run"

mismatch="$(grep -cE "Mismatch uniform type|Uniforms were never supplied" "$LOG")"
[[ "$mismatch" -eq 0 ]] || { bad "$mismatch uniform-set error(s) in the editor's log:"; grep -E "Mismatch uniform type" "$LOG" | sort | uniq -c | head; }

for pass in DepthClip ReconstructPreviousDepth Lock Accumulate Rcas ComputeLuminancePyramid AutogenReactive TcrAutogen; do
	grep -q "Shader baker: baking 'Fsr2${pass}PassShaderRD/" "$LOG" || bad "Fsr2${pass}PassShaderRD was not baked"
done
editor_flavor="$(grep -c "not baking one shader stage \[Fsr2" "$LOG")"
[[ "$editor_flavor" -eq 0 ]] || { bad "$editor_flavor FSR 2 stage(s) were baked in the editor's flavor and dropped:"; grep "not baking one shader stage \[Fsr2" "$LOG"; }

if [[ $fail -eq 0 ]]; then
	echo "PASS -- $during editor frame(s) drawn during the bake, no uniform-set errors, all 8 FSR 2 passes baked in WebGPU's flavor"
fi
exit $fail
