import sys

import numpy as np
from PIL import Image

im = np.asarray(Image.open(sys.argv[1]).convert("RGB")).astype(int)
v = ["base", "A", "B", "C", "D", "E", "F", "G"]
for i, k in enumerate(v):
    a = im[i * 100 : i * 100 + 100, :400]
    b = im[0:100, :400]
    d = np.any(a != b, -1)
    ch = (a.max(-1) - a.min(-1)).max()
    print(f"{k}: {int(d.sum())} px differ from base, max {int(np.abs(a - b).max())}, max chroma {int(ch)}")
