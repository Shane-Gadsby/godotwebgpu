#!/usr/bin/env python3
"""Turn a load-ramp result (run_ramp.sh) into the README's performance table.

    python3 readme_perf_table.py <results_ramp/<date> dir> [--write] [--readme PATH]

Prints the Markdown by default. --write replaces the text between `<!-- perf-table:begin -->` and
`<!-- perf-table:end -->` in README.md (the repo root's, or --readme) and leaves everything outside the markers alone.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from summarize_ramp import RAMPS, fmt_n, load, ratio_cell, results  # noqa: E402

BEGIN = "<!-- perf-table:begin -->"
END = "<!-- perf-table:end -->"
COLUMNS = [
    ("native_vulkan_fp", "Native (Vulkan)"),
    ("native_compat", "Native (Compatibility)"),
    ("webgl", "WebGL 2 (stock Godot 4.7.2)"),
    ("webgpu_mobile", "WebGPU, Mobile"),
    ("webgpu_fp", "WebGPU, Forward+"),
    ("firefox_webgpu_fp", "WebGPU, Forward+ (Firefox)"),
]
ROW_NAMES = {
    "draws": "Draw calls (a mesh with its own material)",
    "nodes": "Moving nodes (draw + GDScript update)",
    "fill": "Full-screen shader layers (fill)",
    "lit": "PBR spheres under 7 shadowed lights",
    "particles": "GPU particle emitters (2048 each)",
}


def table(result_dir):
    runs, env = load([result_dir])
    res = results(runs)
    target = "20"
    for leg_runs in runs.values():
        for e, _, _, _ in leg_runs:
            target = e.get("target_fps", target)
    target = target.removesuffix(".0")
    cols = [(k, n) for k, n in COLUMNS if k in runs]
    nruns = max((len(v) for v in runs.values()), default=0)
    out = []
    when = env.get("date", "?").split(" ")[0]
    gpu = env.get("gpu", "?").split(",")[0]
    driver = env.get("gpu", "").rpartition("driver ")[2]
    out.append(f"Measured on {when}: {env.get('machine', '?').split(', ', 1)[-1]}, {gpu} (driver {driver}), "
               f"{env.get('os', 'Linux').split(',')[0]}, {env.get('chrome', 'Chrome')}"
               + (f", {env['firefox'].split(' (')[0].replace('Playwright Firefox build', 'Playwright Firefox')}" if "firefox_webgpu_fp" in runs and env.get("firefox") else "")
               + f"; engine {env.get('tree', '?').split(' ')[0]}.")
    out.append("")
    out.append(f"**Method:** each ramp adds one kind of content until the frame rate falls to {target} fps, at 1280x720 "
               "with the frame-rate cap lifted, and the table gives how much content that took (higher is better; median "
               f"of {nruns} runs). \"1.50x (+50%)\" means WebGPU Forward+ carried 1.5 times the other leg's load at "
               f"{target} fps. `>=` marks a leg that hit the cap first. "
               "Source: `webgpu_tests/benchmark/run_ramp.sh` (one unit of N per ramp is in `webgpu_tests/README.md`).")
    out.append("")
    out.append("| Content ramped | " + " | ".join(n for _, n in cols) + " | WebGPU vs WebGL | WebGPU vs native |")
    out.append("| --- | " + " | ".join("---:" for _ in cols) + " | ---: | ---: |")
    for ramp, _ in RAMPS:
        if not any((k, ramp) in res for k, _ in cols):
            continue
        cells = []
        for k, _ in cols:
            if (k, ramp) in res:
                v, m, _ = res[(k, ramp)]
                cells.append(f"{m}{fmt_n(v)}")
            else:
                cells.append("-")
        out.append(f"| {ROW_NAMES[ramp]} | " + " | ".join(cells) + f" | {ratio_cell(res, 'webgpu_fp', 'webgl', ramp)} | "
                   f"{ratio_cell(res, 'webgpu_fp', 'native_vulkan_fp', ramp)} |")
    return "\n".join(out)


def main(argv):
    write = "--write" in argv
    argv = [a for a in argv if a != "--write"]
    readme = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "README.md")
    if "--readme" in argv:
        i = argv.index("--readme")
        readme = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    if len(argv) != 1:
        print(__doc__)
        return 1
    md = table(argv[0])
    if not write:
        print(md)
        return 0
    with open(readme) as f:
        text = f.read()
    pat = re.compile(re.escape(BEGIN) + r".*?" + re.escape(END), re.S)
    if not pat.search(text):
        print(f"{readme}: no {BEGIN} ... {END} markers")
        return 1
    text = pat.sub(lambda _: f"{BEGIN}\n{md}\n{END}", text, count=1)
    with open(readme, "w") as f:
        f.write(text)
    print(f"wrote the performance table into {os.path.normpath(readme)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
