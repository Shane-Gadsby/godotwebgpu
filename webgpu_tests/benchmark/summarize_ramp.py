#!/usr/bin/env python3
"""Markdown summary of load-ramp logs (run_ramp.sh): N per ramp per leg, and ratios between legs.

    python3 summarize_ramp.py [--note TEXT] <logs dir or log files...>

Logs are named <leg>__r<run>.log. Every "[BENCHMARK] ramp=<name> k=v ..." line is one result. With several runs of a
leg the table shows the median N and the spread table shows (max - min) / median.

Ratio definition: a/b = N_a / N_b, the content leg a carried at the target fps relative to leg b. "3.20x (+220%)" reads
"a carried 3.2 times b's load at 20 fps". A value marked >= is a lower bound (the leg hit the N or time cap before
falling to the target); <= marks a leg that was below the target even at N = 1.
"""
import os
import re
import statistics
import sys

LEGS = [
    ("native_vulkan_fp", "native Vulkan F+"),
    ("native_compat", "native Compat (GL)"),
    ("webgl", "WebGL (4.7.2)"),
    ("webgpu_mobile", "WebGPU Mobile"),
    ("webgpu_fp", "WebGPU F+"),
    ("firefox_webgpu_fp", "Firefox WebGPU"),
]
RAMPS = [
    ("draws", "1 MeshInstance3D, own material, 12 tris (draw-call CPU)"),
    ("nodes", "draws unit + 1 GDScript transform update/frame"),
    ("fill", "1 full-screen blended quad, 64-iteration ALU shader"),
    ("lit", "1 PBR sphere (2.3k tris) under 7 shadowed lights"),
    ("particles", "1 GPUParticles3D emitter of 2048 particles"),
]
RATIOS = [
    ("webgpu_fp", "webgl", "WebGPU F+ / WebGL"),
    ("webgpu_mobile", "webgl", "WebGPU Mobile / WebGL"),
    ("webgpu_fp", "native_vulkan_fp", "WebGPU F+ / native Vulkan"),
    ("webgl", "native_compat", "WebGL / native Compat"),
    ("webgpu_fp", "webgpu_mobile", "F+ / Mobile (web)"),
    ("firefox_webgpu_fp", "webgpu_fp", "Firefox / Chrome WebGPU"),
]
KV = re.compile(r"(\w+)=(\S+)")


def parse(path):
    env, browser, ramps = {}, {}, {}
    with open(path, errors="replace") as f:
        for line in f:
            i = line.find("[BENCHMARK] ")
            if i < 0:
                continue
            body = line[i + 12:].strip()
            if body.startswith("ramp_env "):
                env = dict(KV.findall(body))
            elif body.startswith("browser_env "):
                browser = dict(KV.findall(body))
            elif body.startswith("ramp="):
                d = dict(KV.findall(body))
                ramps[d["ramp"]] = d
    return env, browser, ramps


def bound(status):
    return {"cap_n": ">=", "cap_time": ">=", "below_min": "<="}.get(status, "")


def fmt_n(v):
    return f"{v:,.0f}" if v >= 100 else f"{v:.1f}"


