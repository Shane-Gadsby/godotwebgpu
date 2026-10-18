#!/usr/bin/env bash
# Braced GDScript (.gdb) regression suite: the checks the unit tests cannot make on their own, run against an editor
# build. Exit status is non-zero if any check fails. See README.md for what each check proves.
#
#   tests/braced_gdscript/run.sh                  # everything (~10 min)
#   tests/braced_gdscript/run.sh --quick          # unit tests, twins, formatter corpus, property test (~2 min)
#   tests/braced_gdscript/run.sh --install-twins  # also writes the twins' agreed .out files into the test tree
#
# EDITOR_BIN overrides the editor (default: the newest bin/godot.linuxbsd.editor*). KEEP_WORK=1 keeps the work
# directory, whose path is printed at the start, for looking at the diffs.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HERE="$ROOT/tests/braced_gdscript"
SCRIPTS="$ROOT/modules/gdscript/tests/scripts"
BRACES="$SCRIPTS/braces"
G="${EDITOR_BIN:-$(ls -t "$ROOT"/bin/godot.linuxbsd.editor* 2>/dev/null | head -1)}"
[ -x "$G" ] || { echo "no editor binary; build one or set EDITOR_BIN"; exit 2; }
QUICK=0
INSTALL=0
for arg in "$@"; do
	case "$arg" in
		--quick) QUICK=1 ;;
		--install-twins) INSTALL=1 ;;
		*) echo "unknown option $arg"; exit 2 ;;
	esac
done
WORK="$(mktemp -d)"
[ "${KEEP_WORK:-0}" = 1 ] || trap 'rm -rf "$WORK"' EXIT
echo "editor: $G"
echo "work:   $WORK"

fail=0
check() { # got, expected, what
	if [ "$1" != "$2" ]; then
		echo "  FAIL: $3 (got '$1', expected '$2')"
		fail=1
	else
		echo "  ok: $3"
	fi
}

# Known differences, each with its reason. A change in either direction fails, so a fix updates this table too.
# Upstream's own --use-binary-tokens failures (they fail on a .gd-only engine too).
UPSTREAM_BINARY_FAILED_ASSERTIONS=8
# The upstream parser reports an error in a `match` body one line later than a braced one can.
KNOWN_CONVERTED_DIFFS="analyzer/errors/match.out"
# Intentionally broken upstream tests the converter refuses to convert (its round-trip check catches the change of
# meaning), leaving the file as it was. `abstract_methods.gd` ends with a lambda that has no body.
KNOWN_CONVERTER_REFUSALS="analyzer/errors/abstract_methods.gd"

# Line numbers move when a file is converted with layout changes; the text of each message must not.
norm_lines() { sed -E 's/\.gdb/.gd/g; s/line [0-9]+/line N/g; s/\.gd:[0-9]+/.gd:N/g' "$1"; }
norm_ext() { sed 's/\.gdb/.gd/g' "$1"; }

generate() { # dir [--use-binary-tokens]
	(cd "$ROOT" && timeout 1500 "$G" --headless --test gdscript-generate-tests "$@" > "$1.log" 2>&1)
}

# Rewrites `name.gd"` to `name.gdb"` in path strings for every name in the tree that is now a .gdb, as the editor's
# conversion action does through update_path_strings().
update_paths() { # dir to_ext(gd|gdb)
	python3 -I - "$1" "$2" <<'PY'
import os, re, sys
root, to = sys.argv[1], sys.argv[2]
names = {f[: -len(".gdb")] for _, _, fs in os.walk(root) for f in fs if f.endswith(".gdb")}
for d, _, fs in os.walk(root):
    for f in fs:
        if not f.endswith((".gd", ".gdb")):
            continue
        p = os.path.join(d, f)
        s = open(p, encoding="utf-8").read()
        if to == "gdb":
            t = re.sub(r'([A-Za-z0-9_.\-]+)\.gd(["\'])', lambda m: m.group(1) + (".gdb" if m.group(1).split("/")[-1] in names else ".gd") + m.group(2), s)
        else:
            t = re.sub(r'([A-Za-z0-9_.\-]+)\.gdb(["\'])', r"\1.gd\2", s)
        if t != s:
            open(p, "w", encoding="utf-8").write(t)
PY
}

compare_outs() { # generated_dir reference_dir normalizer -> prints differing .out paths
	local out
	(cd "$1" && find . -name "*.out" -not -path "./lsp/*" -not -path "./completion/*" | sort) | while read -r out; do
		out="${out#./}"
		if ! diff -q <($3 "$1/$out") <($3 "$2/$out") > /dev/null 2>&1; then
			echo "$out"
			diff <($3 "$1/$out") <($3 "$2/$out") | head -6 >> "$1.diffs"
		fi
	done
}

