# effect_check.py <name> <off.png> <on.png> absent|present
# absent: <on> (effect enabled but not shown) is pixel-identical to <off> (effect disabled).
# present: they differ, and the difference is the effect's: for blur, edge pixels turn gray (counted on the frame);
# for rolling, each bar's horizontal position changes from the top rows to the bottom rows (skew, in px).
import sys

import numpy as np
from PIL import Image

name, off, on, expect = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
A = np.asarray(Image.open(off).convert("L")).astype(float) / 255
B = np.asarray(Image.open(on).convert("L")).astype(float) / 255
ndiff = int((np.abs(A - B) > 0).sum())


def gray(X):
    return int(((X > 0.05) & (X < 0.95)).sum())


def skew(X):
    # Centroid x of the first bar right of center, in the top and bottom 10% of rows.
    h, w = X.shape

    def cx(rows):
        prof = X[rows].mean(axis=0)
        xs = np.where(prof > 0.5)[0]
        xs = xs[xs > w // 2]
        if len(xs) == 0:
            return float("nan")
        run = [xs[0]]
        for x in xs[1:]:
            if x != run[-1] + 1:
                break
            run.append(x)
        return float(np.mean(run))

    return cx(slice(0, h // 10)) - cx(slice(h - h // 10, h))


detail = f"{ndiff} px differ; gray edge px off/on {gray(A)}/{gray(B)}; skew off/on {skew(A):.1f}/{skew(B):.1f} px"
if expect == "absent":
    ok = ndiff == 0
else:
    ok = ndiff > 200 and (gray(B) > 3 * max(gray(A), 1) if name.startswith("blur") else abs(skew(B)) > abs(skew(A)) + 5)
print(("PASS " if ok else "FAIL ") + f"{name}: expect {expect}: {detail}")
sys.exit(0 if ok else 1)
