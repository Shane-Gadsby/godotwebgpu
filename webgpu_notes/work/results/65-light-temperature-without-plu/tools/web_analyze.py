#!/usr/bin/env python3
# ruff: noqa: UP031
"""web_analyze.py <shots dir> <native txt>: mean linear RGB of the central 21x21 px of each canvas screenshot
(8-bit sRGB decoded), the same metrics as analyze.py, and the difference from the native Forward+ run."""

import os
import re
import sys

import numpy as np
from PIL import Image

d, native = sys.argv[1], sys.argv[2]


def lin(c):
    c = c / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


nat = {}
for line in open(native):
    m = re.match(r"\[LT\] (\S+) ([-\d.]+) ([-\d.]+) ([-\d.]+) fmt", line)
    if m:
        nat[m.group(1)] = np.array([float(x) for x in m.groups()[1:]])
web = {}
for f in sorted(os.listdir(d)):
    if f.endswith(".png"):
        a = np.asarray(Image.open(os.path.join(d, f)).convert("RGB")).astype(float)
        h, w = a.shape[:2]
        web[f[:-4]] = lin(a[h // 2 - 10 : h // 2 + 11, w // 2 - 10 : w // 2 + 11]).reshape(-1, 3).mean(0)


def Y(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


worst = 0.0
for k, c in web.items():
    n = nat.get(k)
    rel = np.max(np.abs(c / n - 1)) * 100 if n is not None else float("nan")
    worst = max(worst, rel)
    print(
        "%-18s web %.5f %.5f %.5f  r/g %.4f b/g %.4f  Y %.5f  vs native %.3f%%"
        % (k, *c, c[0] / c[1], c[2] / c[1], Y(c), rel)
    )
ys = [Y(web[k]) for k in web if re.match(r"l65_t\d+$", k)]
if ys:
    print("constant luminance (web): spread %.3f%%" % ((max(ys) - min(ys)) / np.mean(ys) * 100))
n65 = [k for k in web if k.startswith("n65_t")]
if n65:
    r = web["n65_t6500"]
    print(
        f"neutral pairs (web): worst deviation from the 6500 K light {max(np.max(np.abs(web[k] / r - 1)) * 100 for k in n65):.3f}%"
    )
print(f"worst web vs native Forward+: {worst:.3f}%")
