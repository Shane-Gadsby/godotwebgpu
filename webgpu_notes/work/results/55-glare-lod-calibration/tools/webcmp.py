#!/usr/bin/env python3
"""webcmp.py <native dir> <web dir>: WebGPU canvas captures against native Forward+ 8-bit renders (1280x720, halves
scene: a 0.5 card on the left half, no clipping). Pixel differences, and the halation edge's fitted sigma on both
(sRGB decoded, rows summed: D = f (G * S - S) in 1D, halofit.fit_1d), against the model 0.112 mm x radius / 24 mm."""

import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from halofit import fit_1d

nd, wd = sys.argv[1:3]


def lin(p):
    c = np.asarray(Image.open(p).convert("RGB")).astype(np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


for name, r in (("halves_off", None), ("halves_r4", 4.0), ("halves_hq_r1", 1.0), ("halves_r16", 16.0)):
    n8 = np.asarray(Image.open(f"{nd}/{name}.png").convert("RGB")).astype(int)
    w8 = np.asarray(Image.open(f"{wd}/{name}.web.png").convert("RGB")).astype(int)
    d = np.abs(n8 - w8)
    line = f"{name:13s} max diff {d.max():3d}  mean {d.mean():.4f}  px>2 {(d.max(2) > 2).sum():6d}"
    if r is not None:
        model = 0.112 * r / 24.0 * 720
        out = []
        for tag, dd, suffix in (("native", nd, ".png"), ("web", wd, ".web.png")):
            S = lin(f"{dd}/halves_off{suffix}")[..., 0].sum(0)
            A = lin(f"{dd}/{name}{suffix}")[..., 0].sum(0)
            sig, mu, f, rel = fit_1d(S, A - S, model)
            out.append(f"{tag} {sig:6.2f} px")
        line += f"  | halo sigma {', '.join(out)} (model {model:.2f})"
    print(line)
