# Braced GDScript internals

For people changing the engine side. The design record, with the decisions' history, is the implementation plan in
`webgpu_notes/` (main checkout, not in git); this page describes the code as it is.

## How a `.gdb` file is compiled

`.gdb` is not a second language. GDScript's resource loader recognizes it (`gdscript_resource_format.cpp`, along with
`.gdbc`), so it loads as a `GDScript` resource and goes through the same parser,
analyzer and compiler. `GDScriptLanguage::is_gdscript_path()` replaces every `get_extension() == "gd"` comparison in
the module; `GDScriptTokenizer::is_braced_path()` picks the tokenizer mode.

**Tokenizer translation (D2).** In brace mode (`GDScriptTokenizerText::set_brace_mode()`,
`gdscript_tokenizer_braces.cpp`) leading whitespace means nothing. The tokenizer emits the tokens the indentation
scanner would have produced:

| Source | Tokens the parser sees |
| --- | --- |
| block `{` | `COLON NEWLINE INDENT` |
| block `}` | `NEWLINE DEDENT` (`PASS NEWLINE DEDENT` for an empty block) |
| `else if` | `ELIF` |
| `x => ...`, `(a, b) => ...` | `FUNC ( params ) COLON`, then `RETURN` for an expression body |
| `//`, `/* */` | comments; `///` and `/** */` lines are stored as `##` doc lines |

Synthetic tokens carry the brace's line and column, so errors, warnings and breakpoints point at real source lines.
The parser is unchanged except for the hooks listed below.

