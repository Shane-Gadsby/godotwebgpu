# 30-update-check-fork-releases: the editor's "new version available" check looks at this fork's GitHub releases

State: ready-for-integration
Owner: worker-30
Claimed: 2026-10-26 20:10
Needs: build (native editor only)
Branch: work/30
Results: webgpu_notes/work/results/30-update-check-fork-releases/

## Context

User request, 2026-10-26: "Check my fork's releases for the 'new version' checks". The editor's update check
(`editor/project_manager/engine_update_label.{h,cpp}`, `EngineUpdateLabel`, shown in the project manager and the
editor; setting `network/connection/check_for_updates` or similar, and the editor's network mode) fetches
`https://godotengine.org/versions.json`, compares with the engine's own version (4.8.dev) and links to
`godotengine.org/download/archive/<version>`. For this fork that compares against stock Godot, which is the wrong
product: the fork's users should be told about new releases of **this fork**.

The fork's releases: https://github.com/Shane-Gadsby/godotwebgpu/releases (`origin`). Drafted by `🔗 GHA`
(`.github/workflows/runner.yml`, job `draft-release`) on a push to `main`, then published by the user. Today's list
(`gh release list -R Shane-Gadsby/godotwebgpu`):

```
Godot WebGPU 4.8.dev 1.5.1 - OSX fix   Latest   main-f184b220602d64d04d86a48f82e7b3cf7a4eb25a   2026-10-20
Godot WebGPU Forward+ v1.5                      main-44f0cc3a5a5b1036d506968504c2dee515842041   2026-10-19
Godot WebGPU Forward+ v1.0                      main-f2036d700ed509247494bdbd35a805eab816b71d   2026-09-28
Godot WebGPU Forward+ v0.9                      main-6691bb81cc156c3d8df79e79030c2532d7f9ab08   2026-09-24
```

So tags are `main-<commit>`, and the fork's version number (1.5.1) appears only in the hand-written title. The engine
binary knows its commit hash (`GODOT_VERSION_HASH`) but no fork version.

## Goal

1. The update check asks GitHub's releases API for this fork (`https://api.github.com/repos/Shane-Gadsby/godotwebgpu/releases`
   — published releases only; drafts and prereleases per the existing "stable only / any" setting if there is one) instead
   of godotengine.org, and the label/button opens the release page. Keep the existing UX, settings and offline/network
   mode behavior; keep upstream's code path intact where it costs nothing (an `#ifdef`-free switch, e.g. a constant for
   the repo), and tag every change `// WebGPU fork` or as the surrounding code does, so an upstream sync sees it.
2. Decide how "newer" is determined, and say why. Options: (a) a fork version constant in the source (e.g. in
   `version.py` as a new field, surfaced as `GODOT_VERSION_...` or an `Engine.get_version_info()` key) that the release
   workflow also uses to name the release and tag, compared with a version parsed from the release (tag or title);
   (b) compare the release's commit (`target_commitish`/the `main-<sha>` tag) against the binary's `GODOT_VERSION_HASH`
   (newer = a published release whose commit is not an ancestor... which the binary cannot know offline), or by
   `published_at` against the build date. Recommend one with a default; if it needs the release workflow or the
   user's naming habit to change (e.g. tags like `v1.5.1`), put that under "Questions for the user" in for-tasks.md with
   your recommended default and implement the default. Do not change or create releases or tags on GitHub, and do not
   push anything: reading the public API is fine.
3. Tests: a unit test for the version parsing/comparison (tests/ directory, doctest, as upstream tests the existing
   label if it does), and a manual check in a windowed editor under Xvfb against the live API (screenshot of the label
   in results/), plus offline behavior.

## Verification

Native editor builds clean with `dev_mode=yes`-level warnings on the changed files; the unit tests pass; the live check
shows the right state for a binary older than the latest release and none for one at it.
