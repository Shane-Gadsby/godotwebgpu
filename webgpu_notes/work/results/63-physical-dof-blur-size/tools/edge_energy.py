#!/usr/bin/env python3
# Face edge energy for the camera-sim A/B: mean squared gradient of linear luminance in a 128x160 box around the
# head marker (written by ab63.gd), and over the whole frame.
import sys

import numpy as np
from PIL import Image


def lin(v):
    v = v / 255.0
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def energy(png):
    a = lin(np.asarray(Image.open(png).convert("RGB")).astype(np.float64))
    y = a @ np.array([0.2126, 0.7152, 0.0722])
    x0, y0 = [float(v) for v in open(png[:-4] + ".txt").read().split()[:2]]
    gx = np.diff(y, axis=1)[:-1, :]
    gy = np.diff(y, axis=0)[:, :-1]
    g = gx * gx + gy * gy
    cx, cy = int(x0), int(y0)
    face = g[cy - 80 : cy + 80, cx - 64 : cx + 64].mean()
    return face, g.mean()


d = sys.argv[1]
for cfg in ["f8.0_u5.0", "f1.4_u5.0", "f1.4_u2.5"]:
    pf, pa = energy(f"{d}/practical_{cfg}.png")
    hf, ha = energy(f"{d}/physical_{cfg}.png")
    print(f"{cfg}: face practical {pf:.3e} physical {hf:.3e} ratio {hf / pf:.3f} | frame ratio {ha / pa:.3f}")