**Block or dictionary (D3).** `_brace_open()` classifies each `{` from the previous significant token and keeps one
stack of `BLOCK`, `DICTIONARY` and `ENUM` entries with the paren depth each opened at. The table is in
[language.md](language.md#when--opens-a-block); `_brace_can_end_header()` is its "can end an expression" column.
The Rider plugin's `GdbPsiLexer` implements the same table and must change with it.

**Allman braces (D4)** are reported where the scanner meets the `{`, and the newline before it is dropped so the rest
of the file still parses.

**Parser hooks.** Ternary `c ? a : b` is `parse_question_mark()`, in the rule table at `PREC_TERNARY`, active only when
`braced_syntax` (from the script path) is set. Arrow bodies that call a void function are accepted by the analyzer's
`resolve_return()` for braced lambdas. The parser can record a block log (`set_block_log()`: each block's `:` and its
end), the `?` positions and the arrow returns; the converter uses those.

**Binary tokens (D8).** `GDScriptTokenizerBuffer::parse_code_string(..., p_braced)` encodes a `.gdb` file with
virtual columns (block depth + 1) and a forced line entry after each synthetic `NEWLINE`, so the buffer replays as
an indented script with the original line numbers. The encoder drives multiline mode from brackets, as the text
tokenizer does. Exported `.gdb` files are written as `.gdbc` (D9); `canonicalize_path()` maps them back.

**Editor behavior (D10).** `EditorLanguage` (`core/object/editor_language.h`) gained per-path hooks:
`get_comment_delimiters_for_path()`, `get_doc_comment_delimiters_for_path()`, `uses_brace_blocks()`,
`format_code_for_path()` (Auto Indent), `can_reformat_code()` / `reformat_code()` (Reformat Code),
`get_source_extensions()`, `get_new_script_extension()` and `adapt_template_for_path()`. `ScriptTextEditor`,
`ScriptCreateDialog`, `CodeEdit` (`auto_unindent_closing_brackets`) and the highlighter call them. The FileSystem
dock's convert actions are `editor/gdscript_braced_conversion.{h,cpp}`.

**Formatter (D12).** `GDScriptFormatter` (`gdscript_formatter.{h,cpp}`) is the only implementation of formatting
rules; the editor, `--gdscript-format`, the language server (`textDocument/formatting`) and the IDE plugins all call
it. It works on pieces: tokens, plus comments and blank lines recovered from the text between them. Every result is
checked: same tokens (with equivalent spellings counted equal) and a clean re-parse, or nothing changes.
Wrapping a line can make another line fit or not, so `format()` repeats until the text is stable (at most 100 passes),
which also keeps it idempotent. If the full result fails the check but the same file formats with trailing commas,
alignment and wrapping turned off, that result is used with a note naming the line. `format_ranges()` formats only
some lines (`--range`, editor selections), `unified_diff()` is `--diff`, and `run_server()` is
`--gdscript-format-server`. `GDSCRIPT_FORMAT_DEBUG=1` prints a rejected result and the first token where input and output part.

**Converter.** `GDScriptFormatter::convert()`. `.gd` → `.gdb` inserts braces from the parser's block log; `.gdb` →
`.gd` is the formatter in indented-target mode, with ternaries rewritten to `a if c else b` and void-call arrows to
block bodies after analysis. Both directions compare `GDScriptParser::TreePrinter::get_tree_text()` of input and
output and refuse on any difference.

## Touch points

Every change to upstream files carries a `// GDB` comment. `grep -rn "// GDB" --include=*.cpp --include=*.h .` lists
them: about 110 lines in 39 files, mostly in `modules/gdscript/` (tokenizer, parser, analyzer, resource format,
language server, tests), plus `main/main.cpp` (CLI), `editor/script/` (script editor, create dialog),
`editor/docks/filesystem_dock.*` (public `rename_file()`), `scene/gui/code_edit.*` and `core/object/editor_language.h`.
New files are `gdscript_tokenizer_braces.cpp`, `gdscript_formatter.{h,cpp}`, `editor/gdscript_braced_conversion.*`,
the tests below and this folder. `gdscript_braces=no` (`modules/gdscript/config.py`) builds without brace mode.

## Versions, builds and CI

`GDScriptFormatter::BRACES_GRAMMAR_VERSION` (1; `get_braces_grammar_version()` returns 0 without brace mode, printed
by `--gdscript-braces-version`) names the syntax. Bump it with any change to the block-or-dictionary table or to a
braced spelling, together with its mirrors: the Rider plugin's `GdbPsiLexer.GRAMMAR_VERSION` and the tree-sitter
grammar in `godot-gdb-tree-sitter`. The Rider plugin compares its version with the engine's and shows a banner when
they differ. Formatter-only changes do not bump it: the plugins format through the engine.

`build-linux.sh` passes `gdscript_braces` to every build of a run, so its editor and templates always agree
(`--no-gdscript-braces` for the standard ones), and ends by building the Rider plugin and the VS Code `.vsix` into
`builds/<version>/editor_plugins/` (`--skip-editor-plugins`; `--plugins-only` for just those; a plugin failure only
warns).

CI (`.github/workflows/linux_builds.yml`) runs `tests/braced_gdscript/run.sh --quick` on the Linux editor build, and
builds the ThreadSanitizer editor with `gdscript_braces=no` so a build without brace mode keeps compiling and passing
its tests. Each plugin repository has its own workflow: the Rider plugin builds against a downloaded Rider and runs the
Plugin Verifier and its tests, the VS Code extension runs its grammar and formatter tests and keeps the `.vsix`, and
the tree-sitter grammar checks that its committed parser is generated from `grammar.js` and runs its corpus.

## Tests

| What | Where | Run |
| --- | --- | --- |
| Braced language features | `modules/gdscript/tests/scripts/braces/features/`: each `x.gdb` has an `x.gd` twin and one shared `x.out`; `*.notest.*` are helpers | `--test --test-suite="*GDScript*"` |
| Brace errors | `braces/errors/` (`.textonly.gdb`: text mode only, binary tokens report it differently) | same |
| Error line numbers at run time | `braces/runtime/` | same |
| Language server | `lsp/braced.gdb`, `test_lsp.h` `[braced]` | `--test --test-case="*[braced]*"` |
| Formatter, converter, docs, editor hooks | `tests/test_gdscript_formatter.h` | `--test --test-case="*Formatter*,*Converter*,*Braced docs*,*Script editor hooks*"` |
| `CodeEdit` unindent | `tests/scene/test_code_edit.cpp` | `--test --test-case="*CodeEdit*"` |
| Fuzzing (tokenizer, parser, formatter, converter on mangled files) | `tests/test_gdscript_braces_fuzz.h` | `--test --test-case="*mangled input*"`, `GDB_FUZZ_ITERATIONS`/`GDB_FUZZ_SEED`; `GDB_FUZZ_DUMP=<file>` writes each input before it runs (a crash leaves it behind), `GDB_FUZZ_FAILURES=<folder>` keeps failing inputs |
| A braced project, natively and in a web export (`.gdbc`) | `webgpu_tests/braced_gdscript/` | `run.sh`, and the web scene smoketest |
| Whole-tree checks | `tests/braced_gdscript/` | `tests/braced_gdscript/run.sh` ([README](../../tests/braced_gdscript/README.md)) |

The test runner treats `.gdb` like `.gd` (`gdscript_test_runner.cpp`), and `--test gdscript-generate-tests` writes
`.out` files for both.

### Adding a construct

1. Write `braces/features/name.gdb` and its indented twin `name.gd`, which must print the same.
2. `tests/braced_gdscript/run.sh --quick --install-twins` checks they agree and writes `name.out`.
3. If the construct is new syntax, add it to the formatter's token-equality rules, to both converter directions, to
   [language.md](language.md), and to the Rider plugin's `GdbPsiLexer` (its conformance test parses the twins).
4. Run `tests/braced_gdscript/run.sh` in full, and build with `dev_mode=yes` as CI does.

## Porting to a new upstream release

1. Merge. Conflicts concentrate in `gdscript_tokenizer.cpp`, `gdscript_parser.cpp` and
   `gdscript_tokenizer_buffer.cpp`; `git rerere` replays repeated resolutions.
2. Re-check every hook: `grep -rn "// GDB"`, and grep the module for new `get_extension() == "gd"` comparisons, which
   must become `is_gdscript_path()`.
3. Run `tests/braced_gdscript/run.sh`. New upstream test scripts are converted to `.gdb` automatically, so new syntax
   that braces get wrong shows up there first.
4. Watch upstream for: new block constructs (they must work through D2, and the D3 table may need a row), changes to
   `GDScriptTokenizerBuffer`'s format (D8), and language-server formatting work.
5. Rebuild the Rider plugin against the new Rider and run its Plugin Verifier and conformance tests: it depends on
   internals of Rider's GdScript plugin, which is the most fragile link.
