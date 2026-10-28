#!/usr/bin/env python3
"""analyze.py <dir> <case> [<case>...]: checks task 47's Panini renders against an independent model.

The reference is built here from the geometry in Sharpless, Ballestrem and Thomas (2010): a direction is projected
onto a unit cylinder around the vertical axis, then viewed from a point d behind the axis onto the plane touching the
cylinder straight ahead. The fit scale (no border) is found numerically, by bisection on the image of the rendered
frustum's border, not from the closed form the engine uses.
"""

import json
import math
import sys

import numpy as np
from PIL import Image
from scipy import ndimage


def panini(v, d, s):
    """Camera-space direction(s) (x right, y up, -z forward) -> Panini plane (X, Y)."""
    v = np.atleast_2d(np.asarray(v, float))
    x, y, f = v[:, 0], v[:, 1], -v[:, 2]
    phi = np.arctan2(x, f)
    h = y / np.hypot(x, f)  # cylinder height: tangent of the elevation
    k = (1.0 + d) / (np.cos(phi) + d)  # viewpoint d behind the axis, plane at distance 1 + d from it
    X = np.sin(phi) * k
    Yp = h * k
    Yr = y / f  # the rectilinear image's vertical
    return np.stack([X, Yr + s * (Yp - Yr)], 1)


def fit_scale(tx, ty, d, s):
    """Largest c with the output rectangle c*[-tx,tx]x[-ty,ty] inside the Panini image of the rendered frustum."""
    n = 4001
    t = np.linspace(-1, 1, n)
    border = np.concatenate([
        np.stack([t * tx, np.full(n, ty), -np.ones(n)], 1),
        np.stack([t * tx, np.full(n, -ty), -np.ones(n)], 1),
        np.stack([np.full(n, tx), t * ty, -np.ones(n)], 1),
        np.stack([np.full(n, -tx), t * ty, -np.ones(n)], 1),
    ])
    img = panini(border, d, s)
    # A point of the rectangle is outside when it is beyond the border image: for the top/bottom edges compare |Y| at
    # the same X, for the sides |X|. Bisection on c with the rectangle sampled densely.
    top = img[:n]
    side = img[2 * n : 3 * n]

    def inside(c):
        if c * tx > side[:, 0].min() + 1e-12:
            return False
        xs = np.linspace(-c * tx, c * tx, 801)
        ytop = np.interp(xs, top[:, 0], top[:, 1])
        return np.all(c * ty <= ytop + 1e-12)

    lo, hi = 0.0, 1.0
    for _ in range(60):
        mid = 0.5 * (lo + hi)
        lo, hi = (mid, hi) if inside(mid) else (lo, mid)
    return lo


def to_pixels(P, W, H, tx, ty, c):
    return np.stack([(P[:, 0] / (c * tx) * 0.5 + 0.5) * W, (0.5 - P[:, 1] / (c * ty) * 0.5) * H], 1)


def components(mask):
    lab, n = ndimage.label(mask)
    out = []
    for i in range(1, n + 1):
        ys, xs = np.nonzero(lab == i)
        if len(xs) < 4:
            continue
        cx, cy = xs.mean() + 0.5, ys.mean() + 0.5
        out.append((cx, cy, xs.std(), ys.std(), len(xs), xs.min(), xs.max(), ys.min(), ys.max()))
    return np.array(out)


