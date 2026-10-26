# Task 30: text for shared docs

## CLAUDE.md

Nothing required. Optional one-liner under "Where to look first" or a new short Architecture paragraph:

> **Editor update check**: `EngineUpdateLabel` asks this fork's GitHub releases (`FORK_RELEASES_REPO`), not
> godotengine.org; a release is newer when its `main-<sha>` commit is not the build's and was committed later
> (`webgpu_notes/TASKS.md` §4.x). Keep the release workflow's `main-<sha>` tags, or the check finds no release.

## Wiki (Engine Additions)

> **Update notifications.** The project manager's "Update available" link checks Godot WebGPU's own releases on
> GitHub rather than Godot's, and opens the release page. A release counts as newer when it was built from a later
> commit than your editor. *Editor Settings > Network > Connection > Check for Updates* works as in Godot: Preview
> includes prereleases, Stable skips them, Patch stays on your engine's minor version; Offline mode disables it.

## doc/classes/EditorSettings.xml

Done on the branch (a Note under `network/connection/check_for_updates`).

## Release workflow

No change. Note for whoever edits `.github/workflows/runner.yml`'s draft-release: the check reads the commit from the
`main-<sha>` tag and the engine version from the `godot-webgpu-*-<version>` asset names.
