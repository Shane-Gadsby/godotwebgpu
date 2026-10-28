#!/usr/bin/env python3
# analyze.py <off.raw> <on.raw> <elev_deg> <opacity> [size=512] [normal=1]
# Compares the measured micro-shadow factor (on / off, linear) with HDRP's ComputeMicroShadowing() evaluated
# independently in numpy from the test material (AO gradient along x, normal tilted -60..60 deg along y, both 8-bit).
import os
import sys

import numpy as np

off_p, on_p, elev, opacity = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
S = int(sys.argv[5]) if len(sys.argv) > 5 else 512
use_normal = (sys.argv[6] if len(sys.argv) > 6 else "1") == "1"
NT = 256
TILT_MAX = 60.0


SRGB = os.environ.get("SRGB", "0") == "1"  # Compatibility's HDR 2D viewport holds sRGB-encoded values


def load(p):
    a = np.fromfile(p, dtype=np.float32).reshape(S, S, 4)[..., 0].astype(np.float64)
    if SRGB:
        a = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
    return a


off, on = load(off_p), load(on_p)

px = np.arange(S)
tex = px * NT // S  # nearest texel under each pixel center
q = lambda v: np.round(v * 255.0) / 255.0  # 8-bit texture storage
ao = q((tex + 0.5) / NT)  # along x (columns)
t = np.deg2rad(-TILT_MAX + (tex + 0.5) / NT * 2 * TILT_MAX)
ny = q(np.sin(t) * 0.5 + 0.5) * 2 - 1 if use_normal else np.zeros(S)
nz_rec = np.sqrt(np.clip(1 - ny * ny, 0, 1))  # Godot rebuilds z from xy
e = np.deg2rad(elev)

best = None
for sgn in (1, -1):  # which way the binormal points / the rows run: pick the one the off image agrees with
    for flip in (False, True):
        nyy = ny[::-1] if flip else ny
        ndl = nz_rec[::-1 if flip else 1] * np.sin(e) + sgn * nyy * np.cos(e)  # rows
        ndl_img = np.repeat(ndl[:, None], S, axis=1)
        m = off > 0.02
        k = np.median(off[m] / np.maximum(ndl_img[m], 1e-4))
        err = np.abs(off - k * np.clip(ndl_img, 0, None)).mean()
        if best is None or err < best[0]:
            best = (err, sgn, flip, ndl_img, k)
err_off, sgn, flip, ndl_img, k = best
m = off > 0.02  # lit pixels (the factor is undefined where there is no direct light)
meas = on[m] / off[m]
res = []
for flipx in (False, True):  # which way u runs across the screen
    ao_img = np.repeat((ao[::-1] if flipx else ao)[None, :], S, axis=0)
    aperture = 2 * ao_img * ao_img
    model = np.where(ndl_img < 0, 1.0, 1.0 + opacity * (np.clip(ndl_img + aperture - 1, 0, 1) - 1.0))
    res.append((np.abs(meas - model[m]).mean(), flipx, model))
_, flipx, model = min(res, key=lambda r: r[0])
d = meas - model[m]
print(f"elev {elev:5.1f} opacity {opacity:.2f}: off = {k:.4f} * N.L (mean err {err_off:.4f}, sign {sgn}, flip {flip}, flipx {flipx}); "
      f"lit px {m.sum()}; factor err mean {np.abs(d).mean():.5f} p99 {np.percentile(np.abs(d), 99):.5f} "
      f"max {np.abs(d).max():.5f}; mean factor measured {meas.mean():.4f} model {model[m].mean():.4f}; "
      f"shadowed frac (<0.5) {np.mean(meas < 0.5):.3f}")
