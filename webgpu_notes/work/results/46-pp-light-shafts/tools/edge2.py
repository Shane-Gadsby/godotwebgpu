import math
import sys

import numpy as np
from PIL import Image


def lin(p):
    c = np.asarray(Image.open(p).convert("RGB")).astype(float) / 255
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4).mean(2)


out_dir, rm = sys.argv[1], sys.argv[2]
for sx in ["0.9", "0.96", "1.0", "1.02", "1.04", "1.06", "1.1", "1.2", "1.3", "1.45", "1.6", "2.0"]:
    d = lin(f"{out_dir}/edge_on_{rm}_{sx}.png") - lin(f"{out_dir}/edge_off_{rm}_{sx}.png")
    u = 0.5 + float(sx) / (1.7778 * math.tan(math.radians(30))) * 0.5
    o = max(u - 1, 0) * 1.7778
    t = min(o / 0.5, 1)
    fade = 1 - t * t * (3 - 2 * t)
    print(f"sx={sx} u={u:.3f} outside={o:.3f} fade={fade:.3f} shaft energy={d.sum():9.1f} max={d.max():.3f}")
