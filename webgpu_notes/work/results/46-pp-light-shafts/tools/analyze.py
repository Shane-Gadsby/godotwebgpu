#!/usr/bin/env python3
"""Checks light-shaft renders against the model (RendererEnvironmentStorage, environment_storage.cpp):
S(p) = int_0^L exp(-decay t) M(p + t u) dt / int_0^L exp(-decay t) dt, t in frame heights, L = min(dist, ln(1000)/decay).
The shafts are recovered as linear(on) - linear(off) of renders with the Linear tonemapper (no clipping in the region used).

  analyze.py falloff off.png on.png --sx --sy --decay --M --intensity [--fov 60] [--disk-deg 1.5]
  analyze.py model   off.png on.png --sx --sy --decay --M --intensity        (full integral over the off image's mask)
  analyze.py diff a.png b.png                                                 (max abs difference, count of differing px)
"""

import argparse
import math
import sys

import numpy as np
from PIL import Image


def srgb_to_linear(c):
    c = c / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def load(p):
    return np.asarray(Image.open(p).convert("RGB")).astype(np.float64)


def sun_uv(sx, sy, w, h, fov):
    # Camera looks down -Z, vertical fov; the sun direction (sx, sy, -1) projects to ndc (sx/(a t), sy/t).
    t = math.tan(math.radians(fov) / 2)
    a = w / h
    ndc = (sx / (a * t), sy / t)
    return ndc[0] * 0.5 + 0.5, 0.5 - ndc[1] * 0.5


def coords(w, h):
    y, x = np.mgrid[0:h, 0:w]
    u = (x + 0.5) / w
    v = (y + 0.5) / h
    return u * (w / h), v  # frame-height units


