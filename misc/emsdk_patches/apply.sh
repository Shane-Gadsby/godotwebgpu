#!/usr/bin/env bash
#
# Applies this fork's Emscripten toolchain patches, and tells you loudly when the
# toolchain has moved out from under them.
#
#   ./apply.sh --status   what is applied, against which toolchain  (default)
#   ./apply.sh --check    exit 0 if applied, 1 if not. For CI and SConstruct.
#   ./apply.sh --apply    apply (idempotent -- re-running is a no-op)
#   ./apply.sh --revert   restore the upstream text
#
# Only needed for `platform=web threads=yes dlink_enabled=yes` builds; every other
# configuration is unaffected. See 0001-dylink-asm-consts-pthread-race.patch for
# what the bug is and why the obvious one-line version of the fix is worse than
# the crash.
#
# `emsdk install` overwrites the toolchain tree, so this has to be re-applied
# after EVERY toolchain change, not just after an emsdk version bump.
set -euo pipefail

# --- locate the toolchain -----------------------------------------------------
# Resolved from the environment rather than hard-coded, so this works on a
# machine whose emsdk is not at ~/emsdk.
if [[ -n "${EMSCRIPTEN_ROOT:-}" ]]; then
	EM_ROOT="$EMSCRIPTEN_ROOT"
elif command -v emcc >/dev/null 2>&1; then
	EM_ROOT="$(dirname "$(readlink -f "$(command -v emcc)")")"
elif [[ -n "${EMSDK:-}" && -d "$EMSDK/upstream/emscripten" ]]; then
	EM_ROOT="$EMSDK/upstream/emscripten"
else
	echo "error: cannot find Emscripten. Source emsdk_env.sh, or set EMSCRIPTEN_ROOT." >&2
	exit 2
fi

PY_FILE="$EM_ROOT/tools/emscripten.py"
JS_FILE="$EM_ROOT/src/lib/libdylink.js"
VERSION_FILE="$EM_ROOT/emscripten-version.txt"

for f in "$PY_FILE" "$JS_FILE"; do
	[[ -f "$f" ]] || { echo "error: expected file not found: $f" >&2; exit 2; }
done

EM_VERSION="$(tr -d '"' < "$VERSION_FILE" 2>/dev/null || echo unknown)"
DERIVED_AGAINST="6.0.9"

# --- the exact upstream text this patch replaces ------------------------------
# Pinned by content hash, never by line number: if upstream edits either of these
# lines, the hash stops matching and we refuse rather than fuzzily applying a
# patch whose assumptions no longer hold.
PY_OLD="    pre += 'var ASM_CONSTS = {\\n  ' + ',  \\n '.join(asm_const_pairs) + '\\n};\\n'"
JS_OLD="          {{{ makeEval('ASM_CONSTS[start] = eval(func)') }}};"
PY_OLD_SHA="cc195787d55070c52de97303d12f1fe608cf682a5d009d5a6585cb02eb40920d"
JS_OLD_SHA="b3c3add0fa4ca2a7440cec534c1ed9cbb6946c4826f00be99832d746e671f965"

MARKER="GODOT WEBGPU PATCH (dylink+pthread ASM_CONSTS race)"

sha_of() { printf '%s' "$1" | sha256sum | cut -d' ' -f1; }

# Per-file, because a half-applied toolchain is a state that really happens (an
# interrupted --apply, or a partially restored tree) and must be recoverable: if
# one file is already patched, its upstream text is gone, so a whole-or-nothing
# check would refuse to ever finish the job.
file_applied() { grep -qF "$MARKER" "$1"; }

is_applied() {
	file_applied "$PY_FILE" && file_applied "$JS_FILE"
}

# Refuses, loudly and with everything needed to migrate it forward, rather than
# ever applying to text it does not recognize.
#
# Matching is WHOLE-LINE and by hash, never substring: an upstream line with
# anything appended to it still *contains* the old text, so a substring match
# would happily patch a line whose meaning has changed. That is the exact fuzzy
# apply this pinning exists to prevent -- a deliberately-broken-pin test caught
# it doing so.
line_sha_present() { # file, expected_sha -> 0 if some line hashes to it
	python3 - "$1" "$2" <<'PYEOF'
import hashlib, sys
path, want = sys.argv[1], sys.argv[2]
with open(path) as f:
    for line in f:
        if hashlib.sha256(line.rstrip('\n').encode()).hexdigest() == want:
            sys.exit(0)
sys.exit(1)
PYEOF
}

