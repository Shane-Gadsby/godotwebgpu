# Load-ramp benchmark: content carried at 20.0 fps

- date: 2026-10-26 11:51 AEST
- machine: CHI, AMD Ryzen 9 7900X 12-Core Processor, 61 GB RAM
- gpu: NVIDIA GeForce RTX 4080 SUPER, 16376 MiB, driver 615.71.09
- os: Zorin OS 18.1, kernel 7.0.0-38-generic, 2560x1440 @ 164.96
- chrome: Google Chrome 156.0.8078.47
- firefox: Playwright Firefox build 1555 (157.0)
- engine: 4.8.dev.custom_build.0bffec3bb (native legs, WebGPU export)
- webgl: official 4.7.2.stable.official template, exported with 4.7.1.stable.official.a13da4feb
- tree: 41abb75ee7 (dirty) work/20
- settings: target_fps=20 max_s=180 mem_mb=1400 repeat=2 capped=0 legs=native_vulkan_fp,native_compat,webgl,webgpu_mobile,webgpu_fp,firefox_webgpu_fp ramps=all

N = the amount of content at which the frame rate falls to 20.0 fps (mean frame time over a 2 s window after a 1 s settle, a below-target window confirmed by a second one; crossing interpolated in log space between the last N above and the first N below). Higher is better. `>=` = a lower bound: the leg hit the N or time cap without falling to the target. `(mem)` = stopped by the memory budget (mem_mb, the web heap is 2 GB): memory-limited, not a frame-rate crossing. `(failed)` = the run died (device lost, crash, stall; reason in the per-run table): the last N measured above the target.

## N per leg

Median of the runs; every run's N is in the repeatability table.

| ramp | one unit of N | native Vulkan F+ | native Compat (GL) | WebGL (4.7.2) | WebGPU Mobile | WebGPU F+ | Firefox WebGPU |
|---|---|---:|---:|---:|---:|---:|---:|
| draws | 1 MeshInstance3D, own material, 12 tris (draw-call CPU) | 77,258 | 44,702 | 28,206 | 37,172 | 34,554 | 20,060 |
| nodes | draws unit + 1 GDScript transform update/frame | 42,964 | 28,981 | 19,937 | 34,489 | 32,115 | 20,908 |
| fill | 1 full-screen blended quad, 64-iteration ALU shader | 1,076 | 1,076 | 1,077 | 759 | 717 | 687 |
| lit | 1 PBR sphere (9.2k tris) under 7 shadowed lights | 26,981 | 6,944 | 4,140 | 33,339 | 26,598 | 21,262 |
| particles | 1 GPUParticles3D emitter of 2048 particles | 6,661 | 1,483 | 1,391 | 2,201 | 2,091 | 606 |

## Ratios

a / b = N_a / N_b: "WebGPU F+ / WebGL = 3.20x (+220%)" means WebGPU Forward+ carried 3.2 times the load WebGL did at 20.0 fps on that ramp.

| ramp | WebGPU F+ / WebGL | WebGPU Mobile / WebGL | WebGPU F+ / native Vulkan | WebGL / native Compat | F+ / Mobile (web) | Firefox / Chrome WebGPU |
|---|---:|---:|---:|---:|---:|---:|
| draws | 1.23x (+23%) | 1.32x (+32%) | 0.45x (-55%) | 0.63x (-37%) | 0.93x (-7%) | 0.58x (-42%) |
| nodes | 1.61x (+61%) | 1.73x (+73%) | 0.75x (-25%) | 0.69x (-31%) | 0.93x (-7%) | 0.65x (-35%) |
| fill | 0.67x (-33%) | 0.71x (-29%) | 0.67x (-33%) | 1.00x (+0%) | 0.94x (-6%) | 0.96x (-4%) |
| lit | 6.42x (+542%) | 8.05x (+705%) | 0.99x (-1%) | 0.60x (-40%) | 0.80x (-20%) | 0.80x (-20%) |
| particles | 1.50x (+50%) | 1.58x (+58%) | 0.31x (-69%) | 0.94x (-6%) | 0.95x (-5%) | 0.29x (-71%) |

## Repeatability

Every run's N (with its status when it is not a frame-rate crossing), the median, and the spread.

