#!/usr/bin/env python3
"""halofit.py: the size of a glare blur from HDR captures (glare_test.gd hdr=1), by fitting its marginals.

The effect moves light: D = A - S = f (K * S - S) for halation (red channel) with S the effect-off frame and K the
chain's blur. Summing D and S over rows (or columns) gives the same relation in 1D with K's marginal, exactly, for any
separable K; so a 1D Gaussian of free sigma and center is fitted to each marginal (f solved linearly). Several points
at different sub-texel positions average out the position dependence of a mip chain's kernel. Light that the engine
clamps at the frame's edges is not modeled, so the points stay well inside the frame.

fit_pair(S, A) -> dict(sx, sy, mx, my, f, rel): sigmas and centers in pixels, the fitted fraction, and the relative
residual (|D - model| / |D|, 0 for an exact Gaussian).
"""

import json

import numpy as np
from scipy.optimize import minimize


def load(p):
    j = json.load(open(p + ".json"))
    return np.fromfile(p + ".f32", dtype=np.float32).reshape(j["h"], j["w"], 3).astype(np.float64)


def _gauss_conv(s, sig, mu, pad):
    n = len(s) + 2 * pad
    x = np.arange(n) - n // 2
    k = np.exp(-0.5 * ((x - mu) / sig) ** 2)
    k /= k.sum()
    sp = np.zeros(n)
    sp[pad : pad + len(s)] = s
    c = np.real(np.fft.ifft(np.fft.fft(sp) * np.fft.fft(np.fft.ifftshift(k))))
    return c[pad : pad + len(s)]


def fit_1d(s, d, sig0):
    pad = len(s)

    def resid(p):
        sig, mu = p
        if sig <= 0.2:
            return 1e30, 0
        m = _gauss_conv(s, sig, mu, pad) - s
        f = (m * d).sum() / (m * m).sum()
        return ((d - f * m) ** 2).sum(), f

    best = None
    for s0 in (sig0 * 0.6, sig0, sig0 * 1.6):
        r = minimize(
            lambda p: resid(p)[0],
            [max(s0, 0.5), 0.0],
            method="Nelder-Mead",
            options={"xatol": 1e-4, "fatol": 1e-14 * (d * d).sum(), "maxiter": 2000},
        )
        if best is None or r.fun < best.fun:
            best = r
    sig, mu = best.x
    e, f = resid(best.x)
    return sig, mu, f, np.sqrt(e / (d * d).sum())


def fit_pair(S3, A3, sig0, channel=0):
    S = S3[..., channel]
    D = A3[..., channel] - S
    sx, mx, fx, rx = fit_1d(S.sum(0), D.sum(0), sig0)
    sy, my, fy, ry = fit_1d(S.sum(1), D.sum(1), sig0)
    return dict(sx=sx, sy=sy, mx=mx, my=my, f=0.5 * (fx + fy), rel=max(rx, ry))


def _block(a, b):
    if b == 1:
        return a
    h, w = (a.shape[0] // b) * b, (a.shape[1] // b) * b
    return a[:h, :w].reshape(h // b, b, w // b, b).sum((1, 3))


def fit_2d(S3, A3, sig0, channel=0):
    """Task 40's metric (analyze_halo.py's sigma_fit): least squares of f (G_sigma * S - S) against D over the frame,
    centered Gaussian. Frames are block-summed by b ~ sigma / 4 first (the source and the halo alike, so the relation holds on the coarser grid
    for a blur several blocks wide; checked against b = 1), so a 4K frame fits in seconds."""
    from scipy.ndimage import gaussian_filter
    from scipy.optimize import minimize_scalar

    b = max(1, int(sig0 / 4))
    S = _block(S3[..., channel], b)
    D = _block(A3[..., channel], b) - S

    def resid(sig):
        M = gaussian_filter(S, sig, mode="constant", truncate=4.0) - S
        f = (M * D).sum() / (M * M).sum()
        return ((D - f * M) ** 2).sum(), f

    s0 = sig0 / b
    r = minimize_scalar(
        lambda s: resid(s)[0], bounds=(max(0.3, s0 * 0.3), s0 * 3), method="bounded", options={"xatol": 1e-4}
    )
    return r.x * b, resid(r.x)[1]
