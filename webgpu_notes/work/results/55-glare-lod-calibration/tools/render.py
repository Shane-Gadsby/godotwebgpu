#!/usr/bin/env python3
"""render.py <editor> <jobs.json> [parallel]: runs glare_test.gd renders (HDR) under xvfb-run.

jobs.json: list of {"out": path.png, "rm": forward_plus|mobile|gl_compatibility, "res": "WxH", "args": [k=v, ...]}.
A job whose <out>.f32 (hdr=1) or <out> already exists is skipped (reported as skipped, not as rendered).
Exits 1 if any render failed.
"""

import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ED = sys.argv[1]
JOBS = json.load(open(sys.argv[2]))
PAR = int(sys.argv[3]) if len(sys.argv) > 3 else 3
PROJ = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "proj")


def run(j):
    out = j["out"]
    hdr = "hdr=1" in j["args"]
    if os.path.exists(out + ".f32" if hdr else out):
        return ("skip", out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    w, h = j["res"].split("x")
    screen = f"-screen 0 {int(w) + 64}x{int(h) + 64}x24"
    cmd = [
        "timeout",
        "180",
        "xvfb-run",
        "-a",
        "-s",
        screen,
        ED,
        "--path",
        PROJ,
        "--rendering-method",
        j["rm"],
        "--resolution",
        j["res"],
        "-s",
        "glare_test.gd",
        "--",
        f"out={out}",
    ] + j["args"]
    with open(out[:-4] + ".log", "w") as log:
        rc = subprocess.call(cmd, stdout=log, stderr=subprocess.STDOUT)
    ok = os.path.exists(out + ".f32" if hdr else out)
    if ok and hdr:
        meta = json.load(open(out + ".json"))
        if f"{meta['w']}x{meta['h']}" != j["res"]:
            return ("fail", f"{out}: captured {meta['w']}x{meta['h']}, asked {j['res']}")
    return ("ok" if ok else "fail", out if ok else f"{out} rc={rc}")


with ThreadPoolExecutor(PAR) as ex:
    res = list(ex.map(run, JOBS))
n = {k: sum(1 for r in res if r[0] == k) for k in ("ok", "skip", "fail")}
for r in res:
    if r[0] == "fail":
        print("FAILED", r[1])
print(f"rendered {n['ok']}, skipped (existing) {n['skip']}, failed {n['fail']}")
sys.exit(1 if n["fail"] else 0)