| leg | ramp | runs | N per run | median | spread (max-min)/median |
|---|---|---:|---|---:|---:|
| native Vulkan F+ | draws | 2 | 79,478, 75,036 | 77,258 | 5.7% |
| native Vulkan F+ | nodes | 2 | 44,328, 41,600 | 42,964 | 6.4% |
| native Vulkan F+ | fill | 2 | 1,076, 1,076 | 1,076 | 0.0% |
| native Vulkan F+ | lit | 2 | 26,999, 26,964 | 26,981 | 0.1% |
| native Vulkan F+ | particles | 2 | 6,715, 6,606 | 6,661 | 1.6% |
| native Compat (GL) | draws | 2 | 44,844, 44,560 | 44,702 | 0.6% |
| native Compat (GL) | nodes | 2 | 29,538, 28,424 | 28,981 | 3.8% |
| native Compat (GL) | fill | 2 | 1,075, 1,076 | 1,076 | 0.1% |
| native Compat (GL) | lit | 2 | 6,913, 6,975 | 6,944 | 0.9% |
| native Compat (GL) | particles | 2 | 1,462, 1,503 | 1,483 | 2.7% |
| WebGL (4.7.2) | draws | 2 | 27,679, 28,734 | 28,206 | 3.7% |
| WebGL (4.7.2) | nodes | 2 | 19,843, 20,030 | 19,937 | 0.9% |
| WebGL (4.7.2) | fill | 2 | 1,075, 1,078 | 1,077 | 0.2% |
| WebGL (4.7.2) | lit | 2 | 4,043, 4,238 | 4,140 | 4.7% |
| WebGL (4.7.2) | particles | 2 | 1,319, 1,462 | 1,391 | 10.3% |
| WebGPU Mobile | draws | 2 | 37,155, 37,190 | 37,172 | 0.1% |
| WebGPU Mobile | nodes | 2 | 35,170, 33,808 | 34,489 | 3.9% |
| WebGPU Mobile | fill | 2 | 755, 764 | 759 | 1.1% |
| WebGPU Mobile | lit | 2 | 33,066, 33,612 | 33,339 | 1.6% |
| WebGPU Mobile | particles | 2 | 2,150, 2,252 | 2,201 | 4.6% |
| WebGPU F+ | draws | 2 | 34,743, 34,364 | 34,554 | 1.1% |
| WebGPU F+ | nodes | 2 | 33,035, 31,195 | 32,115 | 5.7% |
| WebGPU F+ | fill | 2 | 717, 716 | 717 | 0.1% |
| WebGPU F+ | lit | 2 | 26,648, 26,548 | 26,598 | 0.4% |
| WebGPU F+ | particles | 2 | 2,054, 2,128 | 2,091 | 3.5% |
| Firefox WebGPU | draws | 2 | 13,967, 26,153 | 20,060 | 60.7% |
| Firefox WebGPU | nodes | 2 | 20,652, 21,164 | 20,908 | 2.4% |
| Firefox WebGPU | fill | 2 | 685, 690 | 687 | 0.7% |
| Firefox WebGPU | lit | 2 | 21,222, 21,301 | 21,262 | 0.4% |
| Firefox WebGPU | particles | 2 | 645, 566 | 606 | 13.0% |

## Legs

| leg | engine | renderer reported | driver | viewport | GPU | browser |
|---|---|---|---|---|---|---|
| native Vulkan F+ | 4.8-dev_(custom_build) | forward_plus | vulkan | 1280x720 | NVIDIA NVIDIA GeForce RTX 4080 SUPER | - |
| native Compat (GL) | 4.8-dev_(custom_build) | gl_compatibility | opengl3 | 1280x720 | NVIDIA NVIDIA GeForce RTX 4080 SUPER | - |
| WebGL (4.7.2) | 4.7.2-stable_(official) | gl_compatibility | opengl3 | 1280x720 | ANGLE (NVIDIA Corporation, NVIDIA GeForce RTX 4080 SUPER/PCI | chrome uncapped=1 canvas=1280x720 |
| WebGPU Mobile | 4.8-dev_(custom_build) | mobile | webgpu | 1280x720 | nvidia/lovelace/ | chrome uncapped=1 canvas=1280x720 |
| WebGPU F+ | 4.8-dev_(custom_build) | forward_plus | webgpu | 1280x720 | nvidia/lovelace/ | chrome uncapped=1 canvas=1280x720 |
| Firefox WebGPU | 4.8-dev_(custom_build) | forward_plus | webgpu | 1280x720 | ?/?/ | firefox uncapped=1 canvas=1280x720 |

Per-step detail: the `[RAMP]` lines in each log. Units of N and the method: `godot/load_ramp/ramp.gd`.
