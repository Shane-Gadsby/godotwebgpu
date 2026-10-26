#!/usr/bin/env python3
"""Markdown summary of load-ramp logs (run_ramp.sh): N per ramp per leg, and ratios between legs.

    python3 summarize_ramp.py [--note TEXT] <results dir (run_ramp.sh --out) | logs dir | log files...>

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
    ("lit", "1 PBR sphere (9.2k tris) under 7 shadowed lights"),
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
    """-> (ramp_env, browser_env, {ramp: result fields}). A ramp that died ("ramp_failed", from ramp_browser.mjs or
    run_ramp.sh) gets n = the largest N its [RAMP] steps measured at or above the target fps, status failed_<reason>."""
    env, browser, ramps, steps, failed = {}, {}, {}, {}, {}
    with open(path, errors="replace") as f:
        for line in f:
            if line.startswith("[RAMP] "):
                d = dict(KV.findall(line))
                if "ramp" in d and "n" in d and "ms" in d:
                    steps.setdefault(d["ramp"], []).append((int(d["n"]), float(d["ms"])))
                continue
            i = line.find("[BENCHMARK] ")
            if i < 0:
                continue
            body = line[i + 12 :].strip()
            if body.startswith("ramp_env "):
                env = dict(KV.findall(body))
            elif body.startswith("browser_env "):
                browser = dict(KV.findall(body))
            elif body.startswith("ramp="):
                d = dict(KV.findall(body))
                ramps[d["ramp"]] = d
            elif body.startswith("ramp_failed "):
                d = dict(KV.findall(body))
                failed[d["ramp"]] = d.get("reason", "unknown")
    target_ms = 1000.0 / float(env.get("target_fps", 20))
    for ramp, reason in failed.items():
        if ramp in ramps:
            continue
        good = [n for n, ms in steps.get(ramp, []) if ms <= target_ms]
        ramps[ramp] = {"ramp": ramp, "n": str(max(good) if good else 0), "status": f"failed_{reason}"}
    return env, browser, ramps


def bound(status):
    if status.startswith("failed_") or status == "mem_cap":
        return ">="
    return {"cap_n": ">=", "cap_time": ">=", "below_min": "<="}.get(status, "")


def tag(status):
    """Short marker for a result that is not a frame-rate crossing."""
    if status == "mem_cap":
        return " (mem)"
    if status.startswith("failed_"):
        return " (failed)"
    return ""


def fmt_n(v):
    return f"{v:,.0f}" if v >= 100 else f"{v:.1f}"


def read_env(path):
    """env.txt written by run_ramp.sh: 'key: value' lines (machine, gpu, driver, browsers, tree, settings)."""
    env = {}
    if os.path.isfile(path):
        with open(path) as f:
            for line in f:
                k, _, v = line.partition(": ")
                if v:
                    env[k.strip()] = v.strip()
    return env


def load(paths):
    """-> (runs: leg -> [(env, browser, ramps, file)], machine env dict)."""
    files, machine = [], {}
    for a in paths:
        if os.path.isdir(os.path.join(a, "logs")):
            machine = read_env(os.path.join(a, "env.txt")) or machine
            a = os.path.join(a, "logs")
        if os.path.isdir(a):
            files += sorted(os.path.join(a, f) for f in os.listdir(a) if f.endswith(".log"))
        else:
            files.append(a)
    runs = {}
    for f in files:
        leg = os.path.basename(f).split("__")[0].removesuffix(".log")
        runs.setdefault(leg, []).append((*parse(f), f))
    return runs, machine


def results(runs):
    """-> {(leg, ramp): (median N, bound marker, [N per run])}."""
    res = {}
    for leg in runs:
        for ramp, _ in RAMPS:
            vals, marks, tags = [], set(), set()
            for _, _, ramps, _ in runs[leg]:
                if ramp in ramps:
                    vals.append(float(ramps[ramp]["n"]))
                    marks.add(bound(ramps[ramp].get("status", "")))
                    tags.add(tag(ramps[ramp].get("status", "")))
            if vals:
                res[(leg, ramp)] = (
                    statistics.median(vals),
                    "".join(sorted(m for m in marks if m)),
                    vals,
                    "".join(sorted(t for t in tags if t)),
                )
    return res


def cell(res, leg, ramp):
    """'>=12,345 (mem)': median N with its bound marker and tag."""
    v, m, _, t = res[(leg, ramp)]
    return f"{m}{fmt_n(v)}{t}"


def ratio_cell(res, a, b, ramp):
    """'1.23x (+23%)' for N_a / N_b, with >= / <= when a side is a bound, '-' when either is missing."""
    if (a, ramp) not in res or (b, ramp) not in res or res[(b, ramp)][0] <= 0:
        return "-"
    va, ma = res[(a, ramp)][:2]
    vb, mb = res[(b, ramp)][:2]
    r = va / vb
    mark = ""
    if ma == ">=" and mb != ">=":
        mark = ">="
    elif mb == ">=" and ma != ">=":
        mark = "<="
    elif ma or mb:
        mark = "?"
    return f"{mark}{r:.2f}x ({(r - 1) * 100:+.0f}%)"


def main(argv):
    note = ""
    if "--note" in argv:
        i = argv.index("--note")
        note = argv[i + 1]
        argv = argv[:i] + argv[i + 2 :]
    runs, machine = load(argv)

    target = "?"
    for leg_runs in runs.values():
        for env, _, ramps, _ in leg_runs:
            target = env.get("target_fps", target)
    legs = [(k, n) for k, n in LEGS if k in runs] + [(k, k) for k in runs if k not in dict(LEGS)]

    res = results(runs)

    out = []
    out.append(f"# Load-ramp benchmark: content carried at {target} fps")
    out.append("")
    if note:
        out.append(f"**{note}**")
        out.append("")
    for k in (
        "date",
        "machine",
        "gpu",
        "os",
        "chrome",
        "firefox",
        "engine",
        "webgl",
        "tree",
        "settings",
        "warning",
        "aborted",
    ):
        if k in machine:
            out.append(f"- {k}: {machine[k]}")
    if machine:
        out.append("")
    out.append(
        f"N = the amount of content at which the frame rate falls to {target} fps (mean frame time over a 2 s window "
        "after a 1 s settle, a below-target window confirmed by a second one; crossing interpolated in log space between the last N above and the first N below). "
        "Higher is better. `>=` = a lower bound: the leg hit the N or time cap without falling to the target. "
        "`(mem)` = stopped by the memory budget (mem_mb, the web heap is 2 GB): memory-limited, not a frame-rate "
        "crossing. `(failed)` = the run died (device lost, crash, stall; reason in the per-run table): the last N "
        "measured above the target."
    )
    out.append("")
    out.append("## N per leg")
    out.append("")
    out.append("Median of the runs; every run's N is in the repeatability table.")
    out.append("")
    out.append("| ramp | one unit of N | " + " | ".join(n for _, n in legs) + " |")
    out.append("|---|---|" + "---:|" * len(legs))
    for ramp, unit in RAMPS:
        if not any((leg, ramp) in res for leg, _ in legs):
            continue
        cells = []
        for leg, _ in legs:
            if (leg, ramp) in res:
                cells.append(cell(res, leg, ramp))
            else:
                cells.append("-")
        out.append(f"| {ramp} | {unit} | " + " | ".join(cells) + " |")
    out.append("")

    ratio_rows = [(a, b, name) for a, b, name in RATIOS if a in runs and b in runs]
    if ratio_rows:
        out.append("## Ratios")
        out.append("")
        out.append(
            'a / b = N_a / N_b: "WebGPU F+ / WebGL = 3.20x (+220%)" means WebGPU Forward+ carried 3.2 times the '
            f"load WebGL did at {target} fps on that ramp."
        )
        out.append("")
        out.append("| ramp | " + " | ".join(name for _, _, name in ratio_rows) + " |")
        out.append("|---|" + "---:|" * len(ratio_rows))
        for ramp, _ in RAMPS:
            if not any((leg, ramp) in res for leg, _ in legs):
                continue
            cells = [ratio_cell(res, a, b, ramp) for a, b, _ in ratio_rows]
            out.append(f"| {ramp} | " + " | ".join(cells) + " |")
        out.append("")

    multi = {leg for leg, _ in legs} if any(len(runs[leg]) > 1 for leg, _ in legs) else set()
    if multi:
        out.append("## Repeatability")
        out.append("")
        out.append("Every run's N (with its status when it is not a frame-rate crossing), the median, and the spread.")
        out.append("")
        out.append("| leg | ramp | runs | N per run | median | spread (max-min)/median |")
        out.append("|---|---|---:|---|---:|---:|")
        for leg, name in legs:
            if leg not in multi:
                continue
            for ramp, _ in RAMPS:
                if (leg, ramp) in res:
                    v, _, vals = res[(leg, ramp)][:3]
                    sp = (max(vals) - min(vals)) / v * 100 if v else 0
                    per_run = []
                    for _, _, ramps, f in runs[leg]:
                        if ramp in ramps:
                            st = ramps[ramp].get("status", "")
                            per_run.append(fmt_n(float(ramps[ramp]["n"])) + ("" if st == "converged" else f" ({st})"))
                    out.append(
                        f"| {name} | {ramp} | {len(vals)} | {', '.join(per_run)} | {cell(res, leg, ramp)} | {sp:.1f}% |"
                    )
        out.append("")

    out.append("## Legs")
    out.append("")
    out.append("| leg | engine | renderer reported | driver | viewport | GPU | browser |")
    out.append("|---|---|---|---|---|---|---|")
    warnings = []
    expect = {
        "native_vulkan_fp": "forward_plus",
        "native_compat": "gl_compatibility",
        "webgl": "gl_compatibility",
        "webgpu_mobile": "mobile",
        "webgpu_fp": "forward_plus",
        "firefox_webgpu_fp": "forward_plus",
    }
    for leg, name in legs:
        env, br, _, _ = runs[leg][0]
        gpu = env.get("adapter", "")
        if br.get("webgpu_adapter", "none") not in ("none", ""):
            gpu = br["webgpu_adapter"]
        elif br.get("webgl_renderer"):
            gpu = br["webgl_renderer"]
        brs = f"{br.get('browser', '')} uncapped={br.get('uncapped', '')} canvas={br.get('canvas', '')}" if br else "-"
        out.append(
            f"| {name} | {env.get('engine', '?')} | {env.get('method', '?')} | {env.get('driver', '?')} | "
            f"{env.get('viewport', '?')} | {gpu.replace('_', ' ')[:60]} | {brs} |"
        )
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
