# 50-build-template-order: SUMMARY

## What changed (branch work/50)

- **build-linux.sh**: editor → wgsl_convert_cli → emsdk env + patches → **web template_debug/template_release with
  threads=no dlink_enabled=no, each installed into builds/<version>/templates/ as soon as it links** → green
  `==> Ready to export (H:MM:SS into the run): ... installed in <dir>` line → Linux templates → the remaining web variants
  (the matrix loop skips no/no). The web build+install is now one function (`build_web_template`), used by both loops.
  The timing table gains a `ready to export (web_nothreads pair installed)` row. The narrowing flags still filter the
  pair (e.g. `--web-targets release` builds only its release half first); a filter that excludes threads=no or dlink=no
  skips it with a warning. The Linux templates run under the PATH from before `emsdk_env.sh` (`env PATH="$NATIVE_PATH"`),
  as they effectively did before, when they ran before the web builds.
  New env overrides `EDITORS_DIR` / `TPZ_COPY_DIR` (defaults unchanged: godot-editors/editors, godot-editors/templates;
  a missing dir is skipped) — needed so a test run from a worktree does not replace the user's editor and .tpz.
- **build.sh** (menu options 2/3/10), **build-macos.sh**, **build-windows.ps1**: gained the non-dlink no-threads pair,
  built and installed first with the same Ready line, then (macOS/Windows: after the native templates) the dlink pair.
  **Bug fixed on the way**: all three built only the dlink pair, found it as "newest `bin/godot.web.template_*.zip`", and
  installed it as `web_nothreads_*.zip`; it is now `web_dlink_nothreads_*.zip`, and the scons output name is spelled out.
  macOS/Windows native template builds run with the pre-emsdk PATH (emsdk can put its own Python, without SCons, first).
- Every Ready line warns when the install dir is not the editor's export templates folder
  (`$XDG_DATA_HOME/godot/export_templates/<ver>`, `~/Library/Application Support/Godot/...`, `%APPDATA%\Godot\...`).
  On this machine `builds/4.8.dev/templates` is a symlink to `~/.local/share/godot/export_templates/4.8.dev`, so in the
  lead's tree it is silent and the pair is live for the editor immediately. No direct copy into that folder was added
  (see for-tasks.md).

## Verification

Narrowed real run from the worktree under the `build` lease (waited 13:17 → 13:08 for worker-45's lease):
`EDITORS_DIR=/nonexistent/editors TPZ_COPY_DIR=/nonexistent/templates ./build-linux.sh --skip-install
--skip-editor-plugins --skip-native-templates --web-targets debug,release --web-threads yes,no --web-dlink no`
(dlink=yes left out to spare the shared lease; the dlink loop is the same code path as threads=yes). Nothing was written
outside the worktree (godot-editors/editors and /templates untouched; the worktree's builds/ is a plain directory).

```
13:08:41 ==> Building editor (platform=linuxbsd)...
13:09:05 ==> Building wgsl_convert_cli ...
13:09:06 ==> Building web/WebGPU export template (debug, dlink=no, threads=no)...
13:10:09 Web template -> .../builds/4.8.dev/templates/web_nothreads_debug.zip
13:10:59 Web template -> .../builds/4.8.dev/templates/web_nothreads_release.zip
13:10:59 ==> Ready to export (0:02:18 into the run): web_nothreads_debug.zip web_nothreads_release.zip installed in .../templates
13:10:59     ... is not the editor's export templates folder (/home/schme16/.local/share/godot/export_templates/4.8.dev):
13:10:59 ==> Building web/WebGPU export template (debug, dlink=no, threads=yes)...
13:13:17 Web template -> .../web_debug.zip
13:14:44 Web template -> .../web_release.zip
==> Time per step: editor 0:24, nothreads debug 1:03, release 0:50, threads debug 2:18, release 1:27, TOTAL 6:03,
    ready to export 0:02:18. EXIT 0
```

build.sh / build-macos.sh: `bash -n` OK, `shellcheck -S warning` clean apart from the existing SC2115 on
`rm -rf "$dest/$(basename "$app")"`; build-linux.sh shellcheck clean. build-windows.ps1: PowerShell parser 0 errors
(pwsh 7), and `Build-WebTemplate` run with a stubbed `Invoke-Native` installs `godot.web.template_debug.wasm32.nothreads.zip`
→ `web_nothreads_debug.zip` and `...release.wasm32.nothreads.dlink.zip` → `web_dlink_nothreads_release.zip`.

smoke.sh (scripts only, so expected unchanged):
```
[WGSL Precompile] Results: 292 compiled, 0 glsl failures, 0 conversion failures
identical interface: 292/292, mismatched: 0, missing in candidate: 0, not in reference: 0
against the stored reference: 584/584 identical, 0 differ, 0 missing, 0 not in reference
validation: 292 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261028-131456-1421726)
```

## Not verified / for the lead

- Not run: the Linux native template step after the pair (`env PATH="$NATIVE_PATH" scons ...`, review only), the dlink
  web variants, and macOS/Windows end to end (no such machines). The user's next full `./build-linux.sh` in the lead's
  tree is the full confirmation: "Ready to export" should appear ~2-3 min in, before "Building Linux export template".
- Risk: low. The order change means the Linux templates now wait for the pair; total run time is unchanged.
