# 42-web-loading-bar: progress bar toggle and colors in the Web export options (TASKS.md §2.49)

State: ready-for-integration
Owner: worker-42
Claimed: 2026-10-28 11:15
Needs: build (web template + editor), gpu (browser checks)
Branch: work/42
Results: webgpu_notes/work/results/42-web-loading-bar/

## Goal

Do TASKS.md §2.49 as written there (read it first; the user chose to add the toggle). Default output unchanged: an
export with default options produces the same HTML as before (diff it). Check Chrome, Firefox (and Safari only if
reachable) that the toggle hides/shows the bar and that the three colors apply to `<progress>` (vendor pseudo-elements)
in both default shells, and that a custom shell without the new placeholders still works. Class/export docs; for-docs.md
with the replacement text for the wiki's Engine-Additions-Web-Export-Options.md "Loading screen" section (read the page
in /mnt/109313D2109313D2/godot-editors/godotwebgpu.wiki; don't edit the wiki).
