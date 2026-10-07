#!/usr/bin/env python3
"""Applies, reverts and reports this fork's Emscripten toolchain patches.

`apply.sh` is the entry point -- it locates the toolchain and delegates here.
Everything that is specific to a *patch* lives in PATCHES below; everything else
in this file is generic machinery, so adding a third patch means adding a table
entry and a .patch file next to it, and touching no logic.

Why a patch table rather than a .patch file plus `patch -p1`:

  * `patch` fuzzes. These patches rewrite code whose *meaning* matters, in a tree
    that is outside this repo and moves under us on every `emsdk install`. A
    fuzzy apply to a line upstream has since changed would produce a toolchain
    that builds and silently emits wrong JS -- far worse than refusing. Every
    hunk here is pinned to the sha256 of the WHOLE upstream line it replaces, so
    a changed line stops matching and we refuse with instructions instead.
  * `patch -R` needs the same context to revert, and we want `--revert` to work
    even from a partially-applied tree.
  * A half-applied toolchain is a state that really happens (an interrupted
    apply, a partially restored tree). Per-file detection makes it recoverable;
    whole-or-nothing checks refuse to ever finish the job once one file's
    upstream text is gone.

The .patch files in this directory stay as the human-readable record of what
each change is and why -- read those first. This table is what executes.
"""

import argparse
import hashlib
import sys
from pathlib import Path

# --- the patch table ---------------------------------------------------------
# Each hunk replaces exactly one whole line, matched by content, never by line
# number. `old_sha` is sha256 of `old` and is checked against this file at
# runtime, so an inconsistent edit here is caught rather than silently applied.
#
# `marker` must appear in every hunk's `new` text: it is how an already-patched
# file is recognized, which is what makes --apply idempotent and lets a
# half-applied tree be finished rather than refused.

PATCHES = [
    {
        "id": "0001-dylink-asm-consts-pthread-race",
        "marker": "GODOT WEBGPU PATCH (dylink+pthread ASM_CONSTS race)",
        "derived_against": "6.0.9",
        "required_for": "platform=web threads=yes dlink_enabled=yes",
        "summary": "side module's EM_ASM registration races the main module's ASM_CONSTS initializer",
        "hunks": [
            {
                "file": "tools/emscripten.py",
                "old": "    pre += 'var ASM_CONSTS = {\\n  ' + ',  \\n '.join(asm_const_pairs) + '\\n};\\n'",
                "new": (
                    "    # GODOT WEBGPU PATCH (dylink+pthread ASM_CONSTS race): merge into whatever a\n"
                    "    # side module already registered on this thread instead of replacing it. The\n"
                    "    # `var` declaration hoists, but this initializer runs ~900KB later in the\n"
                    "    # module's JS, so on a freshly spawned pthread worker a side module's\n"
                    "    # addEmAsm() can populate ASM_CONSTS before this line -- and a plain `=` then\n"
                    "    # silently discards those registrations.\n"
                    "    pre += 'var ASM_CONSTS = Object.assign(typeof ASM_CONSTS != \"undefined\" && ASM_CONSTS || {}, {\\n  ' + ',  \\n '.join(asm_const_pairs) + '\\n});\\n'"
                ),
            },
            {
                "file": "src/lib/libdylink.js",
                "old": "          {{{ makeEval('ASM_CONSTS[start] = eval(func)') }}};",
                "new": (
                    "          // GODOT WEBGPU PATCH (dylink+pthread ASM_CONSTS race): on a freshly spawned\n"
                    "          // pthread worker this can run before the module's own `var ASM_CONSTS = {...}`\n"
                    "          // initializer. Safe only because that initializer now merges rather than\n"
                    "          // replaces (tools/emscripten.py); on its own this guard would convert a loud\n"
                    "          // abort into silently-discarded EM_ASM bodies.\n"
                    "          ASM_CONSTS ??= {};\n"
                    "          {{{ makeEval('ASM_CONSTS[start] = eval(func)') }}};"
                ),
            },
        ],
    },
    {
        "id": "0002-export-all-eager-heap-views",
        "marker": "GODOT WEBGPU PATCH (eager heap-view export)",
        "derived_against": "6.0.9",
        "required_for": "platform=web dlink_enabled=yes (visible with threads=yes)",
        "summary": "-sEXPORT_ALL re-exports HEAP* eagerly; that throws on a pthread worker",
        "hunks": [
            {
                "file": "src/modules.mjs",
                "old": "    if ((EXPORT_ALL || EXPORTED_FUNCTIONS.has(ident) || extraExports.has(ident)) && !nativeAliases[ident]) {",
                "new": (
                    "    // GODOT WEBGPU PATCH (eager heap-view export): skip the memory views here.\n"
                    "    // updateMemoryViews() already assigns Module['HEAPxx'] for every view it creates\n"
                    "    // (see maybeExportHeap in src/runtime_common.js), and exportRuntimeSymbols()'s own\n"
                    "    // shouldExport() deliberately skips HEAP* for that reason -- 'HEAP objects are\n"
                    "    // exported separately in updateMemoryViews'. This loop never got the same\n"
                    "    // exclusion, so under -sEXPORT_ALL=1 the views are exported a SECOND time,\n"
                    "    // eagerly, in the postamble. The acorn growableHeap pass rewrites each of those\n"
                    "    // into `(growMemViews(), HEAPxx)`, and growMemViews() dereferences wasmMemory --\n"
                    "    // which on a freshly spawned pthread worker only arrives later, in the `load`\n"
                    "    // message. So the first one throws there, and every Module export emitted after\n"
                    "    // it is silently skipped (PThread, addRunDependency, ...).\n"
                    "    //\n"
                    "    // Skipping them leaves updateMemoryViews() as the single owner of\n"
                    "    // Module['HEAPxx'], which is what the non-EXPORT_ALL path already does. Do NOT\n"
                    "    // 'fix' this by exporting accessors instead: updateMemoryViews() assigns these\n"
                    "    // properties directly, so a getter-only property would make that assignment\n"
                    "    // throw under strict mode and silently no-op otherwise.\n"
                    "    if (/^HEAP(?:U?(?:8|16|32|64)|F32|F64|_DATA_VIEW)$/.test(ident)) continue;\n"
                    "    if ((EXPORT_ALL || EXPORTED_FUNCTIONS.has(ident) || extraExports.has(ident)) && !nativeAliases[ident]) {"
                ),
            },
        ],
    },
]

