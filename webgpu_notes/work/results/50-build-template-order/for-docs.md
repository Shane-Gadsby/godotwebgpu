# Wiki: Engine-Additions-Building.md (task 50, TASKS.md §2.50)

Changes for `/mnt/109313D2109313D2/godot-editors/godotwebgpu.wiki/Engine-Additions-Building.md`. Not edited by the
worker; the lead copies them in.

## The "Build scripts" table: replace the four rows

| Script | Platform | Builds |
|---|---|---|
| `build-linux.sh` | Linux | The editor, then the web templates without threads or GDExtension support (debug and release), then the Linux export templates and the other six web template variants (with and without threads, with and without GDExtension support). Installs prerequisites first. |
| `build-macos.sh` | macOS | The editor, the web templates without threads or GDExtension support, a universal macOS template, then the web templates with GDExtension support. Installs prerequisites first. Run on a Mac. |
| `build-windows.ps1` | Windows | The editor, the web templates without threads or GDExtension support, the Windows export templates, then the web templates with GDExtension support, all packaged into a `.tpz`. Installs prerequisites first (`-SkipInstall`, `-SkipWeb` and `-Jobs` are available). |
| `build.sh` | Linux | An interactive menu for the editor and templates, including cross-compiled Windows and macOS builds. Its web templates are built without threads, first without GDExtension support and then with it. |

## New paragraph, after "Building the editor and the web templates in one run keeps them from the same build. ..."

The web templates most exports use, without thread support or GDExtension support (`web_nothreads_debug.zip` and
`web_nothreads_release.zip`), are built right after the editor and installed straight away. The script then prints
**Ready to export**, and you can export with them while the remaining templates build. They are installed in
`builds/<version>/templates/`; the editor reads templates from its export templates folder
(`~/.local/share/godot/export_templates/<version>/` on Linux, `~/Library/Application Support/Godot/export_templates/<version>/`
on macOS, `%APPDATA%\Godot\export_templates\<version>\` on Windows), so the script says so when the two differ. Make
that folder a link to `builds/<version>/templates/` to export with each new build without copying anything.

## build-linux.sh options table: one row changed

| `--web-targets`, `--web-threads`, `--web-dlink` | Narrow the web template matrix, for example `--web-targets release --web-threads no --web-dlink no`. The no-threads, no-GDExtension templates are still built first when the filters include them. |

## After "The script prints the time each step took when it finishes." append

It also prints how long into the run the export-ready templates were installed.

## New line under the options table (or a short "Environment" subsection)

`EDITORS_DIR` and `TPZ_COPY_DIR` set the two folders outside the source tree that `build-linux.sh` also installs the
editor and the `.tpz` into; a folder that does not exist is skipped.
