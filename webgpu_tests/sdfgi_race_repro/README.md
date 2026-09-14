# SDFGI same-texel race — minimal repro

Step 1 of the investigation plan in `plan-of-attack.md`'s Tier 1 Phase 1
(SDFGI brightness runaway). A from-scratch WebGPU program — **not Godot** —
that isolates the one pattern all 39 rounds of `webgpu_notes/TASKS.md` Task
9.5 investigation converged on: two persistent `read_write` storage
textures, same texel read-modified-written every call
(`average -= prev; average += new; store(history, new); store(average, average)`),
across separate `queue.submit()`s with no explicit CPU-GPU sync — matching
`sdfgi_integrate.glsl`'s `lightprobe_history_tex`/`lightprobe_average_tex`
accumulation exactly, but with GLSL/Tint/Godot's renderer removed entirely.

Because the real algorithm telescopes (each call's subtraction exactly
cancels the previous call's own addition when reads see the true previous
write), the correct final value after N calls is a single known integer —
not a running sum — so any cross-submission hazard-tracking gap shows up as
an exact mismatch, not statistical noise.

## Run it

```bash
npm install playwright   # once
npx playwright install chromium   # once, if not already done

# The meaningful run — real Vulkan-backed GPU, matching webgpu_tests/
# scene_smoketest's WEBGPU_REAL_GPU convention (swiftshader is a materially
# different adapter, see webgpu_notes/TASKS.md Task 9.5):
WEBGPU_REAL_GPU=1 node run_repro.mjs

# Larger sample / different slack sweep:
WEBGPU_REAL_GPU=1 node run_repro.mjs --calls=2000 --slack=0,1,2,4,8,16,32,64

# Serve only, open manually in any WebGPU browser:
node run_repro.mjs --serve-only
```

Exit code 0 = not reproduced at any slack value tried; 2 = reproduced
(divergence found — see the printed table for which slack value(s)).

## What "slack" means here

Round 39 of the Task 9.5 trail found that skipping the real accumulation
dispatch on a schedule (simulating a multi-layer ping-pong buffer) reduces
the divergence rate but doesn't zero it out. `slack` here is the same idea:
between each accumulate call, N unrelated filler compute dispatches are
submitted on the same queue (simulating a real frame's other GPU work
sharing the timeline) before the next accumulate call touches the same
texel again.

## `native_emdawnwebgpu_repro.cpp` — same test through the real C API path

`race_repro.js` talks to `navigator.gpu` directly from browser JS. The real
engine never does that — it submits through `wgpuQueueSubmit()` (the
`webgpu.h` C API) via Emscripten's `emdawnwebgpu` port, same as
`platform=web webgpu=yes` builds this repo produces. `race_repro.js` alone
can't tell you whether that extra C API → JS glue layer has a bug of its
own, so this is the same repro rebuilt through that exact path instead:

```bash
source ~/emsdk/emsdk_env.sh   # Emscripten 4.0.10+, per CLAUDE.md
em++ -O2 --use-port=emdawnwebgpu -sEXIT_RUNTIME=0 -sASSERTIONS=1 -std=c++17 \
  native_emdawnwebgpu_repro.cpp -o native_repro.html

WEBGPU_REAL_GPU=1 node run_native_repro.mjs
WEBGPU_REAL_GPU=1 node run_native_repro.mjs --calls=2000 --slack=0,1,2,4,8,16,32,64
```

`native_repro.{html,js,wasm}` are build output (gitignored) — rebuild after
editing the `.cpp`. Exit codes/output format match `run_repro.mjs`.

**Known gotcha already worked around in this file**: `wgpu::Buffer::MapAsync()`'s
lambda-captured `wgpu::Buffer` copy did not reliably keep the JS-side buffer
alive across the async gap in this emdawnwebgpu version (Emscripten 4.0.11,
webgpu_cpp.h from the pinned `emdawnwebgpu` port) — manifested as
`MapAsyncStatus::Aborted` / `"Buffer was destroyed before mapping was
resolved."` every time, 100% reproducible, unrelated to slack or call count.
Worked around with an explicit `static std::vector<wgpu::Buffer>` keep-alive
before calling `MapAsync()`. This is a harness quirk, not a finding about
the actual investigation — flagging here so it isn't mistaken for one if
this file is revisited later.

## Next steps if this reproduces

Per `plan-of-attack.md`'s Phase 1 step 2: binary-search what actually
eliminates it (forced `onSubmittedWorkDone()` sync between accumulate calls;
single command buffer per read+write pair; a second Dawn backend). This repro
is deliberately structured so those variants are small, additive changes to
`race_repro.js`'s `runOneSlack()`, not a rewrite.
