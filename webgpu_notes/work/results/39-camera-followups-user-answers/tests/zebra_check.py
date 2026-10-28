# zebra_check.py diff <a.png> <b.png>             -> PASS if pixel-identical
# zebra_check.py color <base> <default> <colored> <default r,g,b> <new r,g,b>
#   The stripes are where <default> (zebras on, default colors) differs from <base> (no overlays). Checks that the
#   stripes in <default> are the default color, that <colored> has the same stripes in the new color (within 1/255),
#   and that every other pixel of <colored> equals <base>.
import sys

import numpy as np
from PIL import Image


def L(p):
    return np.asarray(Image.open(p).convert("RGB")).astype(int)


mode = sys.argv[1]
if mode == "diff":
    a, b = L(sys.argv[2]), L(sys.argv[3])
    d = np.abs(a - b).max(axis=2)
    ok = d.max() == 0
    print(("PASS" if ok else "FAIL") + f" pixel identity: max diff {d.max()}, {int((d > 0).sum())} px differ")
    sys.exit(0 if ok else 1)
base, dflt, col = L(sys.argv[2]), L(sys.argv[3]), L(sys.argv[4])
c0 = np.array([round(float(x) * 255) for x in sys.argv[5].split(",")])
c1 = np.array([round(float(x) * 255) for x in sys.argv[6].split(",")])
mask = np.abs(dflt - base).max(axis=2) > 0
n = int(mask.sum())
e_default = np.abs(dflt[mask] - c0).max() if n else 99
# Pixels of the base image that already have the default color are stripes too but show no difference: find the
# stripes in the colored image the same way and require the two masks to agree outside such pixels.
mask_c = np.abs(col - base).max(axis=2) > 0
ambiguous = (np.abs(base - c0).max(axis=2) <= 0) | (np.abs(base - c1).max(axis=2) <= 0)
mismatch = int(((mask != mask_c) & ~ambiguous).sum())
e_col = np.abs(col[mask_c] - c1).max() if mask_c.sum() else 99
ok = n > 1000 and e_default <= 1 and e_col <= 1 and mismatch == 0
print(
    ("PASS" if ok else "FAIL")
    + f" zebra color: {n} stripe px ({n / mask.size:.4f} of frame); default color max err {e_default}/255; "
    f"chosen color {tuple(c1)} max err {e_col}/255 over {int(mask_c.sum())} px; stripe masks differ at {mismatch} px; "
    f"untouched px equal base: {bool((col[~mask_c] == base[~mask_c]).all())}"
)
sys.exit(0 if ok else 1)
