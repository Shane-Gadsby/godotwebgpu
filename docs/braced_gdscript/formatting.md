# Formatting and style

One formatter, in the engine, formats both `.gd` and `.gdb` files. The script editor's Reformat Code, the command line,
the language server, and the Rider and VS Code plugins all run it, so every tool produces the same text.

## What it does

Given this:

```gdscript
var table={"a":1,"b":[1,2,3]}
func _ready()->void{
print("hi",speed)
      if speed>5 and not is_inside_tree(){
  print( "fast" )
}
else{
print("slow")}
for i in range(3){if i==1{continue}
print(i)}
}
```

it writes this (the `js` preset, with spaces):

```gdscript
var table = { "a": 1, "b": [1, 2, 3] }

func _ready() -> void {
    print("hi", speed)
    if speed > 5 && !is_inside_tree() {
        print("fast")
    } else {
        print("slow")
    }
    for i in range(3) {
        if i == 1 { continue }
        print(i)
    }
}
```

- **Indentation** of `.gdb` files comes from the braces: one level per block, plus a configurable number of levels
  inside `( )`, `[ ]` and dictionaries when they span lines. In `.gd` files indentation is the block structure, so
  the formatter keeps it and only converts between tabs and spaces.
- **Spacing**: around binary operators and `=`, after commas and `:` in type hints and dictionaries, none inside
  `( )` and `[ ]`, and inside `{ }` dictionaries as the preset says. `->`, `:=` and unary operators are spaced as in
  the GDScript style guide.
- **Braces**: `{` stays on the header line; `} else {` joins. Short blocks written on one line (`if i == 1 { continue }`,
  `func f() -> int { return 1 }`) stay on one line; anything else gets one statement per line.
- **Blank lines**: a configurable number around functions and classes, runs elsewhere reduced to a maximum.
- **Spellings**, when the style asks: `and`/`&&`, `or`/`||`, `not`/`!`, `#`/`//` comments and `elif`/`else if`.
- **Line ends**: trailing whitespace removed, a final newline added, line endings as `.editorconfig` says.

It never rebuilds code from the parse tree, so it cannot drop or reorder anything. Every result is checked before it
is used: the output must contain the same tokens as the input (with the spellings above counted as equal) and must
still parse. If either check fails the file is left unchanged and an "internal formatter error" is reported. Please
report those, with the file.

It does not wrap long lines (`max_line_length` is read but not enforced yet) and does not reorder members.

## Presets

| Setting | `js` (default for `.gdb`) | `godot` (default for `.gd`) |
| --- | --- | --- |
| Indentation | 4 spaces | tabs |
| Continuation inside `( )` | +1 level | +2 levels (style guide) |
| Continuation inside `[ ]`, `{ }` | +1 level | +1 level |
| Dictionaries | `{ "a": 1 }` | `{"a": 1}` |
| Logical operators | `&&`, `\|\|`, `!` | `and`, `or`, `not` |
| Comments | `//`, `///` | as written |
| `elif` | `else if` | `elif` |
| Blank lines around functions and classes | 1 | 2 |
| Blank lines elsewhere, at most | 1 | 1 |

`godot` follows the official [GDScript style guide](https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_styleguide.html).
Use it for `.gdb` files too if your team reads both kinds of file.

## `.editorconfig`

The formatter reads [`.editorconfig`](https://editorconfig.org) files from the script's folder upwards, to the first
one with `root = true`. Settings there override the preset. Put one at the project root:

```ini
root = true

[*.{gd,gdb}]
indent_style = space            # tab | space
indent_size = 4
end_of_line = lf
insert_final_newline = true

[*.gdb]
gdscript_preset = js                    # godot | js
gdscript_operator_style = symbols       # keep | words | symbols
gdscript_comment_style = slashes        # keep | hash | slashes
gdscript_elif_style = else_if           # keep | elif | else_if
gdscript_spaces_in_braces = true        # { "a": 1 } rather than {"a": 1}
gdscript_paren_continuation = 1         # extra levels inside ( )
gdscript_bracket_continuation = 1       # extra levels inside [ ] and { } literals
gdscript_blank_lines_around_members = 1
gdscript_max_blank_lines = 1
```

Standard keys: `indent_style`, `indent_size`, `tab_width`, `end_of_line`, `insert_final_newline`, `max_line_length`.
`slashes` and `else_if` are braced spellings; in `.gd` files they mean "keep". `keep` leaves each occurrence as
written.

## Running it

| Where | How |
| --- | --- |
| Godot script editor | Edit → Reformat Code, **Ctrl+Alt+L** (Cmd+Alt+L on macOS). Rebind it in Editor Settings → Shortcuts → Script Text Editor → Reformat Code. One undo step. Edit → Indentation → Auto Indent re-indents a `.gdb` selection from its braces without the rest. |
| Rider | Reformat Code (Ctrl+Alt+L), Reformat File, and Settings → Tools → Actions on Save → Reformat code |
| VS Code | Format Document (Shift+Alt+F), and `"editor.formatOnSave": true` |
| Any editor speaking LSP | `textDocument/formatting` on Godot's language server |
| Terminal | `godot --headless --gdscript-format <files or folders>` |

```sh
godot --headless --gdscript-format scripts/                 # format in place
godot --headless --gdscript-format --check scripts/         # report only; exit status 1 if anything would change
godot --headless --gdscript-format --preset godot a.gdb     # ignore the preset in .editorconfig
godot --headless --gdscript-format --indent-style space --indent-size 2 a.gdb
godot --headless --gdscript-format --stdin --stdin-path player.gdb < player.gdb > formatted.gdb
```

`--stdin-path` names the file the text belongs to: its extension picks `.gd` or `.gdb` rules and its folder picks the
`.editorconfig`. `--indent-style` and `--indent-size` override `.editorconfig`, which is how editors pass their own
indentation setting. The exit status is 1 when a file has an error, or with `--check` when one would change.

## In CI

```yaml
- name: GDScript formatting
  run: godot --headless --gdscript-format --check .
```

A pre-commit hook, with [pre-commit](https://pre-commit.com):

```yaml
- repo: local
  hooks:
    - id: gdscript-format
      name: gdscript-format
      entry: godot --headless --gdscript-format
      language: system
      files: \.(gd|gdb)$
```
