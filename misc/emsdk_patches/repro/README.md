# Standalone reproducer — **does not currently reproduce the bug**

This is kept as a record of what was tried, not as a working test. Read this
before spending time on it again.

## What this was meant to be

`webgpu_notes/TASKS.md` Task 14 subtask 1.5.3 asks for a standalone reproducer
that tests for **the bug**, not for the patch, so that `apply.sh` can say "upstream
has fixed this, delete the patch" instead of silently outliving its cause. That is
the right design and it is still the right design. It just does not exist yet.

## What was built, and what it does

`main.c` + `side.c` + `pre.js` build a minimal `MAIN_MODULE` + `SIDE_MODULE`
pair where the side module carries an `EM_ASM` body and is instantiated on a
**freshly spawned** pthread worker:

```bash
source ~/emsdk/emsdk_env.sh
emcc side.c -sSIDE_MODULE=1 -pthread -o side.wasm
emcc main.c side.wasm -sMAIN_MODULE=1 -pthread -sPTHREAD_POOL_SIZE=8 \
     -sEXIT_RUNTIME=0 -o main.html
# serve with COOP/COEP headers and load main.html
```

Three conditions were found to be necessary just to get a *valid* test, and each
cost a round, so they are worth keeping:

- **The side module must also be built with `-pthread`**, or it fails much earlier
  with `mismatch in shared state of memory`.
- **A pre-spawned pool (`PTHREAD_POOL_SIZE` > 0 taken at startup) never races**:
  those workers load their side modules during startup. The race needs a worker
  spawned later.
- **`main()` must not `pthread_join`**: the worker cannot start until the main
  thread returns to the browser event loop, so joining deadlocks instead of
  racing ("Tried to spawn a new thread, but the thread pool is exhausted").

The generated `main.js` shows the structural hazard exactly as the real build
does — `addEmAsm`'s `ASM_CONSTS[start]` write sits ~930KB *ahead* of the
`var ASM_CONSTS = {...}` initializer. And yet it runs clean: `WORKER_RESULT 42`.

## Variants tried, all clean

1. Side module linked at build time (`emcc main.c side.wasm ...`).
2. Side module loaded at runtime via `Module['dynamicLibraries']` (`pre.js`),
   which is how Godot's JS shell does it.

## Untested hypothesis for why it does not fire

In this reproducer the side module has to be **fetched** on the worker, so by the
time instantiation happens the module's top-level JS — including the
`ASM_CONSTS` initializer — has already run to completion. Godot's workers receive
an **already-compiled** `WebAssembly.Module` from the main thread and instantiate
it synchronously inside their early bootstrap, which would put `addEmAsm` before
the initializer. Other untried differences: `-sSIDE_MODULE=2` (what this fork
uses) rather than `=1`, `-sMODULARIZE=1`, and the sheer size of the real side
module (~47MB, thousands of `EM_ASM` entries).

## What is used as the oracle instead

A real export, which reproduces reliably in about 25 seconds:

```bash
scons platform=web target=template_release dlink_enabled=yes webgpu=yes \
      opengl3=no threads=yes
# export any project with variant/thread_support=true, serve with COOP/COEP,
# load it, and watch the WORKER console (not just the page console).
```

On an unpatched toolchain a worker raises
`Uncaught TypeError: Cannot set properties of undefined (setting '<addr>')` and
the module never finishes loading. On a patched one the engine reports
`Build configuration: ..., multi-threaded, GDExtension support.` and boots.

**Two traps in running that check**, both of which produced wrong answers first:

- `threads/emscripten_pool_size` must be **8** (the engine's default), not `-1`.
  The link flag is `-sPTHREAD_POOL_SIZE="Module['emscriptenPoolSize']||8"`, and
  `-1 || 8` is `-1`, which wedges *every* threaded build — including
  `dlink_enabled=no`, which otherwise works. Always run `dlink_enabled=no` as a
  control: if it fails too, the harness is wrong, not the subject.
- Playwright's `page.on('console')` does **not** forward worker-target output, and
  this failure is raised on a worker. Use `page.on('pageerror')`, which does see
  it, or attach to worker targets over CDP.
