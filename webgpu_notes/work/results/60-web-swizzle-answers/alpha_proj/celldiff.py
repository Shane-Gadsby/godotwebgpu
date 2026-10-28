import sys

import numpy as np
from PIL import Image

# Per-cell RGB difference of web vs native (cells 0-5 = viewports; 6-7 = Texture2DRD, expected to differ by design).
nat, web = (np.asarray(Image.open(p).convert("RGB")).astype(int) for p in sys.argv[1:3])
for c in range(8):
    x0, y0 = (c % 4) * 256, (c // 4) * 256
    d = np.abs(nat[y0 : y0 + 256, x0 : x0 + 256] - web[y0 : y0 + 256, x0 : x0 + 256])
    print(f"cell {c}: max {d.max():3d}  px>1: {(d.max(axis=2) > 1).sum():6d}  mean {d.mean():.3f}")