def falloff(a):
    off, on = load(a.off), load(a.on)
    h, w = off.shape[:2]
    su, sv = sun_uv(a.sx, a.sy, w, h, a.fov)
    X, Y = coords(w, h)
    D = np.hypot(X - su * w / h, Y - sv)
    r = math.tan(math.radians(a.disk_deg)) / (2 * math.tan(math.radians(a.fov) / 2))
    lin_off, lin_on = srgb_to_linear(off).mean(2), srgb_to_linear(on).mean(2)
    if a.M < 0:
        # Auto: the disk's measured radiance minus the threshold (Compatibility renders this sky shader darker).
        a.M = lin_off[D < r * 0.5].mean() - a.thr
        print(f"M from the render: {a.M:.4f}")
    diff = lin_on - lin_off
    ok = (on.max(2) < 254) & (off.max(2) < 254) & (D > r + 0.04)
    edges = np.arange(r + 0.04, 2.0, 0.02)
    rows = []
    for lo, hi in zip(edges[:-1], edges[1:]):
        m = ok & (D >= lo) & (D < hi)
        if m.sum() < 50:
            continue
        d = 0.5 * (lo + hi)
        L = min(d, math.log(1000) / a.decay)
        exp = (
            a.intensity * a.M * (math.exp(-a.decay * (d - r)) - math.exp(-a.decay * d)) / (1 - math.exp(-a.decay * L))
            if d <= L
            else 0.0
        )
        rows.append((d, diff[m].mean(), exp, m.sum()))
    rows = np.array(rows)
    # Noise: the residual around each thin ring's mean, for D in [0.1, 0.6] (8-bit quantization included).
    res = []
    for lo in np.arange(0.1, 0.6, 0.005):
        m = ok & (D >= lo) & (D < lo + 0.005)
        if m.sum() > 20:
            res.append(diff[m] - diff[m].mean())
    res = np.concatenate(res)
    print(
        f"noise: residual RMS={np.sqrt(np.mean(res**2)):.5f} (linear; one 8-bit step here is ~{(srgb_to_linear(np.array(140.0)) - srgb_to_linear(np.array(139.0))):.5f})"
    )
    sel = rows[:, 2] > 0.01

    # Fit decay: the value minimizing the squared log error of the model's shape (amplitude free).
    def shape(dd, k):
        return (np.exp(-k * (dd - r)) - np.exp(-k * dd)) / (1 - np.exp(-k * np.minimum(dd, math.log(1000) / k)))

    best = min(
        np.arange(0.5, 8.0, 0.005),
        key=lambda k: np.var(np.log(np.maximum(rows[sel, 1], 1e-6)) - np.log(shape(rows[sel, 0], k))),
    )
    amp = math.exp(np.mean(np.log(np.maximum(rows[sel, 1], 1e-6)) - np.log(shape(rows[sel, 0], best))))
    (-best, math.log(amp) - best * r)
    rel = np.abs(rows[sel, 1] - rows[sel, 2]) / rows[sel, 2]
    print(f"sun uv=({su:.4f},{sv:.4f}) disk r={r:.4f} frame heights")
    print(f"fitted decay={best:.4f} (set {a.decay}); fitted amplitude={amp:.4f} (expected {a.intensity * a.M:.4f})")
    print(f"bins={sel.sum()} relative error: median={np.median(rel):.4f} max={rel.max():.4f}")
    for d, got, exp, n in rows[sel][:: max(1, sel.sum() // 8)]:
        print(f"  D={d:.3f} got={got:.4f} model={exp:.4f}")


def model(a):
    off, on = load(a.off), load(a.on)
    h, w = off.shape[:2]
    su, sv = sun_uv(a.sx, a.sy, w, h, a.fov)
    # The mask: gray sky above the threshold keeps what exceeds it (luminance soft knee on a gray pixel); the bars are
    # black, so they mask nothing. Off render unclipped by construction.
    M = np.maximum(srgb_to_linear(off).mean(2) - a.thr, 0.0)
    # Evaluate the model on a coarse grid (every 8th pixel), against the 8x8 block means of the render.
    s = 8
    gy, gx = np.mgrid[s // 2 : h : s, s // 2 : w : s]
    X = (gx + 0.5) / h
    Y = (gy + 0.5) / h
    SX, SY = su * w / h, sv
    dx, dy = SX - X, SY - Y
    dist = np.hypot(dx, dy)
    ux, uy = dx / np.maximum(dist, 1e-9), dy / np.maximum(dist, 1e-9)
    L = np.minimum(dist, math.log(1000) / a.decay)
    n = 256
    h_box = math.log(1000) / a.decay / a.N  # The prefilter's stride: max_length / sample count.
    S = np.zeros_like(X)
    W = np.zeros_like(X)
    # The renderer's mask is the 4x4 box mean at quarter resolution, read bilinearly; do the same here.
    hq, wq = h // 4, w // 4
    Mq = M[: hq * 4, : wq * 4].reshape(hq, 4, wq, 4).mean((1, 3))

    def sample(px, py):
        (px >= 0) & (px <= w / h) & (py >= 0) & (py <= 1)
        fx = np.clip(px * h / 4 - 0.5, 0, wq - 1)
        fy = np.clip(py * h / 4 - 0.5, 0, hq - 1)
        x0 = np.floor(fx).astype(int)
        y0 = np.floor(fy).astype(int)
        x1 = np.minimum(x0 + 1, wq - 1)
        y1 = np.minimum(y0 + 1, hq - 1)
        tx = fx - x0
        ty = fy - y0
        v = (Mq[y0, x0] * (1 - tx) + Mq[y0, x1] * tx) * (1 - ty) + (Mq[y1, x0] * (1 - tx) + Mq[y1, x1] * tx) * ty
        return v  # Clamped to the frame's edge, as the sampler.

    for i in range(n):
        t = (i + 0.5) / n * L
        wt = np.exp(-a.decay * t)
        box = (
            sum(
                sample(
                    X + ux * (t + (k + 0.5) / 16 * h_box - h_box / 2), Y + uy * (t + (k + 0.5) / 16 * h_box - h_box / 2)
                )
                for k in range(16)
            )
            / 16
        )
        S += wt * box
        W += wt
    S = a.intensity * S / W
    diff = srgb_to_linear(on).mean(2) - srgb_to_linear(off).mean(2)
    blocks = diff[: (h // s) * s, : (w // s) * s].reshape(h // s, s, w // s, s).mean((1, 3))
    unclipped = (on.max(2) < 254).astype(float)[: (h // s) * s, : (w // s) * s].reshape(h // s, s, w // s, s).min((
        1,
        3,
    )) > 0
    S = S[: h // s, : w // s]
    m = unclipped & (S > 0.01)
    c = np.corrcoef(S[m], blocks[m])[0, 1]
    rel = np.sqrt(np.mean((blocks[m] - S[m]) ** 2)) / np.sqrt(np.mean(S[m] ** 2))
    print(
        f"blocks={m.sum()} correlation={c:.4f} relative RMS error={rel:.4f} mean model={S[m].mean():.4f} mean render={blocks[m].mean():.4f}"
    )
    if a.save:
        img = np.concatenate([S, blocks], 1)
        Image.fromarray((np.clip(img / max(img.max(), 1e-6), 0, 1) ** 0.5 * 255).astype(np.uint8)).save(a.save)


def diff(a):
    x, y = load(a.off), load(a.on)
    d = np.abs(x - y)
    print(f"max abs diff={d.max():.0f} differing px={(d.max(2) > 0).sum()} of {d.shape[0] * d.shape[1]}")
    sys.exit(0 if d.max() == 0 else 1)


p = argparse.ArgumentParser()
p.add_argument("cmd")
p.add_argument("off")
p.add_argument("on")
for k, v in [
    ("sx", 0.1),
    ("sy", 0.15),
    ("decay", 2.0),
    ("M", 3.2),
    ("intensity", 1.0),
    ("fov", 60.0),
    ("disk_deg", 1.5),
    ("thr", 0.5),
    ("N", 32.0),
]:
    p.add_argument("--" + k.replace("_", "-"), dest=k, type=float, default=v)
p.add_argument("--save")
a = p.parse_args()
{"falloff": falloff, "model": model, "diff": diff}[a.cmd](a)
