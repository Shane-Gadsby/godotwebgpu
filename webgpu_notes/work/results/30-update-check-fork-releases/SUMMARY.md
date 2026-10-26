# Task 30 summary: the editor's update check looks at this fork's GitHub releases

## What changed (branch work/30)

- `editor/project_manager/engine_update_label.{h,cpp}`: with `EngineUpdateLabel::FORK_RELEASES_REPO =
  "Shane-Gadsby/godotwebgpu"` (non-empty), the check asks `api.github.com/repos/<repo>/releases?per_page=30` instead of
  `godotengine.org/versions.json`, and the label opens the release's GitHub page. Upstream's code path is untouched
  behind that runtime constant (empty string = upstream behavior, no `#ifdef`); every change tagged `// WebGPU fork`.
  Settings, network/offline mode, error-and-retry and the hidden-when-up-to-date behavior are unchanged.
- **Newer = commit date** (design and reasons in `for-tasks.md`): the newest acceptable published release's commit
  (from its `main-<sha>` tag) is either the build's own `GODOT_VERSION_HASH` (up to date, one request) or its
  committer date is fetched (`/commits/<sha>`, second request) and compared with `GODOT_VERSION_TIMESTAMP`. No change
  to `version.py`, the release workflow or tag naming. Prereleases only under Check Newest Preview / Auto-on-dev; the
  engine version from asset names filters other majors and, for Check Newest Patch, other minors.
- Dev builds only (`DEV_ENABLED`): `GODOT_UPDATE_CHECK_AS=<hash>:<commit UNIX time>` poses as another build, for
  checking the label against the live API.
- `tests/editor/project_manager/test_engine_update_label.cpp`: 3 doctest cases (parsing, picking, comparison, plus
  the ISO date parse the check relies on).
- `doc/classes/EditorSettings.xml`: a Note on `network/connection/check_for_updates`.

## Verification (all on this branch)

- `scons platform=linuxbsd target=editor dev_build=yes tests=yes warnings=extra werror=yes -j12`: clean (4 min 11 s;
  only the pre-existing Wayland protocol DTD notices).
- `--test --test-case="*EngineUpdateLabel*"`: 3 test cases, 29 assertions, all passed.
- Project manager windowed under Xvfb against the live API (`shot.sh`, 15 s after start), `label_states.png` top to
  bottom:
  1. posing as v1.5's commit dated before v1.5.1: **"Update available: 4.8.dev 1.5.1 - OSX fix."**
     (`project_manager_older_build.png`, full window);
  2. posing as v1.5.1's own commit: no label (up to date);
  3. the real build (webgpu-4.8 at 2a91f79996, committed after the release): no label;
  4. network mode Offline: "Offline mode, update checks disabled."
- Not checked: clicking the label (would `xdg-open` a browser; the URL is the API's `html_url`, checked to start with
  `https://github.com/`). `webgpu_notes/work/smoke.sh` not run: no driver, shader or converter code is touched.

## For the lead

- Integrate `for-tasks.md` (TASKS §4 entry) and `for-docs.md` (optional CLAUDE.md line, wiki paragraph).
- Two questions for the hand-offs page in `for-tasks.md` (how "newer" is decided, default commit date; which repo,
  default Shane-Gadsby/godotwebgpu; CLAUDE.md's `dwalter/godotwebgpu` origin line looks stale).
- No .github workflow change.

## Risks

- A build from a branch committed after a release but not containing it (webgpu-4.8 before a main-only fix) is not
  told about that release: the binary cannot know ancestry, and asking GitHub's compare API fails for unpushed commits.
- GitHub's unauthenticated limit is 60 requests/hour per address; a check costs one or two. Hitting it shows the
  existing "Failed to check for updates. Response code: 403." with click-to-retry.
- If the release workflow stops tagging `main-<sha>` and puts no full SHA in `target_commitish`, no release qualifies
  and the label stays hidden (silently). Tests pin the expected format.
