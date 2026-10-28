#!/usr/bin/env python3
# Per-channel blur radius of a vertical white|black edge (lca_test.gd): rows 40-60% averaged, sRGB decoded,
# normalized per channel; radius = 2 x the line spread's standard deviation (a uniform disc's LSF is a semicircle,
# variance r^2/4), and the 10-90% edge width.
import sys

import numpy as np
from PIL import Image


def srgb_to_lin(v):
    v = v / 255.0
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def measure(path, win=90):
    a = srgb_to_lin(np.asarray(Image.open(path).convert("RGB")).astype(np.float64))
    h, w, _ = a.shape
    prof = a[int(h * 0.4) : int(h * 0.6)].mean(axis=0)  # (w, 3)
    c = w // 2
    res = []
    for ch in range(3):
        p = prof[c - win : c + win, ch]
        hi = prof[c - win - 40 : c - win, ch].mean()
        lo = prof[c + win : c + win + 40, ch].mean()
        e = (p - lo) / (hi - lo)  # 1 on the white (left) side
        lsf = -np.diff(e)
        x = np.arange(len(lsf)) + 0.5
        m = (lsf * x).sum() / lsf.sum()
        var = (lsf * (x - m) ** 2).sum() / lsf.sum()

        # 10-90% width
        def cross(t):
            i = np.argmax(e < t)
            return i - 1 + (e[i - 1] - t) / (e[i - 1] - e[i])

        res.append(dict(r=2 * np.sqrt(var), center=m - win, w1090=cross(0.1) - cross(0.9), hi=hi, lo=lo))
    return res


if __name__ == "__main__":
    for p in sys.argv[1:]:
        r = measure(p)
        print(
            p.split("/")[-1],
            " ".join(
                "{} r={:.2f} c={:+.2f} w={:.2f}".format(n, d["r"], d["center"], d["w1090"]) for n, d in zip("RGB", r)
            ),
        )
