# Braced GDScript regression suite

Braced GDScript (`.gdb`) is GDScript written with `{ }` blocks. The unit tests (`--test`) cover its tokenizer,
formatter, converter and editor hooks, and run every test script under `modules/gdscript/tests/scripts/braces/`.
`run.sh` adds the checks that need whole trees of scripts, written and read back by an editor build:

| Check | What it proves |
| --- | --- |
| Unit tests, text and binary tokens | The GDScript suite passes in both modes. Binary-token mode has 8 failing assertions that upstream has too (`UPSTREAM_BINARY_FAILED_ASSERTIONS`) |
| Twins | Each `braces/features/x.gdb` has an `x.gd` twin written with indentation. Both must print the same `.out`. `--install-twins` writes the agreed files into the test tree after you change a feature test |
| A project in braced GDScript | `webgpu_tests/braced_gdscript/fixture_project` (a `.gdb` main scene and autoload, `class_name` and `extends` across `.gd` and `.gdb`, Tier B syntax) prints `[GDB-CHECK] PASS`. The web scene smoketest exports the same project (binary tokens, so `.gdbc`) and runs it in browsers |
| Formatter on every GDScript test | Both presets (`godot`, `js`) format every test script in `modules/gdscript/tests/scripts` without an internal error, and a second pass changes nothing |
| Property test | Each braced feature test and `messy.gdb`, with its whitespace mangled by `perturb.py` (indentation, blank lines, trailing spaces, spaces near brackets; 25 seeds, `SEEDS` to change), formats back to the same text as the original |
| Braced tests converted to `.gd` | The braced feature tests, converted with `--gdscript-convert to-gd`, print what the `.gdb` files print |
| Every GDScript test converted to `.gdb` | Every upstream test, converted to `.gdb`, prints what it printed as `.gd`: with lines kept, with lines kept and binary tokens, and formatted with the `godot` preset |
| Round trip | Every test converted to `.gdb` and back to `.gd` prints what it printed before |
| Fuzzing | 2 × 20,000 mangled `.gd` and `.gdb` files (characters inserted and deleted, slices duplicated, lines swapped, brackets changed) go through the tokenizer, parser, formatter and converter without a crash, a hang, an internal formatter error or an unstable result. The unit tests run 300 of them; `FUZZ_ITERATIONS` changes the count here. `GDB_FUZZ_SEED`, `GDB_FUZZ_ITERATIONS`, `GDB_FUZZ_DUMP` and `GDB_FUZZ_FAILURES` drive the test case directly (`test_gdscript_braces_fuzz.h`) |

Known differences are listed at the top of `run.sh`, each with its reason. A known difference that goes away fails the
check too, so the table stays true.

```sh
tests/braced_gdscript/run.sh                  # everything, about ten minutes
tests/braced_gdscript/run.sh --quick          # unit tests, twins, formatter, property test: about two minutes
EDITOR_BIN=bin/godot.linuxbsd.editor.x86_64 tests/braced_gdscript/run.sh
KEEP_WORK=1 tests/braced_gdscript/run.sh      # keep the work directory (printed first) to read the diffs
```

It needs an editor built with `tests=yes`. CI runs `--quick` on the Linux editor build (`linux_builds.yml`).

The Rider plugin has a matching check of its own: its PSI conformance test parses the same converted suite with
Rider's GdScript parser (see the plugin's README).