echo "unit tests"
r=$("$G" --headless --test --test-suite="*GDScript*" 2>&1 | grep "assertions:" | grep -o "[0-9]* failed")
check "$r" "0 failed" "GDScript suite"
r=$("$G" --headless --test --test-suite="*GDScript*" --use-binary-tokens 2>&1 | grep "assertions:" | grep -o "[0-9]* failed")
check "$r" "$UPSTREAM_BINARY_FAILED_ASSERTIONS failed" "GDScript suite, binary tokens (upstream's own failures only)"
r=$("$G" --headless --test --test-case="*CodeEdit*,*Formatter*,*Converter*,*Braced docs*,*Script editor hooks*" 2>&1 | grep "assertions:" | grep -o "[0-9]* failed")
check "$r" "0 failed" "CodeEdit, formatter, converter, docs, editor hooks"

echo "twins: each braced feature test has a .gd twin that must print the same"
mkdir -p "$WORK/twins/gd" "$WORK/twins/gdb" "$WORK/twins/errors"
# Each side needs the helper scripts (`*.notest.*`) of both kinds.
cp "$BRACES"/features/*.gd "$BRACES"/features/*.notest.gdb "$WORK/twins/gd/"
cp "$BRACES"/features/*.gdb "$BRACES"/features/*.notest.gd "$WORK/twins/gdb/"
cp "$BRACES"/errors/*.gdb "$WORK/twins/errors/"
for d in gd gdb errors; do generate "$WORK/twins/$d"; done
differ=0
for f in "$WORK"/twins/gdb/*.out; do
	diff -q "$WORK/twins/gd/$(basename "$f")" "$f" > /dev/null || { differ=$((differ + 1)); echo "    differs: $(basename "$f")"; }
done
check "$differ" "0" "$(ls "$WORK"/twins/gdb/*.out | wc -l) twins agree"
if [ "$INSTALL" = 1 ] && [ "$differ" = 0 ]; then
	cp "$WORK"/twins/gdb/*.out "$BRACES/features/"
	cp "$WORK"/twins/errors/*.out "$BRACES/errors/"
	echo "  installed the twins' .out files"
fi

echo "command line for editor plugins"
check "$("$G" --headless --gdscript-braces-version 2> /dev/null)" "1" "--gdscript-braces-version (bump with GdbPsiLexer.GRAMMAR_VERSION in the Rider plugin)"
# Each braced feature test, converted through standard input to .gd and back, still converts and parses.
cfail=0
for src in "$BRACES"/features/*.gdb; do
	name=$(basename "$src")
	"$G" --headless --gdscript-convert to-gd --stdin --stdin-path "$src" < "$src" 2> /dev/null > "$WORK/stdin_${name%.gdb}.gd" || { cfail=$((cfail + 1)); echo "    to-gd: $name"; continue; }
	"$G" --headless --gdscript-convert to-gdb --stdin --stdin-path "$WORK/stdin_${name%.gdb}.gd" < "$WORK/stdin_${name%.gdb}.gd" > /dev/null 2>&1 || { cfail=$((cfail + 1)); echo "    to-gdb: $name"; }
done
check "$cfail" "0" "--gdscript-convert --stdin both ways"

echo "formatter on every GDScript test"
for preset in godot js; do
	rm -rf "$WORK/fmt" && cp -r "$SCRIPTS" "$WORK/fmt"
	"$G" --headless --gdscript-format --preset "$preset" "$WORK/fmt" > /dev/null 2> "$WORK/fmt.err"
	check "$(grep -c 'Internal formatter' "$WORK/fmt.err")" "0" "$preset: no internal errors"
	check "$("$G" --headless --gdscript-format --check --preset "$preset" "$WORK/fmt" 2> /dev/null | tail -1 | grep -o '[0-9]* would change')" "0 would change" "$preset: idempotent"
done

echo "property test: a mangled .gdb file formats back to the same text"
runs=0
pfail=0
mkdir -p "$WORK/prop"
for src in "$BRACES"/features/*.gdb "$HERE/messy.gdb"; do
	name=$(basename "$src")
	if ! "$G" --headless --gdscript-format --stdin --stdin-path "$name" < "$src" > "$WORK/prop/canon_$name" 2> /dev/null; then
		pfail=$((pfail + 1))
		echo "    cannot format $name"
		continue
	fi
	for seed in $(seq 1 "${SEEDS:-25}"); do
		runs=$((runs + 1))
		python3 -I "$HERE/perturb.py" "$src" "$WORK/prop/mangled_$name" "$seed"
		if ! "$G" --headless --gdscript-format --stdin --stdin-path "$name" < "$WORK/prop/mangled_$name" 2> /dev/null | cmp -s - "$WORK/prop/canon_$name"; then
			pfail=$((pfail + 1))
			cp "$WORK/prop/mangled_$name" "$WORK/prop/fail_${seed}_$name"
			echo "    $name, seed $seed"
		fi
	done
done
check "$pfail" "0" "$runs mangled files"

if [ "$QUICK" = 1 ]; then
	exit $fail
fi

echo "fuzzing: mangled .gd and .gdb files through the tokenizer, parser, formatter and converter"
for seed in 1 2; do
	r=$(GDB_FUZZ_SEED=$seed GDB_FUZZ_ITERATIONS=${FUZZ_ITERATIONS:-20000} GDB_FUZZ_DUMP="$WORK/fuzz_input.gdb" GDB_FUZZ_FAILURES="$WORK" \
		"$G" --headless --test --test-case="*mangled input*" 2>&1 | grep "assertions:" | grep -o "[0-9]* failed")
	check "${r:-crashed (input in $WORK/fuzz_input.gdb)}" "0 failed" "seed $seed, ${FUZZ_ITERATIONS:-20000} inputs (failures kept in the work directory)"
done

echo "braced feature tests converted to .gd"
mkdir -p "$WORK/b2g"
cp "$BRACES"/features/*.gdb "$WORK/b2g/"
cp "$BRACES"/features/*.notest.gd "$WORK/b2g/" 2> /dev/null
"$G" --headless --gdscript-convert to-gd --replace --preset godot "$WORK/b2g" > /dev/null 2> "$WORK/b2g.err"
update_paths "$WORK/b2g" gd
generate "$WORK/b2g"
check "$(compare_outs "$WORK/b2g" "$BRACES/features" norm_lines | tr '\n' ' ' | sed 's/ $//')" "" "they print what the .gdb files print"

converted_suite() { # what, convert flags, normalizer, expected differences, [generator flags]
	rm -rf "$WORK/cs" && cp -r "$SCRIPTS" "$WORK/cs" && rm -rf "$WORK/cs/braces"
	# shellcheck disable=SC2086 # Flags are words.
	"$G" --headless --gdscript-convert to-gdb $2 "$WORK/cs" > /dev/null 2> "$WORK/cs.err"
	check "$(grep 'Internal converter' "$WORK/cs.err" | sed "s#^$WORK/cs/##; s#:.*##" | sort | tr '\n' ' ' | sed 's/ $//')" "$KNOWN_CONVERTER_REFUSALS" "$1: the converter refuses only the known broken tests"
	# Tests run as .gdb; helpers stay .gd, so preload() paths to them still work. Tests that were .gdb to begin
	# with have no .gd to remove.
	find "$WORK/cs" -name "*.gdb" | while read -r f; do
		case "$f" in *.notest.gdb) rm "$f" ;; *) rm -f "${f%.gdb}.gd" ;; esac
	done
	update_paths "$WORK/cs" gdb
	find "$WORK/cs" -name "*.out" -delete
	generate "$WORK/cs" ${5:-}
	check "$(compare_outs "$WORK/cs" "$SCRIPTS" "$3" | tr '\n' ' ' | sed 's/ $//')" "$4" "$1"
}

echo "every GDScript test converted to .gdb"
converted_suite "lines kept" "--keep-lines" norm_ext "$KNOWN_CONVERTED_DIFFS"
# In binary-token mode, upstream's .gd tests already differ from their .out files in a few places (warnings the
# binary format cannot reproduce). Those are measured here, not listed, and the converted tests may differ only there.
rm -rf "$WORK/upstream" && cp -r "$SCRIPTS" "$WORK/upstream" && rm -rf "$WORK/upstream/braces"
find "$WORK/upstream" -name "*.out" -delete
generate "$WORK/upstream" --use-binary-tokens
upstream_binary_diffs=$(compare_outs "$WORK/upstream" "$SCRIPTS" norm_ext)
echo "  upstream's own binary-token differences: $(echo "$upstream_binary_diffs" | grep -c .)"
expected_binary=$(printf '%s\n%s\n' "$upstream_binary_diffs" "${KNOWN_CONVERTED_DIFFS// /$'\n'}" | grep . | sort -u | tr '\n' ' ' | sed 's/ $//')
converted_suite "lines kept, binary tokens" "--keep-lines" norm_ext "$expected_binary" --use-binary-tokens
converted_suite "formatted (godot preset)" "--preset godot" norm_lines ""

echo "round trip: .gd -> .gdb -> .gd"
converted_suite "lines kept" "--keep-lines" norm_ext "$KNOWN_CONVERTED_DIFFS" > /dev/null
rm -rf "$WORK/back" && cp -r "$WORK/cs" "$WORK/back" && find "$WORK/back" -name "*.out" -delete
"$G" --headless --gdscript-convert to-gd --replace "$WORK/back" > /dev/null 2> "$WORK/back.err"
check "$(grep -c 'Internal converter' "$WORK/back.err")" "0" "no internal converter errors"
update_paths "$WORK/back" gd
generate "$WORK/back"
check "$(compare_outs "$WORK/back" "$SCRIPTS" norm_lines | tr '\n' ' ' | sed 's/ $//')" "" "every test prints what it printed before"

exit $fail
