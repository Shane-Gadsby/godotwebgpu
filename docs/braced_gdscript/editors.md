# Editors

## Godot editor

`.gdb` scripts open in the script editor like `.gd` ones, with everything that works there: highlighting, completion,
go to definition, documentation, warnings, breakpoints and the debugger. What changes for `.gdb`:

- **Typing.** Enter after `{` indents the new line; typing `}` on a line of its own lines it up with its opener.
  Toggle Comment (Ctrl+K) uses `//`.
- **Reformat Code** (Edit menu, Ctrl+Alt+L) formats the selected lines, or the whole file; Editor Settings → Text
  Editor → Behavior → Files → Reformat Code On Save does it on every save. See [Formatting](formatting.md).
  **Auto Indent** (Edit → Indentation) re-indents the selection from its braces.
- **New scripts.** The Create Script dialog accepts `.gdb` (pick the extension in the path field or the file
  dialog's filter). Project Settings → Editor → Script → Braced GDScript By Default makes `.gdb` the default for new
  scripts. Templates, including your own in `res://script_templates/`, are converted to braces when used.
- **Converting.** In the FileSystem dock, right-click scripts or folders: Convert to Braced GDScript (.gdb) or
  Convert to GDScript (.gd). Scripts with unsaved changes are saved first. Each file is renamed through the dock, so
  scenes, resources and project settings that use it follow, and `preload`, `load` and `extends` paths in other
  scripts are rewritten, keeping the form they were written in (`res://` or relative). A file is skipped, and listed
  in a report, if the target name exists or the converter refuses it (see [FAQ](faq.md#the-converter-refused-a-file)).

## JetBrains Rider

The plugin is in its own repository, `godot-gdb-rider`, next to the engine checkout. It needs Rider 2026.2 or newer
with Rider's bundled Godot support enabled (it is by default).

### Install

1. Build it: `./gradlew buildPlugin` (JDK 21, and a local Rider: `riderPath` in its `gradle.properties`). The
   plugin is `build/distributions/godot-gdb-rider-<version>.zip`. The engine's `./build-linux.sh` builds it too and
   copies it to `builds/<version>/editor_plugins/` (`./build-linux.sh --plugins-only` for just the plugins).
2. Rider → Settings → Plugins → ⚙ → Install Plugin from Disk…, choose the zip, restart Rider.
3. Settings → Tools → Braced GDScript: set the Godot executable to this fork's editor build. Empty uses the
   `GODOT_GDB` environment variable, then `godot` on the `PATH`.
4. Optional: Settings → Tools → Actions on Save → Reformat code, to format on save.

### What you get

| Feature | Source | Needs |
| --- | --- | --- |
| Highlighting, brace matching, folding, commenting, smart Enter and `}`, live templates (`func`, `if`, `ife`, `for`, `while`, `match`, `lambda`, `prop`, `class`) | the plugin | nothing |
| Completion, go to declaration, Find Usages, rename, structure view, parameter info, inlay hints, Rider's GdScript inspections | Rider's own GdScript support, which the plugin feeds `.gdb` files to | nothing (works offline) |
| `.gd` and `.gdb` files seeing each other (`class_name`, `extends`, `preload`) | the same | nothing |
| Code Rider generates comes out braced: override completion (`func _ready() -> void:`), Create method, Add match branches, pasted `.gd` code | the plugin converts each indented header an edit inserts | nothing |
| Inspections and quick-fixes: `{` on the line after its header, a `:` block header, unbalanced brackets | the plugin | nothing |
| Intentions: `elif` ⇄ `else if`, `c ? a : b` ⇄ `a if c else b`, `func` ⇄ `=>` lambdas, Convert file to `.gd`/`.gdb` (paths in other files and the `.uid` file follow) | the plugin; converting uses the engine | the Godot executable, for converting |
| Postfix templates (`.if`, `.for`, `.while`, `.match`, `.not`, `.var`, `.return`, `.await`, `.print`, `.par`), Surround With `if`/`for`/`while` | the plugin | nothing |
| Quick Documentation from `///`, `##` and `/** */`, breadcrumbs and sticky lines, TODO items in `//` comments | the plugin, on top of Rider's | nothing |
| Reformat Code on the file or a selection, Reformat File, format on save (including changed lines only) | the engine's formatter, kept running as a format server | the Godot executable (step 3) |
| Engine diagnostics and hover | Godot's language server (its completion, navigation and the like are turned off for `.gdb`, since Rider's are there) | a running Godot editor with the project open (Editor Settings → Network → Language Server, port 6005 by default) |
| A banner when the Godot executable speaks another version of the braced syntax than the plugin | the plugin | the Godot executable |

Indentation settings live in Settings → Editor → Code Style → GDScript (braced) and are passed to the engine
formatter; everything else comes from `.editorconfig`.

### Known limits

- Rider's own analysis reads `c ? a : b` as if it were `c if a else b` (the same operands in other roles). Navigation
  and completion inside a ternary work; a type Rider infers for the whole ternary may be the condition's.
- An arrow lambda whose body is a call to a function that returns nothing (`() => print("x")`) is read as a
  `return` of that call, so Rider may hint about returning a void value. The engine accepts it.

## VS Code

The extension is in its own repository, `godot-gdb-vscode`. It provides highlighting, comments, bracket matching,
indentation on `{` and `}`, Format Document, Format Selection and format-on-save through the engine (kept running as a
format server), and syntax errors as you type.

1. Package it: `npx @vscode/vsce package --no-dependencies --allow-missing-repository`, or take
   `builds/<version>/editor_plugins/godot-gdb-<version>.vsix` from the engine's `./build-linux.sh`.
2. Extensions → ⋯ → Install from VSIX…
3. Settings: `gdscriptBraced.godotPath` (empty uses `GODOT_GDB`, then `godot`), `gdscriptBraced.checkSyntax`.

For completion and navigation, point an LSP client (for example the godot-tools extension) at Godot's language server;
it accepts `.gdb` documents with the language id `gdscript-braced` or `gdb`.

## Other editors

Anything with an LSP client can use Godot's language server for `.gdb` files (language id `gdscript-braced` or `gdb`),
including formatting through `textDocument/formatting`. Without one, run the formatter on save:
`godot --headless --gdscript-format --stdin --stdin-path "$FILE"`.

Editors that highlight, fold and select with tree-sitter (Neovim, Helix, Zed) can use the grammar in
`godot-gdb-tree-sitter`, with its `highlights.scm` and `folds.scm` queries. Its README has the Neovim
(nvim-treesitter) and Helix setup, including Helix's formatter line. The grammar is written for syntax version 1
(see [Formatting](formatting.md#syntax-version)) and has no engine connection of its own.

## Keeping the plugins in step with the engine

The plugins do not link against the engine, so an editor and a plugin from different builds work together as long as
they agree on the syntax version, which changes only when the language does. The Rider plugin asks the engine and shows
a banner on `.gdb` files when they differ; the VS Code extension and tree-sitter grammar do not, so rebuild them with the engine after a
version change. Formatting itself always comes from the engine you point the plugin at, so it follows the engine's
rules even when the plugin is older.
