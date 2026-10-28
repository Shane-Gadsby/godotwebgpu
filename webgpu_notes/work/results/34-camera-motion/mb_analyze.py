#!/usr/bin/env python3
"""Task 34: measure motion blur length and rolling-shutter skew in captured frames against analytic references.

blur <png> <expected_L_px>: fits the averaged horizontal profile of the moving 0.2H-wide white square (linear light)
    with A * (square convolved with a centered box of length L), L free, and reports the error with L fixed to the
    expected value. A box of length L is the exact image of a constant-speed object under an ideal shutter.
skew <png> <expected_px>: tracks the bar nearest the frame center row by row and fits x(y); reports the horizontal
    shift from the top row to the bottom row (x_top - x_bottom) against the expected readout * screen speed.
"""

import json
import sys

import numpy as np
from PIL import Image


def load_linear(path):
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64) / 255.0
    lin = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
    return lin.mean(axis=2)


def model(xs, c, w, L):
    lo, hi = c - w / 2, c + w / 2
    if L < 1e-6:
        return ((xs >= lo) & (xs < hi)).astype(float)
    a = np.clip(np.minimum(xs + L / 2, hi) - np.maximum(xs - L / 2, lo), 0, None)
    return a / L


def fit(prof, w, Ls, cs):
    xs = np.arange(prof.size) + 0.5
    best = None
    for L in Ls:
        for c in cs:
            m = model(xs, c, w, L)
            A = (m * prof).sum() / max((m * m).sum(), 1e-12)
            e = float(((prof - A * m) ** 2).mean())
            if best is None or e < best[0]:
                best = (e, float(c), float(L), float(A))
    return best


def blur(path, expected):
    img = load_linear(path)
    H, W = img.shape
    prof = img[int(H * 0.46) : int(H * 0.54)].mean(axis=0)
    w = 0.2 * H
    xs = np.arange(prof.size) + 0.5
    c0 = (prof * xs).sum() / max(prof.sum(), 1e-9)
    coarse = fit(prof, w, np.arange(0.0, 2 * w, 0.5), np.arange(c0 - 4, c0 + 4, 0.1))
    fine = fit(
        prof,
        w,
        np.arange(max(coarse[2] - 0.6, 0), coarse[2] + 0.6, 0.02),
        np.arange(coarse[1] - 0.3, coarse[1] + 0.3, 0.02),
    )
    fixed = fit(prof, w, [expected], np.arange(c0 - 4, c0 + 4, 0.02))
    return {
        "file": path.split("/")[-1],
        "size": [W, H],
        "expected_L_px": round(expected, 2),
        "fit_L_px": round(fine[2], 2),
        "fit_L_over_expected": round(fine[2] / expected, 4) if expected else None,
        "fit_amplitude": round(fine[3], 4),
        "fit_rms": round(fine[0] ** 0.5, 5),
        "rms_with_expected_L": round(fixed[0] ** 0.5, 5),
        "L_over_H": round(fine[2] / H, 5),
    }


def skew(path, expected, fov_deg=None):
    img = load_linear(path)
    H, W = img.shape
    mid = img[H // 2]
    lo0, hi0 = W // 2 - W // 6, W // 2 + W // 6
    c0 = int(np.argmax(np.convolve(mid, np.ones(5), "same")[lo0:hi0])) + lo0
    found = {}
    for rows in (range(H // 2, int(H * 0.97)), range(H // 2 - 1, int(H * 0.03), -1)):
        guess = float(c0)
        for y in rows:
            lo, hi = max(int(guess) - 12, 0), min(int(guess) + 13, W)
            seg = img[y, lo:hi]
            if seg.max() < 0.2:
                continue
            wts = np.clip(seg - 0.1, 0, None)
            x = (wts * (np.arange(lo, hi) + 0.5)).sum() / wts.sum()
            found[y] = x
            guess = x
    ys = np.array(sorted(found), dtype=float) + 0.5
    xs = np.array([found[y] for y in sorted(found)])
    k, b = np.polyfit(ys, xs, 1)
    resid = xs - (k * ys + b)
    shift = -k * H
    x_mid = k * (H / 2) + b - W / 2  # The bar's offset from the frame center at the middle row.
    if fov_deg:
        # Under a yaw at rate w, content at offset x moves at w f (1 + x^2 / f^2): correct the center-column expectation.
        f = (H / 2) / np.tan(np.radians(fov_deg / 2))
        expected *= 1 + (x_mid / f) ** 2
    return {
        "bar_offset_px": round(float(x_mid), 2),
        "file": path.split("/")[-1],
        "size": [W, H],
        "rows": int(ys.size),
        "x_top_minus_bottom_px": round(float(shift), 3),
        "expected_px": round(expected, 3),
        "ratio": round(float(shift / expected), 4) if expected else None,
        "line_fit_resid_rms_px": round(float(np.sqrt((resid**2).mean())), 3),
        "skew_angle_deg": round(float(np.degrees(np.arctan(-k))), 3),
        "shift_over_H": round(float(shift / H), 5),
    }


if __name__ == "__main__":
    kind, path, exp = sys.argv[1], sys.argv[2], float(sys.argv[3])
    print(
        json.dumps(
            blur(path, exp) if kind == "blur" else skew(path, exp, float(sys.argv[4]) if len(sys.argv) > 4 else None)
        )
    )