def analyze(dirname, case):
    meta = json.load(open(f"{dirname}/{case}.json"))
    im = np.asarray(Image.open(f"{dirname}/{case}.png").convert("RGB")).astype(float) / 255.0
    H, W = im.shape[:2]
    ty = math.tan(math.radians(meta["fov"]) / 2)
    tx = ty * W / H
    if case.startswith("d"):
        d, s = (float(v) for v in case[1:].split("s"))
    else:
        d, s = 0.0, 0.0
    c = fit_scale(tx, ty, d, s) if d > 0 else 1.0
    res = {
        "case": case,
        "d": d,
        "s": s,
        "fit_scale": c,
        "closed_form_fit": (d + 1) * (1 / math.hypot(1, tx)) / (d + 1 / math.hypot(1, tx)),
    }
    dirs = np.array([q["dir"] for q in meta["dots"]])
    exp = to_pixels(panini(dirs, d, s), W, H, tx, ty, c)
    unp = np.array([q["unproject"] for q in meta["dots"]])
    blue = (im[:, :, 2] > 0.25) & (im[:, :, 0] < 0.5) & (im[:, :, 1] < 0.5)
    comps = components((im[:, :, 1] > 0.25) & (im[:, :, 0] < 0.5) & (im[:, :, 2] < 0.5))
    errs, unp_meas, unp_model, aspects = [], [], [], []
    for i, e in enumerate(exp):
        if not (12 < e[0] < W - 12 and 12 < e[1] < H - 12) or len(comps) == 0:
            continue
        # Dots the blue rod crosses or touches are skipped: their blobs merge with it.
        x0, y0 = int(e[0]), int(e[1])
        if blue[max(y0 - 12, 0) : y0 + 13, max(x0 - 12, 0) : x0 + 13].any():
            continue
        j = np.argmin(np.hypot(comps[:, 0] - e[0], comps[:, 1] - e[1]))
        m = comps[j]
        errs.append(math.hypot(m[0] - e[0], m[1] - e[1]))
        unp_meas.append(math.hypot(m[0] - unp[i][0], m[1] - unp[i][1]))
        unp_model.append(math.hypot(e[0] - unp[i][0], e[1] - unp[i][1]))
    res["dots"] = {
        "matched": len(errs),
        "max_err_px": max(errs, default=-1),
        "mean_err_px": float(np.mean(errs)) if errs else -1,
        "unproject_vs_measured_max_px": max(unp_meas, default=-1),
        "unproject_vs_model_max_px": max(unp_model, default=-1),
    }
    # Shapes: each white sphere subtends a cone of 2 degrees; its silhouette, mapped through the reference and filled,
    # has second moments that the rendered blob's must match (width over height, as a stretch measure).
    white = (im[:, :, 0] > 0.5) & (im[:, :, 1] > 0.5) & (im[:, :, 2] > 0.5)
    wcomps = components(white)
    for b in meta.get("big", []):
        v = np.asarray(b["dir"], float)
        v /= np.linalg.norm(v)
        a = np.cross(v, [0, 1, 0])
        a /= np.linalg.norm(a)
        bb = np.cross(v, a)
        t = np.linspace(0, 2 * np.pi, 721)[:-1]
        alpha = math.radians(2.0)
        sil = math.cos(alpha) * v[None, :] + math.sin(alpha) * (np.cos(t)[:, None] * a + np.sin(t)[:, None] * bb)
        poly = to_pixels(panini(sil, d, s), W, H, tx, ty, c)
        if poly[:, 0].min() < 2 or poly[:, 0].max() > W - 2:
            continue
        gx, gy = np.meshgrid(
            np.arange(poly[:, 0].min(), poly[:, 0].max(), 0.1), np.arange(poly[:, 1].min(), poly[:, 1].max(), 0.1)
        )
        px, py = gx.ravel(), gy.ravel()
        inside = np.zeros(px.shape, bool)
        x1, y1 = poly[:, 0], poly[:, 1]
        x2, y2 = np.roll(x1, -1), np.roll(y1, -1)
        for k in range(len(x1)):
            cond = ((y1[k] > py) != (y2[k] > py)) & (
                px < (x2[k] - x1[k]) * (py - y1[k]) / (y2[k] - y1[k] + 1e-30) + x1[k]
            )
            inside ^= cond
        ex, ey = px[inside].std(), py[inside].std()
        cen = np.array([px[inside].mean(), py[inside].mean()])
        j = np.argmin(np.hypot(wcomps[:, 0] - cen[0], wcomps[:, 1] - cen[1]))
        m = wcomps[j]
        aspects.append({
            "az": b["az"],
            "measured": m[2] / m[3],
            "expected": ex / ey,
            "rectilinear": 1 / math.cos(math.radians(b["az"])),
            "centroid_err_px": float(math.hypot(m[0] - cen[0], m[1] - cen[1])),
        })
    res["shapes"] = aspects
    # Poles: per row, the centroid column of each red run; a straight vertical line has a constant column.
    red = (im[:, :, 0] > 0.25) & (im[:, :, 1] < 0.5) & (im[:, :, 2] < 0.5)
    pole_dev = []
    for comp in components(red):
        x0, x1, y0, y1 = int(comp[5]), int(comp[6]), int(comp[7]), int(comp[8])
        cols = []
        for y in range(y0 + 3, y1 - 2):
            row = im[y, x0 : x1 + 1, 0] * red[y, x0 : x1 + 1]
            if row.sum() > 0:
                cols.append((np.arange(x0, x1 + 1) + 0.5) @ row / row.sum())
        if len(cols) > 50:
            cols = np.array(cols)
            pole_dev.append({
                "x": float(cols.mean()),
                "rows": len(cols),
                "peak_to_peak_px": float(np.ptp(cols)),
                "std_px": float(cols.std()),
            })
    res["poles"] = pole_dev
    # The rod: per column, the centroid row of blue vs the reference's curve for the line (x, -2.5, -6).
    blue = (im[:, :, 2] > 0.25) & (im[:, :, 0] < 0.5) & (im[:, :, 1] < 0.5)
    xs = np.linspace(-60, 60, 200001)
    curve = to_pixels(panini(np.stack([xs, np.full_like(xs, -2.5), np.full_like(xs, -6.0)], 1), d, s), W, H, tx, ty, c)
    rod_err = []
    for x in range(8, W - 8, 4):
        ey = np.interp(x + 0.5, curve[:, 0], curve[:, 1])
        # Only the rod's own run: within 25 px of the reference curve (stray blended pixels elsewhere are not the rod).
        near = np.abs(np.arange(H) + 0.5 - ey) < 25
        col = im[:, x, 2] * blue[:, x] * near
        if col.sum() <= 0 or not (8 < ey < H - 8):
            continue
        cy = (np.arange(H) + 0.5) @ col / col.sum()
        rod_err.append((x, cy, ey))
    rod_err = np.array(rod_err)
    if len(rod_err):
        res["rod"] = {
            "columns": len(rod_err),
            "max_err_px": float(np.abs(rod_err[:, 1] - rod_err[:, 2]).max()),
            "sag_center_to_edge_px": float(rod_err[:, 1].min() - rod_err[:, 1].max()),
        }
    res["roundtrip"] = meta["roundtrip"]
    return res


if __name__ == "__main__":
    for case in sys.argv[2:]:
        print(json.dumps(analyze(sys.argv[1], case), indent=1))
