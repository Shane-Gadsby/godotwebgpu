# Braced GDScript in a web export

`fixture_project/` is written in braced GDScript (`.gdb`): its main scene, an autoload, a `class_name` class that
extends an indented `.gd` one, and a `.gd` script that extends it back by path. `main.gdb` checks each of those and
the braced-only syntax (arrow lambdas, `c ? a : b`, `else if`, `match` blocks, property accessors), and prints
`[GDB-CHECK] PASS` or `[GDB-CHECK] FAIL: <what>`; the disc it draws turns green or red.

- In the web scene smoketest (`scene_smoketest/scenes.json`, id `braced_gdscript`), it is exported with binary
  tokens, so the scripts ship as `.gdbc`, and run in each browser. It needs web templates built from this tree.
- `tests/braced_gdscript/run.sh` runs it natively: `godot --headless --path fixture_project -- --quit-on-check`.
