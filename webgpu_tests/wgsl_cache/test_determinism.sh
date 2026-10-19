#!/usr/bin/env bash
# wgsl_precompile.py must be byte-for-byte reproducible: run twice, the second time with a
# different PYTHONHASHSEED, and compare the generated headers (TASKS.md 2.16 gap 5).
# Exit 2 = SKIP when glslangValidator or tint_convert_cli is missing.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
command -v glslangValidator >/dev/null || { echo "SKIP -- glslangValidator not installed"; exit 2; }
[[ -x "$REPO/bin/tint_convert_cli" ]] || { echo "SKIP -- bin/tint_convert_cli not built"; exit 2; }
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
OUT=$(python3 "$REPO/drivers/webgpu/wgsl_precompile.py" "$REPO" "$TMP/a.h" 2>&1) || { echo "first run failed"; exit 1; }
# Run from this directory on purpose: includes must not depend on the cwd. A run that drops
# shaders still exits 0, so the failure counts are the real check.
grep -q "0 glsl failures, 0 tint failures" <<<"$OUT" || { echo "precompile reported failures:"; grep -E "FAIL|WARNING|Results" <<<"$OUT" | head; exit 1; }
PYTHONHASHSEED=7 python3 "$REPO/drivers/webgpu/wgsl_precompile.py" "$REPO" "$TMP/b.h" >/dev/null 2>&1 || { echo "second run failed"; exit 1; }
if cmp -s "$TMP/a.h" "$TMP/b.h"; then
    echo "deterministic: $(wc -c < "$TMP/a.h") bytes identical across runs"
else
    echo "NOT deterministic: precompiled headers differ"; diff <(head -c 4000 "$TMP/a.h") <(head -c 4000 "$TMP/b.h") | head -10; exit 1
fi
