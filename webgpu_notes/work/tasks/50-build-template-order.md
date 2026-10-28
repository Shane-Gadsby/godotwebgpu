# 50-build-template-order

State: claimed
Owner: worker-50
Claimed: 2026-10-28 12:20
Needs: build (one full build-linux.sh run)
Branch: work/50
Results: webgpu_notes/work/results/50-build-template-order/

Read `webgpu_notes/work/README.md`, CLAUDE.md "Build Commands", and TASKS.md §2.50 (the whole task is written there).

## Goal

Do TASKS.md §2.50: every `build-*` helper builds **and installs** the no-threads, no-dlink web `template_debug` and
`template_release` right after the editor (and what the templates need), prints a "ready to export" line, then builds
the rest; `build.sh` / `build-macos.sh` / `build-windows.ps1` gain the non-dlink no-threads pair first. Verify with a real
`./build-linux.sh` run from your worktree that is narrowed enough to finish quickly but still shows the order (e.g.
`--skip-install --skip-editor-plugins --web-targets debug,release --web-threads yes,no --web-dlink yes,no
--skip-native-templates` or similar; take the `build` lease per run, five other workers share it), with timestamps showing
the pair installed and "ready to export" printed before the other variants; the macOS and Windows scripts by careful review (and `bash -n` / PowerShell parse
check) since this machine has neither.
