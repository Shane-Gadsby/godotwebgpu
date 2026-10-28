#!/usr/bin/env python3
"""analyze_meter40.py <base_dir> <new_dir>: Average before/after the whole-frame fix, and the log-average per mode.

The metered value is read back as task 36's analyze_meter.py does: scale / (AE frame / AE-off frame). References are
computed here from the AE-off HDR frame, following the definitions in camera_attributes_storage.cpp:
- Average (new): the mean of max(R, G, B) over every pixel; (old): upstream's reduction, modeled block by block.
- Log-average: exp(sum(w log(v + 1e-4)) / sum(w)) with the mode's weights (Reinhard et al. 2002, eq. 1).
- Highlight: the percentile, unchanged by the option.
"""

import json
import math
import sys

import numpy as np

B, N = sys.argv[1], sys.argv[2]
DELTA = 1e-4


def godot_average_old(v):
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
    j = json.load(open(f"{p}.30.json"))
    return np.fromfile(f"{p}.30.f32", dtype=np.float32).reshape(j["h"], j["w"], 3).astype(np.float64)


def metered(d, name, res, F, scale=0.4):
    G = load(f"{d}/{name}_{res}")
    m = F > 1e-3
    return scale / np.median(G[m] / F[m])


def weights(mode, F, point=(0.5, 0.5), size=2.5):
    H, Wd = F.shape[:2]
    aspect = Wd / H
    yy, xx = np.mgrid[0:H, 0:Wd]
    qx, qy = (xx + 0.5) / H, (yy + 0.5) / H
    if mode == "avg":
        return np.ones((H, Wd))
    if mode == "center":
        r = 0.25
        inside = np.hypot(qx - 0.5 * aspect, qy - 0.5) <= r
        a_in = math.pi * r * r / aspect
        return np.where(inside, 0.75 / a_in, 0.25 / (1 - a_in))
    r = math.sqrt(size / 100 * aspect / math.pi)
    inside = np.hypot(qx - point[0] * aspect, qy - point[1]) <= r
    return inside.astype(np.float64)


def ev(a, b):
    return math.log2(a / b)


print(f"{'res':10} {'case':22} {'reference':>11} {'measured':>11} {'error EV':>9}")
for res in ["1920x1080", "1280x720"]:
    F = load(f"{N}/ref_{res}")
    Fb = load(f"{B}/ref_{res}")
    assert np.array_equal(F, Fb), "AE-off frames differ between base and new"
    v = F.max(axis=2)
    H, Wd = v.shape
    mean = v.mean()
    old_model = godot_average_old(v)
    seen = v[: (H // 64) * 64 if H >= 64 else H, : (Wd // 64) * 64].mean()
    old = metered(B, "avg", res, F)
    new = metered(N, "avg", res, F)
    print(
        f"{res:10} {'Average, before (base)':22} {old_model:11.6f} {old:11.6f} {ev(old, old_model):+9.4f}   (reference: upstream's reduction modeled)"
    )
    print(
        f"{res:10} {'Average, after':22} {mean:11.6f} {new:11.6f} {ev(new, mean):+9.4f}   (reference: whole-frame mean)"
    )
    print(
        f"{res:10} {'  change':22} {'':11} {'':11} {ev(new, old):+9.4f}   exposure moves {-ev(new, old):+.4f} EV; base vs the true mean {ev(old, mean):+.4f} EV"
    )
    for name, mode, kw in [
        ("logavg", "avg", {}),
        ("logcenter", "center", {}),
        ("logspot", "spot", {}),
        ("logspotwin", "spot", {"point": (0.75, 0.3), "size": 1.0}),
    ]:
        w = weights(mode, F, **kw)
        ref = math.exp((w * np.log(v + DELTA)).sum() / w.sum())
        arith = (w * v).sum() / w.sum()
        meas = metered(N, name, res, F)
        print(
            f"{res:10} {'log ' + mode + (' (0.75,0.3)' if kw else ''):22} {ref:11.6f} {meas:11.6f} {ev(meas, ref):+9.4f}   (arithmetic {arith:.6f}: log meters {ev(ref, arith):+.3f} EV)"
        )
    p99 = np.percentile(v, 99.0) * 0.4
    for name in ["hl99", "loghl99"]:
        meas = metered(N, name, res, F)
        print(f"{res:10} {name:22} {p99:11.6f} {meas:11.6f} {ev(meas, p99):+9.4f}")