def main(argv):
    note = ""
    if "--note" in argv:
        i = argv.index("--note")
        note = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    files = []
    for a in argv:
        if os.path.isdir(a):
            files += sorted(os.path.join(a, f) for f in os.listdir(a) if f.endswith(".log"))
        else:
            files.append(a)
    runs = {}  # leg -> list of (env, browser, ramps, file)
    for f in files:
        leg = os.path.basename(f).split("__")[0].removesuffix(".log")
        runs.setdefault(leg, []).append((*parse(f), f))

    target = "?"
    for leg_runs in runs.values():
        for env, _, ramps, _ in leg_runs:
            target = env.get("target_fps", target)
    legs = [(k, n) for k, n in LEGS if k in runs] + [(k, k) for k in runs if k not in dict(LEGS)]

    # Per leg, per ramp: median N, bound marker, all values.
    res = {}
    for leg, _ in legs:
        for ramp, _ in RAMPS:
            vals, marks = [], set()
            for _, _, ramps, _ in runs[leg]:
                if ramp in ramps:
                    vals.append(float(ramps[ramp]["n"]))
                    marks.add(bound(ramps[ramp].get("status", "")))
            if vals:
                res[(leg, ramp)] = (statistics.median(vals), "".join(sorted(m for m in marks if m)), vals)

    out = []
    out.append(f"# Load-ramp benchmark: content carried at {target} fps")
    out.append("")
    if note:
        out.append(f"**{note}**")
        out.append("")
    out.append(f"N = the amount of content at which the frame rate falls to {target} fps (median frame time over a 2 s window "
               "after a 1 s settle; crossing interpolated in log space between the last N above and the first N below). "
               "Higher is better. `>=` = the leg hit the N/time cap without falling to the target (lower bound).")
    out.append("")
    out.append("## N per leg")
    out.append("")
    out.append("| ramp | one unit of N | " + " | ".join(n for _, n in legs) + " |")
    out.append("|---|---|" + "---:|" * len(legs))
    for ramp, unit in RAMPS:
        if not any((leg, ramp) in res for leg, _ in legs):
            continue
        cells = []
        for leg, _ in legs:
            if (leg, ramp) in res:
                v, m, _ = res[(leg, ramp)]
                cells.append(f"{m}{fmt_n(v)}")
            else:
                cells.append("-")
        out.append(f"| {ramp} | {unit} | " + " | ".join(cells) + " |")
    out.append("")

    ratio_rows = [(a, b, name) for a, b, name in RATIOS if a in runs and b in runs]
    if ratio_rows:
        out.append("## Ratios")
        out.append("")
        out.append("a / b = N_a / N_b: \"WebGPU F+ / WebGL = 3.20x (+220%)\" means WebGPU Forward+ carried 3.2 times the "
                   f"load WebGL did at {target} fps on that ramp.")
        out.append("")
        out.append("| ramp | " + " | ".join(name for _, _, name in ratio_rows) + " |")
        out.append("|---|" + "---:|" * len(ratio_rows))
        for ramp, _ in RAMPS:
            if not any((leg, ramp) in res for leg, _ in legs):
                continue
            cells = []
            for a, b, _ in ratio_rows:
                if (a, ramp) in res and (b, ramp) in res and res[(b, ramp)][0] > 0:
                    va, ma, _ = res[(a, ramp)]
                    vb, mb, _ = res[(b, ramp)]
                    r = va / vb
                    # a lower bound on a over an exact b is a lower bound on the ratio, and so on.
                    mark = ""
                    if ma == ">=" and mb != ">=":
                        mark = ">="
                    elif mb == ">=" and ma != ">=":
                        mark = "<="
                    elif ma or mb:
                        mark = "?"
                    cells.append(f"{mark}{r:.2f}x ({(r - 1) * 100:+.0f}%)")
                else:
                    cells.append("-")
            out.append(f"| {ramp} | " + " | ".join(cells) + " |")
        out.append("")

    multi = {leg for leg, _ in legs if len(runs[leg]) > 1}
    if multi:
        out.append("## Repeatability")
        out.append("")
        out.append("| leg | ramp | runs | N values | spread (max-min)/median |")
        out.append("|---|---|---:|---|---:|")
        for leg, name in legs:
            if leg not in multi:
                continue
            for ramp, _ in RAMPS:
                if (leg, ramp) in res:
                    v, _, vals = res[(leg, ramp)]
                    sp = (max(vals) - min(vals)) / v * 100 if v else 0
                    out.append(f"| {name} | {ramp} | {len(vals)} | {', '.join(fmt_n(x) for x in vals)} | {sp:.1f}% |")
        out.append("")

    out.append("## Legs")
    out.append("")
    out.append("| leg | engine | renderer reported | driver | viewport | GPU | browser |")
    out.append("|---|---|---|---|---|---|---|")
    warnings = []
    expect = {"native_vulkan_fp": "forward_plus", "native_compat": "gl_compatibility", "webgl": "gl_compatibility",
              "webgpu_mobile": "mobile", "webgpu_fp": "forward_plus", "firefox_webgpu_fp": "forward_plus"}
    for leg, name in legs:
        env, br, _, _ = runs[leg][0]
        gpu = env.get("adapter", "")
        if br.get("webgpu_adapter", "none") not in ("none", ""):
            gpu = br["webgpu_adapter"]
        elif br.get("webgl_renderer"):
            gpu = br["webgl_renderer"]
        brs = f"{br.get('browser', '')} uncapped={br.get('uncapped', '')} canvas={br.get('canvas', '')}" if br else "-"
        out.append(f"| {name} | {env.get('engine', '?')} | {env.get('method', '?')} | {env.get('driver', '?')} | "
                   f"{env.get('viewport', '?')} | {gpu.replace('_', ' ')[:60]} | {brs} |")
        if not env:
            warnings.append(f"{name}: no ramp_env line (did the engine start?)")
        if env and env.get("method") != expect.get(leg, env.get("method")):
            warnings.append(f"{name}: renderer is {env.get('method')}, expected {expect[leg]}")
        if env and env.get("viewport") != "1280x720":
            warnings.append(f"{name}: viewport {env.get('viewport')}, not 1280x720")
        if br.get("software") == "1":
            warnings.append(f"{name}: SOFTWARE adapter ({gpu}) -- not a GPU measurement")
        for _, _, ramps, f in runs[leg]:
            missing = [r for r, _ in RAMPS if r not in ramps]
            if env.get("ramps"):
                missing = [r for r in missing if r in env["ramps"].split(",")]
            if missing:
                warnings.append(f"{name}: {os.path.basename(f)} has no result for {', '.join(missing)}")
    out.append("")
    if warnings:
        out.append("**Warnings:**")
        out.append("")
        out += [f"- {w}" for w in warnings]
        out.append("")
    out.append("Per-step detail: the `[RAMP]` lines in each log. Units of N and the method: `godot/load_ramp/ramp.gd`.")
    print("\n".join(out))


if __name__ == "__main__":
    main(sys.argv[1:])
