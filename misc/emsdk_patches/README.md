# Emscripten toolchain patches

Patches this fork applies to the **Emscripten toolchain itself** (not to vendored
sources — those live in `thirdparty/*/patches/`). The toolchain at `$EMSDK` is
external and cannot be versioned, so the patches, the pins and the tooling live
here and are re-applied on demand.

```bash
./apply.sh --status    # what is applied, against which toolchain (default)
./apply.sh --check     # exit 0 if ALL patches are applied, 1 if not -- for CI and scons
./apply.sh --apply     # idempotent
./apply.sh --revert    # restore upstream text
```

## When you need this

For **`platform=web dlink_enabled=yes`** builds. Both patches are in code paths
only a dlink build takes (`-sSIDE_MODULE`, `-sEXPORT_ALL`), and both symptoms need
a pthread worker to fire, so:

| configuration | without the patches |
|---|---|
| `dlink_enabled=yes threads=yes` | **does not boot** (0001), plus ~11 errors/load and silently dropped `Module` exports on worker threads (0002) |
| `dlink_enabled=yes threads=no` | unaffected — neither symptom can fire, but the patches are still correct |
| `dlink_enabled=no` (any `threads`) | unaffected, nothing to apply |

`apply.sh` is all-or-nothing, so just apply it for any dlink build.

**`emsdk install` overwrites the toolchain tree, so this must be re-applied after
every toolchain change**, not just after an emsdk version bump. `build-linux.sh`
does it itself, right after its own `emsdk install`. A `dlink_enabled=yes` build
tells you if the patches are missing — loudly when `threads=yes`, as a note
otherwise.

## Current patches

| | |
|---|---|
| `0001-dylink-asm-consts-pthread-race.patch` | Fixes the `ASM_CONSTS` initialization race that stops a threads + GDExtension build from booting at all. Two halves — `tools/emscripten.py` and `src/lib/libdylink.js` — and **neither is valid without the other**; the patch header explains why the obvious one-line version is worse than the crash. |
| `0002-export-all-eager-heap-views.patch` | Stops `-sEXPORT_ALL=1` re-exporting the `HEAP*` views eagerly in the postamble, where `growMemViews()` throws on a pthread worker whose `wasmMemory` hasn't arrived yet. Non-fatal but it aborts the export block, so later `Module` exports (`PThread`, `addRunDependency`, …) are silently never assigned on workers. `updateMemoryViews()` already owns those properties. **Do not "fix" this with accessors** — the patch header explains why that breaks `Module.HEAP*` outright. |

## How this is built

`apply.sh` only locates the toolchain. Every patch and all the logic lives in
**`emsdk_patch.py`**, as a table of hunks:

```python
PATCHES = [{"id": ..., "marker": ..., "hunks": [{"file": ..., "old": ..., "new": ...}]}]
PINS    = {"<file>": "<sha256 of that hunk's `old` line>"}
```

**Adding a patch is a table entry plus a `.patch` file next to it** — no logic
changes. The `.patch` files are the human-readable record of *what and why*; the
table is what executes. Keep them in sync: the `.patch` file is what someone
reads in two years, and it is the only place the reasoning lives.

Requirements the machinery enforces for you:

- `marker` must appear in every hunk's `new` text. That is how an already-patched
  file is recognized, which is what makes `--apply` idempotent and lets a
  half-applied tree be finished instead of refused.
- `PINS[file]` must equal `sha256(hunk["old"])`. Checked at runtime, so an
  inconsistent edit to the table is caught before it reaches the toolchain
  rather than producing a mystery.
- `--revert` is a plain `new` → `old` replacement, so it needs no hand-written
  inverse. If the marker is present but the exact patched text is not, the file
  was edited by something else and revert **refuses** rather than guessing.

After editing the table, run `./apply.sh --revert && ./apply.sh --apply` and
confirm `--status` reports what you expect.

### Also update the build's toolchain fingerprint — automatically handled

`platform/web/detect.py`'s `get_toolchain_js_fingerprint()` reads this table and
hashes every file it names, on top of its own explicit list. **A new patch is
therefore covered with no action needed.** That matters: if a patched file were
missing from the hash, scons would not relink and the patch would appear to do
nothing (see the gotcha below).

## How this is designed to retire itself

- **Pinned by content, never by line number.** Every hunk is pinned to the
  sha256 of the whole upstream line it replaces. If upstream edits that line, we
  refuse and print what to do, rather than fuzzily applying a patch whose
  assumptions have moved. Substring matching is deliberately *not* used: a line
  with text appended still contains the old text, and an early version of this
  tooling patched such a line happily until a deliberately-broken-pin test caught
  it.
- **The removal criterion is in each patch header**, under "HOW TO TELL WHEN THIS
  CAN BE DELETED", and it is a test for *the bug* rather than for the patch.
  Deleting a patch means deleting its `.patch` file and its `PATCHES` entry.
- There is **no standalone reproducer yet** — `repro/` records what was tried and
  why it does not fire, and names the real-export procedure used instead. That is
  a known gap against Task 14 subtask 1.5.3.

## CI

**Not wired into CI, deliberately.** CI builds only `threads=no dlink_enabled=yes`
(`.github/workflows/webgpu_tests.yml:211`), where neither symptom can fire, so an
`apply.sh --apply` step there would change nothing in the artifacts. Task 14
subtask 1.5.6.1 assumed CI built the threaded configuration; it does not. **If a
`threads=yes dlink_enabled=yes` job is ever added, it must run `apply.sh --apply`
after the emsdk setup step** (`mymindstorm/setup-emsdk`, line 137) and before the
build.

## Gotcha that wasted a day

**scons used not to relink because the toolchain changed.** `--apply` or
`--revert` left the previous `bin/godot*.js` in place, and then "the patch didn't
work" was indistinguishable from "the patch wasn't in the binary". That is exactly
what happened on 2026-10-07: the release threads+dlink template was linked after
0001 and booted, the debug one had been linked before it and was never relinked,
so the editor's "Run in Browser" (which uses the *debug* template) kept throwing
the original `ASM_CONSTS` TypeError against an already-fixed toolchain.

`platform/web/detect.py`'s `get_toolchain_js_fingerprint()` now hashes the
toolchain files that shape the generated JS glue — including every file named in
this directory's patch table — and `platform/web/SCsub` makes that hash a
dependency of the link, so applying or reverting relinks on its own. Nothing to
delete by hand.

Still verify by grepping the *linked* output rather than trusting `--status`,
which describes the toolchain and says nothing about `bin/`. The output is
minified, and for 0001 **both** halves must be present:

```bash
J=bin/godot.web.template_debug.wasm32.dlink.js
grep -c 'ASM_CONSTS??={}'            $J   # 0001, libdylink.js half   -> 1
grep -o 'ASM_CONSTS=Object.assign[^{]*' $J   # 0001, emscripten.py half -> matches
grep -c 'Module\["HEAP8"\]=(growMemViews(),HEAP8)' $J   # 0002 applied    -> 0
```

A build carrying only 0001's `Object.assign` half still crashes; only the `??=`
half silently discards EM_ASM bodies. Check for both.
