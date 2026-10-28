# For the lead: CLAUDE.md and TASKS.md (task 50, §2.50)

## CLAUDE.md, "Build Commands", the `./build-linux.sh` paragraph

Replace its first sentence

> `./build-linux.sh` is the full local build: editor, Linux templates and the 8-variant web matrix, one SCons run after
> another in this tree, so every template's baked WGSL matches the editor's baker.

with

> `./build-linux.sh` is the full local build: editor, then the **no-threads, no-dlink web pair** (debug + release),
> installed into `builds/<version>/templates/` at once with a "Ready to export" line (also in the timing table), then the
> Linux templates and the other six web variants, one SCons run after another in this tree, so every template's baked
> WGSL matches the editor's baker. `build.sh`, `build-macos.sh` and `build-windows.ps1` build that pair first too, then
> their `dlink_enabled=yes threads=no` pair (§2.50).

and add to the end of its flags list (after `--plugins-only (...)`):

> `EDITORS_DIR` / `TPZ_COPY_DIR` override the two installs outside the tree (`godot-editors/editors`, `godot-editors/templates`;
> a missing folder is skipped), so a test run from a worker's worktree does not replace the user's editor.

## TASKS.md

- §2.50 → §4 (closed). Archive text:

  > **Build scripts: the no-threads, no-dlink web templates first** (2026-10-28, task 50). Every `build-*` helper builds
  > and installs `web_nothreads_{debug,release}.zip` right after the editor (and `wgsl_convert_cli`), prints "Ready to
  > export", then the rest. `build-linux.sh`: editor → wgsl_convert_cli → emsdk patches → the pair → Linux templates →
  > the other web variants; the narrowing flags still apply (a filter that excludes threads=no or dlink=no skips the
  > pair, with a warning), and the timing table gains a "ready to export" line. `build.sh` / `build-macos.sh` /
  > `build-windows.ps1` gained the non-dlink pair first; they had built only the dlink pair **and installed it as
  > `web_nothreads_*.zip`** (picking "the newest bin/ zip"), so an export from their templates used a GDExtension
  > template whether or not the preset asked for one. Each script now spells out the scons zip name. Native template
  > builds run with the PATH from before `emsdk_env` (on macOS/Windows emsdk can put its own Python first). The
  > "editor's export templates folder" is reached only because `builds/<version>/templates` is a symlink to it on the
  > user's machine; the ready line warns when the two differ.

- §3 trap (optional, short): "A build helper that picks 'the newest `bin/godot.web.template_debug*.zip`' installs
  whichever variant was built last under whatever name it was given: `build.sh`, `build-macos.sh` and
  `build-windows.ps1` shipped the dlink template as `web_nothreads_*.zip` for months. Spell the scons output name out
  (`.wasm32[.nothreads][.dlink].zip`)."

## Questions for the user

None needed. One choice the lead may want to confirm: the pair goes to the same place as every other template
(`builds/<version>/templates/`), which on this machine is a symlink to `~/.local/share/godot/export_templates/4.8.dev`.
The scripts do not copy into the editor's export templates folder directly when it is a different folder; they print a
hint instead. Adding a direct copy would be a few lines per script.
