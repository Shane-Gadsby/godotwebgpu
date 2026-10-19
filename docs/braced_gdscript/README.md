# Braced GDScript (`.gdb`)

Braced GDScript is GDScript written with `{ }` blocks instead of indentation, in files named `*.gdb`. It is the same
language: the same keywords, types, classes, signals, annotations and runtime. Only the block syntax differs, plus a few
JavaScript-style spellings (`else if`, `//` comments, `c ? a : b`, `(x) => x * 2`, `/** */` docs). Indentation carries
no meaning, so you can write freely and let a formatter lay the code out for you.

```gdscript
extends CharacterBody2D

const SPEED := 300.0

/** Moves toward the mouse. Returns the distance left. */
func chase(delta: float) -> float {
    var to_mouse := get_global_mouse_position() - global_position
    if to_mouse.length() < 4.0 {
        return 0.0
    } else if Input.is_action_pressed("walk") {
        velocity = to_mouse.normalized() * SPEED * 0.5
    } else {
        velocity = to_mouse.normalized() * SPEED
    }
    move_and_slide()
    return to_mouse.length()
}
```

It needs this fork's engine, built with `gdscript_braces=yes` (the default). Stock Godot cannot load `.gdb` files.

## Documents

| Document | For |
| --- | --- |
| [Language](language.md) | Every construct, side by side with indented GDScript, and the exact rule for when `{` opens a block |
| [Formatting and style](formatting.md) | The formatter, its two presets, `.editorconfig` keys, the command line, CI recipes |
| [Editors](editors.md) | The Godot editor, JetBrains Rider and VS Code |
| [Migration and FAQ](faq.md) | Converting a project, mixing `.gd` and `.gdb`, tool caveats, what was left out and why |
| [Internals](internals.md) | How it works, where the code is, how to test and port it |

Editor support lives in three repositories next to this one: `godot-gdb-rider` (JetBrains Rider), `godot-gdb-vscode`
(VS Code) and `godot-gdb-tree-sitter` (a tree-sitter grammar for Neovim, Helix, Zed and other tree-sitter tools).

## Building

`./build-linux.sh` builds the editor, the Linux export templates and every web template with braced GDScript, then
the Rider plugin and the VS Code `.vsix` from the sibling repositories, into `builds/<version>/editor_plugins/`. A
plugin that cannot be built (no repository, no JDK or `npx`, a Gradle error) is skipped with a warning rather than
failing the engine build. Gradle runs on a JDK 21 the script finds (`JAVA_HOME`, `PATH`, `/usr/lib/jvm`, then
Rider's own), so a `JAVA_HOME` pinned to an older JDK does not break it. `--plugins-only` builds just the two
plugins, in seconds, without touching the engine build; `--skip-editor-plugins` skips them; `--no-gdscript-braces` builds the standard editor and
templates instead, and skips the plugins, which need a braced editor to format.

With plain SCons, braced GDScript is on by default (`gdscript_braces=yes`). Build the editor and the templates you
export with from the same setting: a braced editor exports `.gdb` scripts (as `.gdbc`), and a template built with
`gdscript_braces=no` cannot load them. Templates built before braced GDScript was merged (`gdb-1.0`) cannot either.

## Getting started

1. **Create a file.** In the editor's Create Script dialog, choose GDScript and change the extension to `.gdb`, or turn
   on Project Settings → Editor → Script → Braced GDScript By Default so new scripts are `.gdb`. The built-in
   templates come out braced. Any text editor works too; save as `name.gdb`.
2. **Or convert one.** Right-click scripts or folders in the FileSystem dock → Convert to Braced GDScript (.gdb).
   Scenes, resources, project settings and `preload`/`load`/`extends` paths in other scripts are updated to the new
   name. Convert to GDScript (.gd) goes back. From a terminal,
   `godot --headless --gdscript-convert to-gdb --replace some/folder/` converts the files (and moves their `.uid`
   files) but leaves references in other files alone; use the editor action for a project.
3. **Mix freely.** `.gd` and `.gdb` scripts extend, preload and call each other; a `class_name` in either is global.
   A scene does not care which kind its scripts are.
4. **Format.** Ctrl+Alt+L (Edit → Reformat Code) in the script editor, Reformat Code in Rider, or
   `godot --headless --gdscript-format path/`. Style comes from `.editorconfig`; see [Formatting](formatting.md).
5. **Export.** Nothing to do. Exports compile `.gdb` like `.gd` (to `.gdbc` in binary-token mode), and line numbers in
   errors and the debugger match your source.

## Rules in one minute

- A block is `{ ... }` with the `{` on the header's line: `if x {`, `func f() -> int {`, `class Inner extends Node {`,
  `match v {`, and each match branch `1, 2 {`. A block's `{` on the line after its header (Allman style) is an
  error that tells you to move it up.
- Newlines still end statements, as in GDScript; `;` separates statements on one line. Inside `( )`, `[ ]` and
  dictionaries, newlines are free, as before.
- `{}` is an empty block: no `pass` needed.
- Anything that is a dictionary in GDScript is still a dictionary: `var d = { "a": 1 }`, `return {}`, `f({})`.
- `#` comments still work next to `//`, `/* */`, `///` and `/** */`.
