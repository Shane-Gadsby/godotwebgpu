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
- **Long lines** (over `max_line_length`, 100 by default) are wrapped inside their brackets: the last outermost list
  on the line (arguments, parameters, an array, a dictionary) gets one item per line, with the continuation indent,
  until every line fits or nothing more can be wrapped. Subscripts and grouping parentheses are left alone, and in
  `.gd` files so is a list that holds a lambda, whose body is indentation-sensitive.
- **Trailing commas** after the last item of a list whose closing bracket is on its own line: arrays, dictionaries
  and enums with the `godot` preset (as the style guide asks), every list with `js`.
- **Alignment**, off unless you ask: the `=` of consecutive `var`/`const` lines, trailing comments, dictionary values,
  and the bodies of one-line `match` branches.
- **Spellings**, when the style asks: `and`/`&&`, `or`/`||`, `not`/`!`, `#`/`//` comments and `elif`/`else if`.
- **Line ends**: trailing whitespace removed, a final newline added, line endings as `.editorconfig` says.

It never rebuilds code from the parse tree, so it cannot drop or reorder anything. Every result is checked before it
is used: the output must contain the same tokens as the input (with the spellings above counted as equal) and must
still parse. If either check fails the file is left unchanged and an "internal formatter error" is reported. Please
report those, with the file; `GDSCRIPT_FORMAT_DEBUG=1` prints the rejected result and where it went wrong. If only
the trailing-comma, alignment or wrapping rules caused it, the file is formatted without them instead, with a note.

It does not reorder members, and a line that cannot be wrapped inside brackets (a long string, a long chain of
operators) stays long.

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
| Trailing commas | every list | arrays, dictionaries, enums |
| Line length | 100 | 100 |

`godot` follows the official [GDScript style guide](https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_styleguide.html).
Use it for `.gdb` files too if your team reads both kinds of file.

## `.editorconfig`

The formatter reads [`.editorconfig`](https://editorconfig.org) files from the script's folder upwards, to the first
one with `root = true`. Settings there override the preset. Comments go on their own lines: as in the EditorConfig format, a `#` after a
value is part of the value, and the setting is then ignored. Put one at the project root:

```ini
root = true

[*.{gd,gdb}]
# tab | space
indent_style = space
indent_size = 4
end_of_line = lf
insert_final_newline = true

[*.gdb]
# godot | js
gdscript_preset = js
# keep | words | symbols
gdscript_operator_style = symbols
# keep | hash | slashes
gdscript_comment_style = slashes
# keep | elif | else_if
gdscript_elif_style = else_if
# { "a": 1 } rather than {"a": 1}
gdscript_spaces_in_braces = true
# extra levels inside ( )
gdscript_paren_continuation = 1
# extra levels inside [ ] and { } literals
gdscript_bracket_continuation = 1
gdscript_blank_lines_around_members = 1
gdscript_max_blank_lines = 1
# keep | literals | all
gdscript_trailing_commas = all
# line up the = of consecutive var/const lines
gdscript_align_assignments = false
# line up trailing comments
gdscript_align_comments = false
gdscript_align_dictionary_values = false
# line up one-line match branch bodies
gdscript_align_match_branches = false
# off: never wrap
max_line_length = 100
```

Standard keys: `indent_style`, `indent_size`, `tab_width`, `end_of_line`, `insert_final_newline`, `max_line_length`.
`slashes` and `else_if` are braced spellings; in `.gd` files they mean "keep". `keep` leaves each occurrence as
written.

## Running it

| Where | How |
| --- | --- |
| Godot script editor | Edit → Reformat Code, **Ctrl+Alt+L** (Cmd+Alt+L on macOS): the selected lines when there is a selection, else the file. Rebind it in Editor Settings → Shortcuts → Script Text Editor → Reformat Code. One undo step. Editor Settings → Text Editor → Behavior → Files → Reformat Code On Save formats on save. Edit → Indentation → Auto Indent re-indents a `.gdb` selection from its braces without the rest. |
| Rider | Reformat Code (Ctrl+Alt+L) on the file or a selection, Reformat File, and Settings → Tools → Actions on Save → Reformat code (including "changed lines only") |
| VS Code | Format Document (Shift+Alt+F), Format Selection, and `"editor.formatOnSave": true` |
| Any editor speaking LSP | `textDocument/formatting` on Godot's language server |
| Terminal | `godot --headless --gdscript-format <files or folders>` |
| An editor plugin | `godot --headless --gdscript-format-server`, which stays running (below) |

```sh
godot --headless --gdscript-format scripts/                 # format in place
godot --headless --gdscript-format --check scripts/         # report only; exit status 1 if anything would change
godot --headless --gdscript-format --preset godot a.gdb     # ignore the preset in .editorconfig
godot --headless --gdscript-format --indent-style space --indent-size 2 a.gdb
godot --headless --gdscript-format --check --diff scripts/  # and show what would change, as a unified diff
godot --headless --gdscript-format --stdin --stdin-path player.gdb < player.gdb > formatted.gdb
godot --headless --gdscript-format --stdin --stdin-path player.gdb --range 10:24 < player.gdb   # only lines 10-24
```

`--stdin-path` names the file the text belongs to: its extension picks `.gd` or `.gdb` rules and its folder picks the
`.editorconfig`; without it, standard input is formatted with `.gd` rules. `--indent-style` and `--indent-size` override `.editorconfig`, which is how editors pass their own
indentation setting. `--range <first>:<last>` (repeatable) formats only those lines, as editors do for a selection,
and applies only with `--stdin` (files named on the command line are always formatted whole);
it refuses when that would leave a `.gd` file's indentation inconsistent. The exit status is 1 when a file has an
error, or with `--check` when one would change.

Each run starts the engine, about 0.3 s. Editor plugins instead keep `godot --headless --gdscript-format-server`
running and send it requests on standard input: each request and each response is a line holding the byte length of
a JSON object, then the object. A request is `{"id", "path", "text", "mode", "ranges", "indent_style", "indent_size",
"preset", "keep_lines"}`, all but `text` optional, with `mode` one of `format` (the default), `to-gd`, `to-gdb` (convert;
`keep_lines` as `--keep-lines`) and `version`. `ranges` is a list of `[first, last]` line pairs, as `--range`. The
answer is `{"id", "ok": true, "text"}` (plus `"note"` when the formatter fell back to formatting without trailing
commas, alignment or wrapping, as above) or `{"id", "ok": false, "line", "message"}`, in well under a millisecond; for
`version`, `text` is the syntax version below. The server exits when its input closes.

### Syntax version

`godot --headless --gdscript-braces-version` prints the version of the braced syntax the engine reads (`1` for
`gdb-1.0`; `0` for a build without braced GDScript). It changes only when what a `.gdb` file means changes: the rule
for when `{` opens a block, or a braced spelling. Editor plugins that read `.gdb` files themselves (the Rider plugin's
lexer, the tree-sitter grammar) are written for one version; the Rider plugin asks the engine and shows a banner when
they differ.

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