# sha256 of each hunk's `old` line, pinned separately so an inconsistent edit to
# the table above is caught by --apply instead of reaching the toolchain.
PINS = {
    "tools/emscripten.py": "cc195787d55070c52de97303d12f1fe608cf682a5d009d5a6585cb02eb40920d",
    "src/lib/libdylink.js": "b3c3add0fa4ca2a7440cec534c1ed9cbb6946c4826f00be99832d746e671f965",
    "src/modules.mjs": "bbd9b3b882d7abbb66e13c813961d1eb1e129d2da9a199290b03fac76e3f1131",
}


def sha(text):
    return hashlib.sha256(text.encode()).hexdigest()


def hunk_path(root, hunk):
    return root / hunk["file"]


def file_is_patched(root, patch, hunk):
    try:
        return patch["marker"] in hunk_path(root, hunk).read_text()
    except OSError:
        return False


def patch_state(root, patch):
    """-> 'applied' | 'not-applied' | 'partial'."""
    done = [file_is_patched(root, patch, h) for h in patch["hunks"]]
    if all(done):
        return "applied"
    if any(done):
        return "partial"
    return "not-applied"


def upstream_line_present(root, hunk):
    """True if some whole line of the file hashes to this hunk's pin.

    Whole-line and by hash, never substring: an upstream line with anything
    appended still *contains* the old text, so a substring match would happily
    patch a line whose meaning has changed. A deliberately-broken-pin test caught
    an earlier substring version doing exactly that.
    """
    want = PINS[hunk["file"]]
    try:
        with hunk_path(root, hunk).open() as f:
            return any(sha(line.rstrip("\n")) == want for line in f)
    except OSError:
        return False


def self_check(patch):
    """The table must describe the text it claims to, and be revertible."""
    problems = []
    for hunk in patch["hunks"]:
        if PINS.get(hunk["file"]) != sha(hunk["old"]):
            problems.append(f"  pin for {hunk['file']} does not match sha256 of its 'old' text")
        if patch["marker"] not in hunk["new"]:
            problems.append(f"  {hunk['file']}: 'new' text does not contain the patch marker")
    return problems


def verify(root, patch, verbose=True):
    """Can this patch be applied to this tree? Refuses rather than fuzzing."""
    problems = self_check(patch)
    if problems:
        if verbose:
            print(
                f"error: {patch['id']} is internally inconsistent; emsdk_patch.py was edited wrongly:", file=sys.stderr
            )
            print("\n".join(problems), file=sys.stderr)
        return False

    missing = [h for h in patch["hunks"] if not file_is_patched(root, patch, h) and not upstream_line_present(root, h)]
    if missing:
        if verbose:
            for h in missing:
                print(f"error: {hunk_path(root, h)} has no line matching the expected upstream text.", file=sys.stderr)
                print(
                    f"       expected sha256: {PINS[h['file']]} (of the whole line this patch replaces)",
                    file=sys.stderr,
                )
            print(file=sys.stderr)
            print(f"Upstream has changed the code {patch['id']} patches.", file=sys.stderr)
            print(f"This patch was derived against Emscripten {patch['derived_against']}.", file=sys.stderr)
            print("Do NOT force it -- re-derive it:", file=sys.stderr)
            print("  1. Check whether the bug still reproduces at all (see the patch file's", file=sys.stderr)
            print("     'HOW TO TELL WHEN THIS CAN BE DELETED'). If it does not, delete the", file=sys.stderr)
            print("     patch file and its entry in emsdk_patch.py's PATCHES table.", file=sys.stderr)
            print("  2. If it does, re-derive the hunk and update its text and pin here.", file=sys.stderr)
            print(f"See misc/emsdk_patches/{patch['id']}.patch and webgpu_notes/TASKS.md Task 12.", file=sys.stderr)
        return False
    return True


