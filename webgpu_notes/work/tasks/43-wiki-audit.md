# 43-wiki-audit: wiki pages for engine additions that have none

State: open
Owner: worker-43
Claimed:
Needs: — (no build)
Branch: (wiki repo, local commits only; no branch in the main repo except this task file)
Results: webgpu_notes/work/results/43-wiki-audit/

## Goal

The user wants the GitHub wiki (local clone /mnt/109313D2109313D2/godot-editors/godotwebgpu.wiki, branch master; the
lead pushes, you never push) to document every user-facing addition of this fork. The Camera section was just drafted
(Engine-Additions-Camera*.md, commit 4a4898d, not pushed): don't redo it, and leave the Web Export Options loading-screen
section to task 42.

1. Audit: list every user-facing addition in README.md ("Engine additions" and other feature sections) and
   webgpu_notes/TASKS.md §4 (archive) and check whether the wiki covers it accurately. Write the table to
   `results/43-wiki-audit/AUDIT.md` (feature, TASKS/README ref, wiki page or "missing", stale facts found).
2. Write the missing pages and fix stale facts, matching the existing pages' style (Engine-Additions-*.md, _Sidebar.md,
   Engine-Additions.md, Home.md). User-facing only: what it does, how to use it, limits; property and option names
   checked against doc/classes/*.xml and the export plugin code.
3. Commit locally in the wiki repo with a plain message (no trailers), report the hash.
Commit the task file and results folder on a work/43 branch in a worktree of the main repo as usual.
