import glob
import os
import sys

import numpy as np
from PIL import Image

pairs = []
for f in sorted(glob.glob(sys.argv[1] + "/*.png")):
    g = f.replace(sys.argv[1], sys.argv[2]).replace("/base_", "/new_")
    pairs.append((f, g))
for f, g in pairs:
    a = np.asarray(Image.open(f).convert("RGB")).astype(int)
    b = np.asarray(Image.open(g).convert("RGB")).astype(int)
    d = np.abs(a - b)
    print(
        f"{os.path.basename(g):38s} max {d.max():3d}  differing px {(d.max(axis=2) > 0).sum():7d}  mean lum {a.mean():7.3f} -> {b.mean():7.3f}"
    )
