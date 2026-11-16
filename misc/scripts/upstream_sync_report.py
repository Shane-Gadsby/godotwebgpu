#!/usr/bin/env python3
"""Before an upstream Godot sync: what the merge will touch, and the things that cost hours on past syncs.

    misc/scripts/upstream_sync_report.py [--upstream upstream/master] [--base HEAD] [--project DIR ...]

Read-only: it runs `git merge-tree` (no worktree changes) and greps. Run `git fetch upstream` first. Each section
says why it is there; the playbook that goes with it is webgpu_notes/TASKS.md §3 trap 114.
"""

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# The base classes the WebGPU driver implements: a change here needs an audit of drivers/webgpu/ (CLAUDE.md).
INTERFACE_HEADERS = [
    "servers/rendering/rendering_device_driver.h",
    "servers/rendering/rendering_context_driver.h",
    "servers/rendering/rendering_shader_container.h",
]
# Files this fork rewrote, where an upstream change is usually a fix to port by hand, not a text merge
# (4.8 beta sync: upstream's Sample driver PR landed on top of our parity rewrite of the same driver).
FORK_REWRITES = [
    "platform/web/js/libs/library_godot_audio.js",
    "platform/web/audio_driver_web.cpp",
    "platform/web/js/engine/config.js",
    "platform/web/js/engine/engine.js",
    "platform/web/detect.py",
    "platform/web/SCsub",
    "servers/rendering/renderer_rd/shader_rd.cpp",
    "glsl_builders.py",
]
# Where the fork's own tests and fixtures call engine API by name.
TEST_TREES = ["webgpu_tests", "tests", "modules/gdscript/tests", "misc/dist", "docs"]
SCRIPT_EXTS = (".gd", ".gdb", ".tscn", ".tres", ".cfg", ".godot", ".mjs", ".js", ".py", ".cs")


def git(*args, check=True):
    r = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    if check and r.returncode not in (0, 1):
        sys.exit(f"git {' '.join(args)} failed: {r.stderr.strip()}")
    return r.stdout


def section(title, why):
    print(f"\n## {title}\n\n_{why}_\n")


def removed_api(base, upstream):
    """Property, method and constant names upstream bound before and no longer binds."""
    diff = git("diff", f"{base}...{upstream}", "--", "*.cpp")
    patterns = [
        re.compile(r'PropertyInfo\([^,]+,\s*"([a-z0-9_/]+)"'),
        re.compile(r'D_METHOD\("([a-z0-9_]+)"'),
        re.compile(r"BIND_(?:ENUM_|BITFIELD_)?CONSTANT\(([A-Z0-9_]+)\)"),
    ]
    removed, added = set(), set()
    for line in diff.splitlines():
        if line.startswith(("---", "+++")):
            continue
        for p in patterns:
            for name in p.findall(line):
                (removed if line.startswith("-") else added if line.startswith("+") else set()).add(name)
    return sorted(removed - added)


