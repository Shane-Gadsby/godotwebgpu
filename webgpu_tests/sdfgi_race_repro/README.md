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

## Next steps if this reproduces

Per `plan-of-attack.md`'s Phase 1 step 2: binary-search what actually
eliminates it (forced `onSubmittedWorkDone()` sync between accumulate calls;
single command buffer per read+write pair; a second Dawn backend). This repro
is deliberately structured so those variants are small, additive changes to
`race_repro.js`'s `runOneSlack()`, not a rewrite.
