#!/usr/bin/env python3
"""ident.py <pre dir> <new dir>: every PNG (and .f32 HDR capture) of the pre-change editor against the changed one."""

import os
import sys

import numpy as np
from PIL import Image

a, b = sys.argv[1:3]
bad = 0
for f in sorted(os.listdir(a)):
    if not (f.endswith(".png") or f.endswith(".f32")):
        continue
    if not os.path.exists(os.path.join(b, f)):
        print("MISSING", f)
        bad += 1
        continue
    if f.endswith(".png"):
        x = np.asarray(Image.open(os.path.join(a, f)).convert("RGBA")).astype(int)
        y = np.asarray(Image.open(os.path.join(b, f)).convert("RGBA")).astype(int)
    else:
        x = np.fromfile(os.path.join(a, f), np.float32)
        y = np.fromfile(os.path.join(b, f), np.float32)
    d = np.abs(x - y)
    n = int((d.reshape(-1, 4 if f.endswith(".png") else 1) > 0).any(1).sum())
    bad += n > 0
    print(f"{f:40s} max {d.max():10.6g}  differing px {n}")
print("IDENTICAL" if bad == 0 else f"DIFFERENT: {bad} files")
