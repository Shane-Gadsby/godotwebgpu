#!/usr/bin/env python3
"""Sharpness and difference of captured Forward+ frames (task 17).

usage: sharpness.py REF_DIR WEB_DIR [ids...]   (files are <id>_<0|1>.png)

lap  = standard deviation of the 3x3 (4-neighbour) Laplacian of the luma, higher = more edge detail.
diff = mean |web - ref| over RGB, 0..255 (the harness's meanAbsDiff).
"""
import sys
import numpy as np
from PIL import Image


def load(p):
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.float64)


def lap(img):
    y = img @ np.array([0.299, 0.587, 0.114])
    l = -4 * y[1:-1, 1:-1] + y[:-2, 1:-1] + y[2:, 1:-1] + y[1:-1, :-2] + y[1:-1, 2:]
    return float(l.std())


ref, web = sys.argv[1], sys.argv[2]
ids = sys.argv[3:] or ["fsr2", "taa", "scaling_3d"]
print(f"{'frame':16} {'lap ref':>8} {'lap web':>8} {'web/ref':>8} {'diff':>7}")
for i in ids:
    for o in (0, 1):
        a, b = load(f"{ref}/{i}_{o}.png"), load(f"{web}/{i}_{o}.png")
        la, lb = lap(a), lap(b)
        print(f"{i}_{o:<14} {la:8.2f} {lb:8.2f} {lb / la:8.3f} {np.abs(a - b).mean():7.3f}")
