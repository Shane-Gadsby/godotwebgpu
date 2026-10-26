# Task 30: text for webgpu_notes/TASKS.md

## §4 archive entry (new section, next free number)

### §4.x The editor's update check looks at this fork's releases (2026-10-26, work/30)

`EngineUpdateLabel` (`editor/project_manager/engine_update_label.{h,cpp}`, the project manager's "Update available"
link) asked `godotengine.org/versions.json` and compared stock Godot's versions with 4.8.dev, which told this fork's
users about the wrong product. It now asks `api.github.com/repos/Shane-Gadsby/godotwebgpu/releases`
(`EngineUpdateLabel::FORK_RELEASES_REPO`; an empty string restores upstream's check, whose code is left intact).
Every change is tagged `// WebGPU fork`.

- **Which release:** published releases only (drafts never; the unauthenticated API does not list them anyway).
  Prereleases only under *Check Newest Preview* (and *Auto* on a non-stable build, which this fork always is: 4.8.dev).
  The engine version is read from the release's asset names (`godot-webgpu-export-templates-<v>.tpz`,
  `godot-webgpu-editor-<v>-<os>.zip`): another major version is never offered, *Check Newest Patch* keeps the minor
  version, a release whose assets name none is not filtered. Newest by `published_at`.
- **Newer how:** the release's commit comes from its `main-<sha>` tag (or a full SHA in `target_commitish`). If it is
  the build's own `GODOT_VERSION_HASH`, the build is up to date. Otherwise a second request
  (`/repos/.../commits/<sha>`) gets the release commit's committer date, which is compared with
  `GODOT_VERSION_TIMESTAMP` (the build commit's committer date, `methods.py`): later = update available. An unknown
  time (a build without git) never reports an update. Chosen because releases carry no machine-readable fork version
  (it is only in the hand-written title) and the binary has none, so a version compare would need `version.py`, the
  release workflow and the naming habit to change; the commit date needs nothing new and is right for every case
  that matters (older build: told; the release's own build: not told; a later webgpu-4.8 build: not told). Its one
  blind spot: a build from a branch committed after the release but not containing it (webgpu-4.8 before a fix
  only on `main`) is not told. Two requests per check against GitHub's 60/hour unauthenticated limit.
- **The label** shows the release title without the "Godot WebGPU " prefix ("Update available: 4.8.dev 1.5.1 - OSX
  fix.") and opens the release's GitHub page. Network mode, offline label, error/retry, settings are unchanged.
- **Testing:** `tests/editor/project_manager/test_engine_update_label.cpp` (3 cases, `--test --test-case="*EngineUpdateLabel*"`).
  A dev build (`DEV_ENABLED`) reads `GODOT_UPDATE_CHECK_AS=<hash>:<commit UNIX time>` to pose as another build
  against the live API, so the label's states can be seen without building an old commit; release editors ignore it.

## §3 trap (only if the lead thinks it earns one)

None cost an hour.

## Questions for the user

1. **How should "newer" be decided?** (choice)
   - **Commit date of the release vs. the build (default, implemented).** Nothing changes in how releases are made;
     a build is told about a published release built from a later commit than its own.
   - A fork version number: a `fork_version` field in `version.py` (e.g. `1.5.1`), bumped before each release, that the
     release workflow puts in the tag (`v1.5.1`) and title, and the editor compares. Exact and readable ("1.6.0
     available"), but every release needs the bump, and the workflow and tags change.
2. **Which repository?** Default `Shane-Gadsby/godotwebgpu` (origin, where the releases are). CLAUDE.md still names
   `dwalter/godotwebgpu` as origin; that line looks stale either way.
