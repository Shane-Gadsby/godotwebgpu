# For TASKS.md

## §4 entry

### 4.xx GPU time on WebGPU through pass timestampWrites (October 2026) *(work task 73; dynamic-resolution phase 1)*

`RenderingServer.viewport_get_measured_render_time_gpu()` and the Visual Profiler's GPU times now work on the web
when the device has `timestamp-query`, which the shell requests whenever the adapter offers it.

How the driver does it:
- A timestamp is the end-of-pass write of an empty compute pass. `writeTimestamp()` on an encoder was Chrome-only.
  The beginning write goes to a spare query, because emdawnwebgpu cannot leave that member out.
- Each frame slot's query pool has a ring of 4 `MAP_READ` buffers. A slot is never recorded into or unmapped while
  its `mapAsync` is pending, and when the ring is full a frame's timestamps are dropped rather than waited for.
- `get_results()` hands out the newest results that have landed, if their count matches, so on the web they are a
  few frames old.
- Without `timestamp-query` everything reads 0, as before.

Worker results (RTX 4080 SUPER, task 68's bench, 1920x1080, scales 1.0 to 0.5):
- Engine GPU time is within 2.5% of the page's timestamp hook in Chrome and in Firefox (release), and in Chrome on
  the debug template.
- Median frame time is the same with measurement on and off.
- With `timestamp-query` withheld: 0 and no errors.
- `--gpu-profile` shows per-pass GPU times.
- 0 console errors in every run.
- Dev_mode editor, web debug and release: 0 warnings.

## §3 trap candidates

- **emdawnwebgpu cannot omit a `GPUComputePassTimestampWrites` index.** It passes `WGPU_QUERY_SET_INDEX_UNDEFINED`
  through as 4294967295, a validation error. Give the unused write a spare query.
- **An empty compute pass's *beginning* write may land before earlier passes finish.** Use the end-of-pass write
  for a "here" timestamp, as Vulkan's bottom-of-pipe write does.

## Questions for the user

None.
