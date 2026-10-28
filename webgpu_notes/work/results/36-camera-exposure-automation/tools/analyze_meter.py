# analyze_meter.py <dir> [frame]: metered luminance per mode vs a reference computed here from the AE-off HDR frame.
# The reference follows the mode definitions (camera_attributes_storage.cpp), not the shader: pixel-center circle
# membership, Nikon's 75%/half-height circle, spot area as a fraction of the frame, np.percentile.
import json
import math
import sys
from typing import Any

import numpy as np

d = sys.argv[1]
frame = sys.argv[2] if len(sys.argv) > 2 else "30"


def godot_average(v):
    # luminance_reduce.glsl as written: 8x8 block means, each level floor(size / 8) (min 1) texels, so blocks past the
    # last whole one are dropped; a block's mean divides by its clipped size.
    src = v
    while True:
        h, w = src.shape
        dh, dw = max(h // 8, 1), max(w // 8, 1)
        out = np.zeros((dh, dw))
        for by in range(dh):
            for bx in range(dw):
                blk = src[by * 8 : min(by * 8 + 8, h), bx * 8 : min(bx * 8 + 8, w)]
                out[by, bx] = blk.sum() / (min(h - by * 8, 8) * min(w - bx * 8, 8))
        src = out
        if dh == 1 and dw == 1:
            return src[0, 0]


def load(p):
    j = json.load(open(f"{p}.{frame}.json"))
    return np.fromfile(f"{p}.{frame}.f32", dtype=np.float32).reshape(j["h"], j["w"], 3).astype(np.float64)


cases: dict[str, tuple[str, float, dict[str, Any]]] = {  # name: (mode, scale, extra)
    "avg": ("avg", 0.4, {}),
    "center": ("center", 0.4, {}),
    "spot": ("spot", 0.4, {"point": (0.5, 0.5), "size": 2.5}),
    "spotwin": ("spot", 0.4, {"point": (0.75, 0.3), "size": 1.0}),
    "hl99": ("hl", 0.4, {"p": 99.0}),
    "hl90": ("hl", 0.4, {"p": 90.0}),
    "hl999": ("hl", 0.8, {"p": 99.9}),
}
rows = []
for res in ["1920x1080", "1280x720"]:
    F = load(f"{d}/ref_{res}")
    H, Wd = F.shape[:2]
    aspect = Wd / H
    v = F.max(axis=2)
    yy, xx = np.mgrid[0:H, 0:Wd]
    qx = (xx + 0.5) / H
    qy = (yy + 0.5) / H
    for name, (mode, scale, ex) in cases.items():
        try:
            G = load(f"{d}/{name}_{res}")
        except FileNotFoundError:
            continue
        m = F > 1e-3
        gain = np.median(G[m] / F[m])
        measured = scale / gain
        if mode == "avg":
            ref = godot_average(v)  # Average is the original reduction, unchanged; v.mean() is printed below.
            print(f"{res} average: frame mean {v.mean():.6f}, original reduction {ref:.6f}")
        elif mode == "center":
            r = 0.25
            inside = np.hypot(qx - 0.5 * aspect, qy - 0.5) <= r
            a_in = math.pi * r * r / aspect
            w = np.where(inside, 0.75 / a_in, 0.25 / (1 - a_in))
            ref = (w * v).sum() / w.sum()
        elif mode == "spot":
            r = math.sqrt(ex["size"] / 100 * aspect / math.pi)
            inside = np.hypot(qx - ex["point"][0] * aspect, qy - ex["point"][1]) <= r
            ref = v[inside].mean()
        else:
            ref = np.percentile(v, ex["p"]) * 0.4
        err_ev = math.log2(measured / ref)
        rows.append((res, name, ref, measured, err_ev, scale / ref, gain))
print(
    f"{'res':10} {'case':8} {'reference lum':>14} {'measured lum':>13} {'error EV':>9} {'exp. gain':>10} {'meas. gain':>10}"
)
for row in rows:
    print(f"{row[0]:10} {row[1]:8} {row[2]:14.6f} {row[3]:13.6f} {row[4]:+9.4f} {row[5]:10.4f} {row[6]:10.4f}")
