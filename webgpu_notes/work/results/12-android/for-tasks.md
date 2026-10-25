# For TASKS.md / the board (task 12)

- §2.35 / §2.9: phone not attached during task 12's first slot (2026-10-25 14:10-14:30); nothing measured on Android.
  Exports (Tint-everywhere vs SPIRV-Cross-everywhere at e99b18555e), `phone_bench.mjs` / `phone_summary.mjs` and
  `results/12-android/run_phone.sh` are ready; relaunch when the Razr 60 is plugged in.
- `webgpu_tests/perf_guard/android/README.md` (lead to add): `phone_bench.mjs` measures without CDP (injected page
  script, fetch reports, VIEW intent), so it is the tool for Firefox for Android; it never closes the user's tabs (the
  older scripts in that folder close every tab but the one they use: memory "Android phone profiling" says not to).
  The README's "Firefox for Android has no WebGPU yet" line dates from 2026-10-17; re-check when the phone is back.
- Trap candidate (cost 10 min): a `fetch(..., { keepalive: true })` body is capped at 64 KB and the fetch rejects
  above it, silently if caught. A page script that reports frames that way loses every frame.

## Questions for the user

- The phone was not attached (`adb devices` empty, 14:10 to 14:30): please plug the Razr 60 in over USB with USB
  debugging allowed (and the screen unlocked), so task 12 can run on it.
- Which Firefox is on the phone? If release Firefox for Android has no WebGPU (`navigator.gpu` missing), may a worker
  install Firefox Nightly from the Play Store (and set `dom.webgpu.enabled` in its about:config), or should Firefox
  for Android be reported as "no WebGPU in the browser" and left there? Default: report it, no install.
