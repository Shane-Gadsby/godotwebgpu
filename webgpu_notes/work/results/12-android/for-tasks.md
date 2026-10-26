# For TASKS.md / the board (task 12)

- §2.35 / §2.9 (2026-10-25, worker-12/12b): Chrome for Android on the Razr 60 works with SPIRV-Cross, and is as fast
  as Tint or faster once patch 0019 is in. GPU ms/frame, Tint vs SC+0019: fp 16.9 / 16.8, heavy 73.8 / 71.3,
  postfx 23.3 / 23.2, platformer 29.6 / 28.7, stress 28.8 / 28.1. The fp_check Look set (34 frames) is identical to
  Tint on the phone. Firefox for Android is out of scope (user).
- §3 trap candidate (cost ~2 h): on Mali-G615 / Chrome 155 a WGSL `for (var i = -3; i <= 3; i++)` runs zero times
  (volumetric fog filter, fog lost its density). `loop`/`while` forms of the same loop are correct. Desktop shows
  nothing. SPIRV-Cross patch 0019 never emits `for`. Bisect a phone-only look bug by swapping WGSL modules on device
  (`phone_bench.mjs --swap-wgsl`, `results/12-android/` scratch bisect.sh).
- §2.9 open, converter-independent: fp_check subsurface_scattering_1 on the phone is 11.1 mean abs diff from desktop
  Vulkan with both Tint and SPIRV-Cross (`looks/fp_subsurface_scattering_1.razr60-chrome-sc19.png` beside the vulkan-desktop one).

- `webgpu_tests/perf_guard/android/README.md` (lead to add): `phone_bench.mjs` measures without CDP (injected page
  script, fetch reports, VIEW intent), so it is the tool for Firefox for Android; it never closes the user's tabs (the
  older scripts in that folder close every tab but the one they use: memory "Android phone profiling" says not to).
  The README's "Firefox for Android has no WebGPU yet" line dates from 2026-10-17; re-check when the phone is back.
- Trap candidate (cost 10 min): a `fetch(..., { keepalive: true })` body is capped at 64 KB and the fetch rejects
  above it, silently if caught. A page script that reports frames that way loses every frame.
