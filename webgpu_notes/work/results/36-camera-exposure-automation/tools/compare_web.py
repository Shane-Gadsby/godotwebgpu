# compare_web.py <native dir> <web dir>: WebGPU canvas captures vs the native Vulkan Forward+ renders of the same cases
# (8-bit sRGB), plus the gray card's chromaticity in the AWB cases (linearized with the sRGB EOTF).
import glob
import os
import sys

import numpy as np
from PIL import Image

nat, web = sys.argv[1], sys.argv[2]
CARD = (slice(260, 340), slice(600, 680))


def eotf(c):
    c = c / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


print(f"{'case':14} {'max':>4} {'mean':>7} {'px>2':>7}  gray card (AWB) native / web")
for wp in sorted(glob.glob(f"{web}/*.web.png")):
    name = os.path.basename(wp)[:-8]
    nps = sorted(glob.glob(f"{nat}/{name}.*.png"))
    if not nps:
        print(name, "no native capture")
        continue
    a = np.asarray(Image.open(nps[-1]).convert("RGB")).astype(int)
    b = np.asarray(Image.open(wp).convert("RGB")).astype(int)
    if a.shape != b.shape:
        print(name, "size differs", a.shape, b.shape)
        continue
    d = np.abs(a - b).max(axis=2)
    extra = ""
    if name.startswith("awb"):
        ca = eotf(a[CARD].astype(float)).mean(axis=(0, 1))
        cb = eotf(b[CARD].astype(float)).mean(axis=(0, 1))
        extra = f"  r/g {ca[0] / ca[1]:.4f} b/g {ca[2] / ca[1]:.4f} / r/g {cb[0] / cb[1]:.4f} b/g {cb[2] / cb[1]:.4f}"
    print(f"{name:14} {d.max():4d} {d.mean():7.4f} {(d > 2).sum():7d}{extra}")
