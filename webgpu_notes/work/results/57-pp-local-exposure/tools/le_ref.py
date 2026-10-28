#!/usr/bin/env python3
"""le_ref.py: an independent numpy implementation of task 57's local exposure model (camera_attributes_storage.cpp,
"Local exposure"), written from the model's definition, in float64, with none of the GPU's shortcuts (no fixed point,
no half floats, exact trilinear weights).

As a module: model_factor(exposed, params) -> per-pixel exposure factor 2^(new - L) for the exposed HDR image exposed (H x W x 3).
As a script:  le_ref.py <off.f16> <on.f16> [key=value ...]  compares a capture with local exposure on against the
model applied to the capture with it off, and prints the error in EV (log2 of the luminance ratio).
Keys: hc, sc, detail, blend, kernel, bias (defaults as CameraAttributes').
"""

import json
import math
import os
import sys

import numpy as np

ROWS = 16  # Cells down the frame.
DEPTH = 32  # Range bins of 1 EV.
LOG2_MIN = -16.0
GRAY = 0.18
REC709 = np.array([0.2126, 0.7152, 0.0722])

DEFAULTS = {"hc": 0.6, "sc": 0.8, "detail": 1.0, "blend": 0.6, "kernel": 50.0, "bias": 0.0}


def load_f16(path):
    meta = json.load(open(os.path.splitext(path)[0] + ".json"))
    w, h = meta["w"], meta["h"]
    data = np.fromfile(path, dtype=np.float16).astype(np.float64)
    return data.reshape(h, w, data.size // (w * h))[:, :, :3]


def log_luminance(exposed):
    return np.log2(np.maximum(exposed @ REC709, 1e-10) / GRAY)


def build_grid(L, kernel):
    """Bilateral grid of L: raw (sum, count) per (cx, cy, bin), then blurred; plus the blurred-luminance plane."""
    H, W = L.shape
    cell = H / ROWS
    gw = -(-W * ROWS // H)  # ceil
    gh = ROWS
    ys, xs = np.mgrid[0:H, 0:W]
    cx = np.floor((xs + 0.5) / cell).astype(int)
    cy = np.floor((ys + 0.5) / cell).astype(int)
    z = np.clip(L - LOG2_MIN, 0.0, DEPTH)
    b = np.minimum(np.floor(z).astype(int), DEPTH - 1)
    lclamped = z + LOG2_MIN
    s = np.zeros((gh, gw, DEPTH))
    c = np.zeros((gh, gw, DEPTH))
    np.add.at(s, (cy, cx, b), lclamped)
    np.add.at(c, (cy, cx, b), 1.0)

    # Gaussian of one cell: binomial [1 4 6 4 1] / 16 along each axis, zero outside the grid.
    k = np.array([1, 4, 6, 4, 1], float) / 16.0

    def conv_axis(a, axis):
        out = np.zeros_like(a)
        n = a.shape[axis]
        for d in range(-2, 3):
            src = [slice(None)] * 3
            dst = [slice(None)] * 3
            if d < 0:
                src[axis] = slice(0, n + d)
                dst[axis] = slice(-d, n)
            else:
                src[axis] = slice(d, n)
                dst[axis] = slice(0, n - d)
            out[tuple(dst)] += k[d + 2] * a[tuple(src)]
        return out

    sb, cb = s, c
    for ax in range(3):
        sb = conv_axis(sb, ax)
        cb = conv_axis(cb, ax)

    # Blurred luminance: a 2D Gaussian (sigma from the kernel's +-3 sigma = kernel % of the frame width) of the
    # per-cell totals, truncated at ceil(3 sigma) cells, zero outside.
    st = s.sum(axis=2)
    ct = c.sum(axis=2)
    sigma = max(kernel * 0.01 * W / 6.0 / cell, 0.05)
    r = min(int(math.ceil(3.0 * sigma)), max(gw, gh))
    sp = np.pad(st, r)
    cp = np.pad(ct, r)
    sbl = np.zeros_like(st)
    cbl = np.zeros_like(ct)
    total = 0.0
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            w = math.exp(-(dx * dx + dy * dy) / (2.0 * sigma * sigma))
            total += w
            sbl += w * sp[r + dy : r + dy + gh, r + dx : r + dx + gw]
            cbl += w * cp[r + dy : r + dy + gh, r + dx : r + dx + gw]
    return sb, cb, sbl / total, cbl / total, cell


def lerp_index(f, n):
    """Texel-centered linear interpolation with clamp-to-edge: indices i0, i1 and weight of i1."""
    f = f - 0.5
    i0 = np.floor(f)
    t = f - i0
    i0 = i0.astype(int)
    return np.clip(i0, 0, n - 1), np.clip(i0 + 1, 0, n - 1), t


def model_factor(exposed, p):
    L = log_luminance(exposed)
    H, W = L.shape
    sb, cb, sbl, cbl, cell = build_grid(L, p["kernel"])
    gh, gw, _ = sb.shape
    ys, xs = np.mgrid[0:H, 0:W]
    x0, x1, tx = lerp_index((xs + 0.5) / cell, gw)
    y0, y1, ty = lerp_index((ys + 0.5) / cell, gh)
    zc = np.clip(L - LOG2_MIN, 0.5, DEPTH - 0.5)
    z0, z1, tz = lerp_index(zc, DEPTH)

    def tri(a):
        out = 0.0
        for yi, wy in ((y0, 1 - ty), (y1, ty)):
            for xi, wx in ((x0, 1 - tx), (x1, tx)):
                for zi, wz in ((z0, 1 - tz), (z1, tz)):
                    out = out + wy * wx * wz * a[yi, xi, zi]
        return out

    def bil(a):
        out = 0.0
        for yi, wy in ((y0, 1 - ty), (y1, ty)):
            for xi, wx in ((x0, 1 - tx), (x1, tx)):
                out = out + wy * wx * a[yi, xi]
        return out

    s, c = tri(sb), tri(cb)
    bilateral = np.where(c > 0, s / np.where(c > 0, c, 1), L)
    s2, c2 = bil(sbl), bil(cbl)
    blurred = np.where(c2 > 0, s2 / np.where(c2 > 0, c2, 1), L)
    base = bilateral + (blurred - bilateral) * p["blend"]
    mid = p["bias"]
    d = base - mid
    target = mid + d * np.where(d > 0, p["hc"], p["sc"]) + (L - base) * p["detail"]
    return np.exp2(target - L), {"L": L, "base": base, "bilateral": bilateral, "blurred": blurred}


def compare(off, on, p, mask_min=1e-4):
    factor, parts = model_factor(off, p)
    y_off = off @ REC709
    y_on = on @ REC709
    valid = (y_off > mask_min) & (y_on > mask_min)
    measured = np.log2(np.where(valid, y_on, 1) / np.where(valid, y_off, 1))
    model = np.log2(factor)
    err = np.abs(measured - model)[valid]
    return (
        {
            "pixels": int(valid.sum()),
            "model_ev_range": [float(model[valid].min()), float(model[valid].max())],
            "err_median": float(np.median(err)),
            "err_p99": float(np.percentile(err, 99)),
            "err_p999": float(np.percentile(err, 99.9)),
            "err_max": float(err.max()),
            "chroma_max": float(
                np.max(
                    np.abs(on[valid] - off[valid] * factor[valid][:, None])
                    / np.maximum(on[valid] @ REC709, 1e-3)[:, None]
                )
            ),
        },
        measured,
        model,
        valid,
    )


def parse_params(args):
    p = dict(DEFAULTS)
    for a in args:
        k, v = a.split("=")
        p[k] = float(v)
    return p


if __name__ == "__main__":
    off = load_f16(sys.argv[1])
    on = load_f16(sys.argv[2])
    p = parse_params(sys.argv[3:])
    stats, *_ = compare(off, on, p)
    print(json.dumps({"params": p, **stats}))