# Only files that are not already patched need to match the upstream pins.
verify_pins() {
	local ok=0
	if ! file_applied "$PY_FILE" && ! line_sha_present "$PY_FILE" "$PY_OLD_SHA"; then
		echo "error: $PY_FILE has no line matching the expected upstream text." >&2
		echo "       expected sha256: $PY_OLD_SHA (of the whole line this patch replaces)" >&2
		ok=1
	fi
	if ! file_applied "$JS_FILE" && ! line_sha_present "$JS_FILE" "$JS_OLD_SHA"; then
		echo "error: $JS_FILE has no line matching the expected upstream text." >&2
		echo "       expected sha256: $JS_OLD_SHA (of the whole line this patch replaces)" >&2
		ok=1
	fi
	if [[ $ok -ne 0 ]]; then
		echo >&2
		echo "This patch was derived against Emscripten $DERIVED_AGAINST; this toolchain is $EM_VERSION." >&2
		echo "Upstream has changed the code being patched. Do NOT force it -- re-derive it:" >&2
		echo "  1. Check whether the bug still reproduces at all (see the patch header's" >&2
		echo "     'HOW TO TELL WHEN THIS CAN BE DELETED'). If it does not, delete this" >&2
		echo "     directory and the SConstruct check that calls it." >&2
		echo "  2. If it does, re-derive both halves and update the pins in this script." >&2
		echo "See misc/emsdk_patches/0001-dylink-asm-consts-pthread-race.patch and" >&2
		echo "webgpu_notes/TASKS.md Task 12 bug #2." >&2
		return 1
	fi
	# Belt and braces: the greps above already prove the text is present, these
	# confirm the pins in this script describe the text we think they do.
	[[ "$(sha_of "$PY_OLD")" == "$PY_OLD_SHA" ]] || { echo "error: PY pin self-check failed; this script was edited inconsistently." >&2; return 1; }
	[[ "$(sha_of "$JS_OLD")" == "$JS_OLD_SHA" ]] || { echo "error: JS pin self-check failed; this script was edited inconsistently." >&2; return 1; }
	return 0
}

do_apply() {
	if is_applied; then
		echo "already applied (Emscripten $EM_VERSION at $EM_ROOT) -- nothing to do"
		return 0
	fi
	verify_pins || exit 3
	python3 - "$PY_FILE" "$JS_FILE" "$MARKER" <<'PYEOF'
import sys
py_file, js_file, marker = sys.argv[1], sys.argv[2], sys.argv[3]

py_old = "    pre += 'var ASM_CONSTS = {\\n  ' + ',  \\n '.join(asm_const_pairs) + '\\n};\\n'"
py_new = (
    "    # GODOT WEBGPU PATCH (dylink+pthread ASM_CONSTS race): merge into whatever a\n"
    "    # side module already registered on this thread instead of replacing it. The\n"
    "    # `var` declaration hoists, but this initializer runs ~900KB later in the\n"
    "    # module's JS, so on a freshly spawned pthread worker a side module's\n"
    "    # addEmAsm() can populate ASM_CONSTS before this line -- and a plain `=` then\n"
    "    # silently discards those registrations.\n"
    "    pre += 'var ASM_CONSTS = Object.assign(typeof ASM_CONSTS != \"undefined\" && ASM_CONSTS || {}, {\\n  ' + ',  \\n '.join(asm_const_pairs) + '\\n});\\n'"
)
js_old = "          {{{ makeEval('ASM_CONSTS[start] = eval(func)') }}};"
js_new = (
    "          // GODOT WEBGPU PATCH (dylink+pthread ASM_CONSTS race): on a freshly spawned\n"
    "          // pthread worker this can run before the module's own `var ASM_CONSTS = {...}`\n"
    "          // initializer. Safe only because that initializer now merges rather than\n"
    "          // replaces (tools/emscripten.py); on its own this guard would convert a loud\n"
    "          // abort into silently-discarded EM_ASM bodies.\n"
    "          ASM_CONSTS ??= {};\n"
    "          {{{ makeEval('ASM_CONSTS[start] = eval(func)') }}};"
)
for path, old, new in ((py_file, py_old, py_new), (js_file, js_old, js_new)):
    body = open(path).read()
    if marker in body:
        continue  # this half is already applied; leave it alone
    lines = body.split('\n')
    hits = [i for i, line in enumerate(lines) if line == old]
    if len(hits) != 1:
        sys.exit(f"error: expected exactly one line matching the upstream text in {path}, found {len(hits)}")
    lines[hits[0]] = new
    open(path, 'w').write('\n'.join(lines))
PYEOF
	echo "applied to Emscripten $EM_VERSION at $EM_ROOT"
	echo "NOTE: scons will not relink just because the toolchain changed. Delete the"
	echo "      target .js/.wasm (or touch a source) or the next build silently keeps"
	echo "      the old output."
}

