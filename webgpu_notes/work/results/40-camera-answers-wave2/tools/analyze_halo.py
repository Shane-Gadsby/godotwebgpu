#!/usr/bin/env python3
"""analyze_halo.py <dir> [renderers] [prefix]: halation's halo size from HDR renders (glare_test.gd hdr=1).

For each radius, the halo-on frame minus the halation-off frame is D = f (G_sigma * S - S) in red (x0.3 green, 0 blue),
S the halation-off frame (scene-linear, Linear tonemapper, HDR 2D) and G_sigma a Gaussian: the model of
camera_attributes_storage.cpp (sigma 0.112 mm x radius on a 24 mm frame, f = min(0.08 strength, 0.95)). Reported:
- sigma_fit: the Gaussian sigma whose model best matches D (least squares over the frame, f free): the halo's
  real size with the source's own disc removed;
- sigma_tail: task 35's measure, a Gaussian fitted to log(red) beyond the disc (includes the disc's width).
"""
import json, math, os, sys
import numpy as np
from scipy.ndimage import gaussian_filter
from scipy.optimize import minimize_scalar

O = sys.argv[1]
RMS = sys.argv[2].split(",") if len(sys.argv) > 2 else ["forward_plus"]
TAG = sys.argv[3] if len(sys.argv) > 3 else "halation"


def load(name):
    p = os.path.join(O, name)
    j = json.load(open(p + ".json"))
    return np.fromfile(p + ".f32", dtype=np.float32).reshape(j["h"], j["w"], 3).astype(np.float64)


def fit_sigma(S, D):
    def resid(sig):
        M = gaussian_filter(S, sig, mode="constant") - S
        f = (M * D).sum() / (M * M).sum()
        return ((D - f * M) ** 2).sum(), f
    r = minimize_scalar(lambda s: resid(s)[0], bounds=(0.3, 200), method="bounded", options={"xatol": 1e-3})
    return r.x, resid(r.x)[1]


def tail_sigma(S, D):
    H, W = S.shape
    ys, xs = np.mgrid[0:H, 0:W]
    w = S / S.sum()
    cx, cy = (xs * w).sum(), (ys * w).sum()
    rr = np.hypot(xs - cx, ys - cy)
    rdisc = rr[S > 0.5 * S.max()].max()
    sel = (rr > rdisc + 2) & (rr < 0.45 * H) & (D > 1e-4 * D.max())
    x = rr[sel] ** 2; y = np.log(D[sel]); wts = D[sel]
    A = np.vstack([np.ones_like(x), x]).T * wts[:, None]
    c = np.linalg.lstsq(A, y * wts, rcond=None)[0]
    return (math.sqrt(-1 / (2 * c[1])) if c[1] < 0 else float("nan")), rdisc


for rm in RMS:
    S3 = load(f"{TAG}_{rm}_point.png")
    S = S3[..., 0]
    H = S.shape[0]
    for hq in ["", "hq_"]:
        for r in ["0.5", "1.0", "2.0", "4.0", "16.0"]:
            name = f"{TAG}_{rm}_{hq}r{r}.png"
            if not os.path.exists(os.path.join(O, name + ".f32")):
                continue
            A = load(name)
            D = A[..., 0] - S
            model = 0.112 * float(r) / 24.0 * H
            sig, f = fit_sigma(S, D)
            ts, rdisc = tail_sigma(S, D)
            g = (A[..., 1] - S3[..., 1]); b = (A[..., 2] - S3[..., 2])
            pos = D > 0
            print(f"{rm:13s} {'HQ ' if hq else 'std'} radius x{r:>4s}: sigma_fit {sig:6.2f} px (model {model:6.2f}, ratio {sig / model:5.3f}), "
                  f"f {f:.3f} (0.95), sigma_tail {ts:6.2f} px (disc {rdisc:.1f} px), green/red {g[pos].sum() / D[pos].sum():.3f}, blue max {np.abs(b).max():.2e}")
