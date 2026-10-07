# Emscripten toolchain patches

Patches this fork applies to the **Emscripten toolchain itself** (not to vendored
sources — those live in `thirdparty/*/patches/`). The toolchain at `$EMSDK` is
external and cannot be versioned, so the patch, the pins and the tooling live
here and are re-applied on demand.

```bash
./apply.sh --status    # what is applied, against which toolchain (default)
./apply.sh --check     # exit 0 if applied, 1 if not -- for CI and SConstruct
./apply.sh --apply     # idempotent
./apply.sh --revert    # restore upstream text
```

## When you need this

Only for **`platform=web threads=yes dlink_enabled=yes`** builds. Every other
configuration this fork ships — including `threads=no dlink_enabled=yes`, the
default — is unaffected and needs nothing.

**`emsdk install` overwrites the toolchain tree, so this must be re-applied after
every toolchain change**, not just after an emsdk version bump. A `threads=yes
dlink_enabled=yes` build warns if the patch is missing.

## Current patches

| | |
|---|---|
| `0001-dylink-asm-consts-pthread-race.patch` | Fixes the `ASM_CONSTS` initialization race that stops a threads + GDExtension build from booting at all. Two halves — `tools/emscripten.py` and `src/lib/libdylink.js` — and **neither is valid without the other**; the patch header explains why the obvious one-line version is worse than the crash. |

## How this is designed to retire itself

- **Pinned by content, never by line number.** `apply.sh` hashes the whole
  upstream line it replaces. If upstream edits that line, it refuses and prints
  what to do, rather than fuzzily applying a patch whose assumptions have moved.
  Substring matching is deliberately *not* used: a line with text appended still
  contains the old text, and an early version of this script patched such a line
  happily until a deliberately-broken-pin test caught it.
- **The removal criterion is in the patch header**, under "HOW TO TELL WHEN THIS
  CAN BE DELETED", and it is a test for *the bug* rather than for the patch.
- There is **no standalone reproducer yet** — `repro/` records what was tried and
  why it does not fire, and names the real-export procedure used instead. That is
  a known gap against Task 14 subtask 1.5.3.

## CI

**Not wired into CI, deliberately.** CI builds only `threads=no dlink_enabled=yes`
(`.github/workflows/webgpu_tests.yml:211`), which does not need this patch, so an
`apply.sh --apply` step there would do nothing. Task 14 subtask 1.5.6.1 assumed CI
built the threaded configuration; it does not. **If a `threads=yes
dlink_enabled=yes` job is ever added, it must run `apply.sh --apply` after the
emsdk setup step** (`mymindstorm/setup-emsdk`, line 137) and before the build.

## Gotcha that wasted a day

**scons used not to relink because the toolchain changed.** `--apply` or
`--revert` left the previous `bin/godot*.js` in place, and then "the patch didn't
work" was indistinguishable from "the patch wasn't in the binary". That is exactly
what happened on 2026-10-07: the release threads+dlink template was linked after
the patch and booted, the debug one had been linked before it and was never
relinked, so the editor's "Run in Browser" (which uses the *debug* template) kept
throwing the original `ASM_CONSTS` TypeError against an already-fixed toolchain.

`platform/web/detect.py`'s `get_toolchain_js_fingerprint()` now hashes the
toolchain files that shape the generated JS glue — including the two this patch
touches — and `platform/web/SCsub` makes that hash a dependency of the link, so
applying or reverting relinks on its own. Nothing to delete by hand.

Still verify by grepping the *linked* output rather than trusting `--status`,
remembering it is minified and that **both** halves must be present:

```bash
grep -c 'ASM_CONSTS??={}' bin/godot.web.template_debug.wasm32.dlink.js        # libdylink.js half
grep -o 'ASM_CONSTS=Object.assign[^{]*' bin/godot.web.template_debug.wasm32.dlink.js  # emscripten.py half
```

A build carrying only the `Object.assign` half still crashes; only the
`??=` half silently discards EM_ASM bodies. Check for both.