do_revert() {
	if ! is_applied; then
		echo "not applied -- nothing to revert"
		return 0
	fi
	python3 - "$PY_FILE" "$JS_FILE" <<'PYEOF'
import re, sys
py_file, js_file = sys.argv[1], sys.argv[2]

s = open(py_file).read()
s = re.sub(
    r"    # GODOT WEBGPU PATCH \(dylink\+pthread ASM_CONSTS race\):.*?\n    pre \+= 'var ASM_CONSTS = Object\.assign\(typeof ASM_CONSTS != \"undefined\" && ASM_CONSTS \|\| \{, \{\\n  ' \+ ',  \\n '\.join\(asm_const_pairs\) \+ '\\n\}\);\\n'",
    "", s, flags=re.S)
# The regex above is deliberately strict; fall back to a line-oriented revert.
lines = open(py_file).read().split('\n')
out, skip = [], False
for line in lines:
    if 'GODOT WEBGPU PATCH (dylink+pthread ASM_CONSTS race)' in line and line.lstrip().startswith('#'):
        skip = True
        continue
    if skip and line.lstrip().startswith('#'):
        continue
    skip = False
    if "pre += 'var ASM_CONSTS = Object.assign(" in line:
        out.append("    pre += 'var ASM_CONSTS = {\\n  ' + ',  \\n '.join(asm_const_pairs) + '\\n};\\n'")
        continue
    out.append(line)
open(py_file, 'w').write('\n'.join(out))

lines = open(js_file).read().split('\n')
out, skip = [], False
for line in lines:
    if 'GODOT WEBGPU PATCH (dylink+pthread ASM_CONSTS race)' in line and line.lstrip().startswith('//'):
        skip = True
        continue
    if skip and line.lstrip().startswith('//'):
        continue
    skip = False
    if line.strip() == 'ASM_CONSTS ??= {};':
        continue
    out.append(line)
open(js_file, 'w').write('\n'.join(out))
PYEOF
	if is_applied; then
		echo "error: revert did not fully remove the patch; restore the toolchain with 'emsdk install'" >&2
		exit 4
	fi
	echo "reverted; Emscripten $EM_VERSION at $EM_ROOT is back to upstream text"
}

do_status() {
	echo "toolchain:       $EM_ROOT"
	echo "version:         $EM_VERSION (patch derived against $DERIVED_AGAINST)"
	if is_applied; then
		echo "0001-dylink-...: APPLIED"
	elif file_applied "$PY_FILE" || file_applied "$JS_FILE"; then
		echo "0001-dylink-...: PARTIALLY APPLIED -- run --apply to finish, or --revert"
		echo "                 tools/emscripten.py:  $(file_applied "$PY_FILE" && echo patched || echo upstream)"
		echo "                 src/lib/libdylink.js: $(file_applied "$JS_FILE" && echo patched || echo upstream)"
	else
		echo "0001-dylink-...: NOT APPLIED"
		if verify_pins 2>/dev/null; then
			echo "                 (upstream text matches the pins; --apply will work)"
		else
			echo "                 (upstream text does NOT match the pins -- --apply will refuse)"
		fi
	fi
	echo
	echo "Only required for: platform=web threads=yes dlink_enabled=yes"
	echo "Verify by loading a real export; there is no standalone repro (see repro/README.md)."
}

case "${1:---status}" in
	--check)  is_applied && exit 0 || exit 1 ;;
	--apply)  do_apply ;;
	--revert) do_revert ;;
	--status) do_status ;;
	*) echo "usage: $0 [--status|--check|--apply|--revert]" >&2; exit 2 ;;
esac