def grep_names(names, roots):
    hits = {}
    if not names:
        return hits
    rx = re.compile(r"\b(" + "|".join(re.escape(n) for n in names) + r")\b")
    for root in roots:
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in ("node_modules", ".godot", ".git", "exports", "export")]
            for f in filenames:
                if not f.endswith(SCRIPT_EXTS):
                    continue
                path = os.path.join(dirpath, f)
                try:
                    with open(path, encoding="utf-8", errors="ignore") as fh:
                        for i, line in enumerate(fh, 1):
                            for m in rx.findall(line):
                                hits.setdefault(m, []).append(f"{os.path.relpath(path, ROOT)}:{i}")
                except OSError:
                    pass
    return hits


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--upstream", default="upstream/master")
    ap.add_argument("--base", default="HEAD")
    ap.add_argument("--project", action="append", default=[], help="another project to grep for removed API")
    a = ap.parse_args()

    mb = git("merge-base", a.base, a.upstream).strip()
    commits = git("rev-list", "--count", f"{mb}..{a.upstream}").strip()
    up_files = set(git("diff", "--name-only", mb, a.upstream).split())
    our_files = set(git("diff", "--name-only", mb, a.base).split())
    print(f"# Upstream sync report: {a.base} + {a.upstream}\n")
    print(
        f"merge-base {mb[:10]} ({git('log', '-1', '--format=%cs', mb).strip()}); upstream "
        f"{git('rev-parse', '--short', a.upstream).strip()} ({git('log', '-1', '--format=%cs', a.upstream).strip()}),"
        f" {commits} commits, {len(up_files)} files"
    )

    section(
        "Predicted conflicts", "git merge-tree, nothing touched. Each needs a reason written into the merge commit."
    )
    mt = (
        subprocess
        .run(
            ["git", "merge-tree", "--write-tree", "--name-only", a.base, a.upstream],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        .stdout.split("\n\n")[0]
        .splitlines()[1:]
    )
    print("\n".join(f"- {f}" for f in mt) or "none")

    both = sorted(up_files & our_files)
    section(
        f"Changed on both sides ({len(both)})",
        "Clean merges here still break: the 4.8 beta sync's .gdb and "
        "fixture regressions were both in files that merged without a conflict.",
    )
    for prefix in ("servers/rendering", "drivers/", "platform/web", "modules/gdscript", "scene/"):
        group = [f for f in both if f.startswith(prefix)]
        if group:
            print(f"- {prefix}: " + ", ".join(os.path.relpath(f, prefix) if prefix.endswith("/") else f for f in group))

    section("Driver interface headers", "A change needs every override in drivers/webgpu/ checked (CLAUDE.md).")
    for h in INTERFACE_HEADERS:
        if h in up_files:
            print(f"- **{h}** changed:")
            for line in git("diff", mb, a.upstream, "--", h).splitlines():
                if line.startswith(("+", "-")) and not line.startswith(("+++", "---")) and "virtual" in line:
                    print(f"    {line.strip()}")
    if not any(h in up_files for h in INTERFACE_HEADERS):
        print("unchanged")

    section(
        "Script API upstream removed, still used here",
        "A removed property assigned from a fixture is a SCRIPT ERROR that aborts the rest of that function: the "
        "4.8 beta sync's whole Forward+ parity run failed on one (trap 113).",
    )
    gone = removed_api(mb, a.upstream)
    hits = grep_names(gone, [os.path.abspath(p) for p in a.project if os.path.isdir(p)])
    if gone:
        # The fork's own trees as of --base (git grep), so the report holds for a base other than the work tree.
        out = git("grep", "-n", "-w", "-E", "|".join(re.escape(n) for n in gone), a.base, "--", *TEST_TREES)
        for line in out.splitlines():
            _ref, path, num, text = line.split(":", 3)
            if path.endswith(SCRIPT_EXTS) and "/node_modules/" not in path:
                for name in gone:
                    if re.search(rf"\b{re.escape(name)}\b", text):
                        hits.setdefault(name, []).append(f"{path}:{num}")
    print(f"{len(gone)} names unbound upstream: {', '.join(gone[:40])}{' ...' if len(gone) > 40 else ''}\n")
    for name, where in sorted(hits.items()):
        print(f"- **{name}**: " + ", ".join(where[:8]) + (" ..." if len(where) > 8 else ""))
    if not hits:
        print("none used in the fork's tests" + (" or the given projects" if a.project else ""))

    section(
        "New .gd-only checks in modules/gdscript",
        "Each must become GDScriptLanguage::is_gdscript_path() "
        "(// GDB), or .gdb scripts silently miss that path (CLAUDE.md, Braced GDScript).",
    )
    found = False
    for line in git("diff", mb, a.upstream, "--", "modules/gdscript").splitlines():
        if line.startswith("+") and re.search(r'get_extension\(\)|has_extension\(|"gd"', line):
            print(f"    {line[1:].strip()}")
            found = True
    print("" if found else "none")

    section(
        "Version and toolchain",
        "status changes move the export template folder (export_templates/<x.y.status>); "
        "an Emscripten minimum can conflict with this fork's pinned, patched toolchain (misc/emsdk_patches).",
    )
    for f in ("version.py", "platform/web/detect.py", ".github/workflows/web_builds.yml"):
        for line in git("diff", mb, a.upstream, "--", f).splitlines():
            if re.match(r"[+-](status|major|minor|patch) =|[+-].*(cc_semver|EM_VERSION)", line):
                print(f"- {f}: {line.strip()}")

    section(
        "Upstream changes to files this fork rewrote", "Usually a fix to port by hand, or one the fork already has."
    )
    for f in FORK_REWRITES:
        log = git("log", "--no-merges", "--format=%h %s", f"{mb}..{a.upstream}", "--", f).strip()
        if log:
            print(f"- **{f}**\n" + "\n".join(f"    {line}" for line in log.splitlines()))
    print("\nNext: the playbook, webgpu_notes/TASKS.md §3 trap 114.")


if __name__ == "__main__":
    main()
