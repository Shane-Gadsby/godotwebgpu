# Migration and FAQ

## Converting a project

1. Commit first, so the conversion is one reviewable change.
2. In the FileSystem dock, select `res://` (or the folders you want), right-click → Convert to Braced GDScript (.gdb).
   Scenes, resources, autoloads and paths in other scripts are updated. The report lists anything it skipped.
3. Add an `.editorconfig` with your style (see [Formatting](formatting.md)), then format everything once:
   `godot --headless --gdscript-format .`
4. Run the project and its tests. A converted script compiles to exactly the code it came from; the converter checks
   that before it writes anything.

From a terminal, `godot --headless --gdscript-convert <to-gdb|to-gd> <files or folders>` writes each converted
file next to its original. `--replace` removes the originals (and moves their `.uid` files), `--keep-lines` keeps every
statement on the line it was on (handy for reviewing a diff), and `--stdin [--stdin-path <path>]` converts standard
input to standard output. Unlike the editor action, it does not rewrite paths in scenes, resources or other scripts.

You can convert a few scripts at a time; `.gd` and `.gdb` mix freely. To go back, Convert to GDScript (.gd) writes
style-guide GDScript and turns every braced-only spelling into its `.gd` form (`c ? a : b` becomes `a if c else b`,
`(x) => x * 2` becomes `func(x): return x * 2`, `//` becomes `#`, `/** */` becomes `##` lines).

## Questions

### Is this a new language?

No. A `.gdb` file is loaded by the same GDScript implementation as a `.gd` file; only the tokenizer reads it
differently. Types, the analyzer, warnings, the bytecode and the debugger are shared, and so is every limitation.

### Does it change performance?

No. Both compile to the same bytecode. Exports in binary-token mode store `.gdb` scripts as `.gdbc`, the same format
as `.gdc`.

### What about tools that read GDScript?

Tools that read `.gd` files themselves (gdtoolkit's gdlint and gdformat, GitHub's syntax highlighting, the official
VS Code extension's grammar, documentation generators that parse source) do not know `.gdb`; editors built on
tree-sitter can use the `godot-gdb-tree-sitter` grammar. Tools that go through
the engine (the language server, `--doctool`, the editor's help) work. On GitHub, a `.gitattributes` line
`*.gdb linguist-language=GDScript` gets most of the highlighting right.

### Can a stock Godot build open my project?

Not the `.gdb` scripts: they need this fork's engine. Exported games are unaffected; they include the engine they
were exported with. To share a project with someone on stock Godot, convert it back to `.gd` (one action, lossless).

### The converter refused a file

The converter checks that its output compiles to the same thing as its input, and leaves a file alone when it cannot
prove that, with "the converted code would not mean the same thing". In practice this happens only for files that
do not compile in the first place (for example a lambda with no body). Fix the error and convert again.

### Why is `{` on its own line an error?

Because braces also make dictionaries. `{` after a header (`if x {`) can only be a block, but `{` at the start of a
line is where a dictionary statement or match pattern starts, so the two styles cannot both be supported without
guessing. The error says where to move it.

### Why not real JavaScript or TypeScript?

That was assessed before braced GDScript was built, as a `.gdj` language: ES6+ written in Godot scripts without
embedding a JavaScript engine. It cannot work as asked. Whatever runs ES6+ correctly is a JavaScript engine, however it
is packaged, and lowering JavaScript syntax onto GDScript's semantics gives code that looks like JavaScript but behaves
like GDScript wherever the two differ (`5 / 2` is `2`, closures capture by value, there are no exceptions,
`==` does not convert types), so JavaScript habits compile and then misbehave. Braced GDScript takes every
JavaScript spelling that means exactly what a GDScript construct means and stops there. For real JavaScript or
TypeScript (npm packages, existing code), embed an engine through an existing module such as
[GodotJS](https://github.com/godotjs/GodotJS) instead; it and `.gdb` could live in one project.

### Why `else if` and `elif`, `&&` and `and`, `//` and `#`?

So existing GDScript pastes into a `.gdb` file unchanged apart from the blocks, and the formatter picks one spelling
for you (`gdscript_*_style` keys). The converter maps them back for `.gd`.

## What was left out

Each of these was considered and rejected. Only spellings that mean exactly what an existing GDScript construct means
were added.

| Spelling | Why not |
| --- | --- |
| `let`, `function`, `this` | A second name for `var`, `func` and `self`. `let` and `this` are valid identifiers in `.gd`, so converting would have to rename them. |
| `?.`, `??` | New semantics (analyzer and VM work). Desugaring would evaluate the left side twice. |
| `===`, `!==` | GDScript has no loose equality for them to differ from; they would suggest one. |
| `++`, `--` | Statement or expression semantics would have to be invented. `+= 1` exists. |
| `for (init; cond; step)` | No matching construct; it would need a `while` rewrite that changes how `continue` behaves. |
| `switch` / `case` | `match` does not fall through. The same keywords with different behavior would mislead. |
| Template strings `` `${x}` `` | The tokenizer would have to produce nested expressions. A later version could turn them into `str()` calls. |
| `class X extends Y { }` around a whole file | A script file is the class; there is no header to put it on. Inner classes already read this way. |