def apply_patch(root, patch):
    for hunk in patch["hunks"]:
        path = hunk_path(root, hunk)
        body = path.read_text()
        if patch["marker"] in body:
            continue  # this hunk is already applied; leave it alone
        lines = body.split("\n")
        hits = [i for i, line in enumerate(lines) if line == hunk["old"]]
        if len(hits) != 1:
            sys.exit(f"error: expected exactly one line matching the upstream text in {path}, found {len(hits)}")
        lines[hits[0]] = hunk["new"]
        path.write_text("\n".join(lines))


def revert_patch(root, patch):
    """Restore the upstream line by replacing `new` with `old` verbatim.

    No hand-written inverse and no regexes: the table already holds both sides
    exactly, so revert is one string replacement. Hunks that *rewrite* their line
    (0001's tools/emscripten.py) revert the same way as hunks that wrap it.

    If the marker is present but the exact patched text is not, the file has been
    edited since we patched it. We refuse rather than guess -- a partial revert
    of generator code is how you get a toolchain that builds and emits wrong JS.
    """
    for hunk in patch["hunks"]:
        path = hunk_path(root, hunk)
        body = path.read_text()
        if patch["marker"] not in body:
            continue
        if hunk["new"] not in body:
            sys.exit(
                f"error: {path} contains the patch marker but not the exact patched text.\n"
                f"       It has been edited by hand or by another tool; restore it with 'emsdk install'."
            )
        path.write_text(body.replace(hunk["new"], hunk["old"], 1))


def cmd_status(root, version, args):
    print(f"toolchain:       {root}")
    print(f"version:         {version}")
    for patch in PATCHES:
        state = patch_state(root, patch)
        label = patch["id"]
        if state == "applied":
            print(f"{label}: APPLIED")
        elif state == "partial":
            print(f"{label}: PARTIALLY APPLIED -- run --apply to finish, or --revert")
            for hunk in patch["hunks"]:
                mark = "patched" if file_is_patched(root, patch, hunk) else "upstream"
                print(f"{' ' * len(label)}  {hunk['file']}: {mark}")
        else:
            print(f"{label}: NOT APPLIED")
            if verify(root, patch, verbose=False):
                print(f"{' ' * len(label)}  (upstream text matches the pins; --apply will work)")
            else:
                print(f"{' ' * len(label)}  (upstream text does NOT match the pins -- --apply will refuse)")
        print(f"{' ' * len(label)}  derived against Emscripten {patch['derived_against']}")
        print(f"{' ' * len(label)}  required for: {patch['required_for']}")
        print(f"{' ' * len(label)}  {patch['summary']}")
    print()
    print("Verify by loading a real export; there is no standalone repro (see repro/README.md).")
    print("--status describes the TOOLCHAIN, not bin/godot*.js. When a result depends on a")
    print("patch being in the binary, grep the linked output.")
    return 0


def cmd_check(root, version, args):
    return 0 if all(patch_state(root, p) == "applied" for p in PATCHES) else 1


def cmd_apply(root, version, args):
    todo = [p for p in PATCHES if patch_state(root, p) != "applied"]
    if not todo:
        print(f"already applied ({len(PATCHES)} patches, Emscripten {version} at {root}) -- nothing to do")
        return 0
    for patch in todo:
        if not verify(root, patch):
            return 3
    for patch in todo:
        apply_patch(root, patch)
        print(f"applied {patch['id']}")
    print(f"Emscripten {version} at {root} is patched ({len(PATCHES)} patches).")
    return 0


def cmd_revert(root, version, args):
    todo = [p for p in PATCHES if patch_state(root, p) != "not-applied"]
    if not todo:
        print("not applied -- nothing to revert")
        return 0
    for patch in todo:
        revert_patch(root, patch)
        print(f"reverted {patch['id']}")
    still = [p["id"] for p in PATCHES if patch_state(root, p) != "not-applied"]
    if still:
        print(f"error: revert did not fully remove {', '.join(still)};", file=sys.stderr)
        print("       restore the toolchain with 'emsdk install'", file=sys.stderr)
        return 4
    print(f"reverted; Emscripten {version} at {root} is back to upstream text")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", required=True, help="Emscripten root (the directory holding emcc)")
    ap.add_argument("--version", default="unknown", help="Emscripten version, for messages")
    ap.add_argument("action", choices=["status", "check", "apply", "revert"])
    args = ap.parse_args()
    root = Path(args.root)
    handler = {"status": cmd_status, "check": cmd_check, "apply": cmd_apply, "revert": cmd_revert}
    return handler[args.action](root, args.version, args)


if __name__ == "__main__":
    sys.exit(main())
