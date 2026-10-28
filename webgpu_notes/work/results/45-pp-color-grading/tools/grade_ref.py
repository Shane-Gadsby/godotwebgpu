#!/usr/bin/env python3
"""Independent numpy reference for the Environment color grading (task 45) and checker for rendered charts.

Model as documented in servers/rendering/storage/color_grading.cpp (URP LutBuilderLdr order). Written from the
documentation, not from the C++: float64, vectorized.

usage: grade_ref.py <png> <case> <chart: lattice|off> [--floor <png of the off case>]
Prints, over the chart's patches: max/mean |rendered - LUT model| and |rendered - analytic| in 8-bit units.
"""

import sys

import numpy as np
from PIL import Image

LUMA = np.array([0.2126, 0.7152, 0.0722])
N = 33


def s2l(v):
    v = np.asarray(v, dtype=np.float64)
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def l2s(v):
    v = np.maximum(np.asarray(v, dtype=np.float64), 0.0)
    return np.where(v <= 0.0031308, v * 12.92, 1.055 * v ** (1 / 2.4) - 0.055)


def smoothstep(a, b, x):
    if b <= a:
        return np.where(x < a, 0.0, 1.0)
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def soft_light(base, blend):
    base = np.maximum(base, 0)
    r1 = 2 * base * blend + base * base * (1 - 2 * blend)
    r2 = np.sqrt(base) * (2 * blend - 1) + 2 * base * (1 - blend)
    return np.where(blend < 0.5, r1, r2)


def rgb_to_hsv(c):
    mx = c.max(-1)
    mn = c.min(-1)
    d = mx - mn
    r, g, b = c[..., 0], c[..., 1], c[..., 2]
    with np.errstate(invalid="ignore", divide="ignore"):
        h = np.where(mx == r, (g - b) / d, np.where(mx == g, 2 + (b - r) / d, 4 + (r - g) / d))
        h = np.where(d == 0, 0, h) / 6.0
        h = np.where(h < 0, h + 1, h)
        s = np.where(mx == 0, 0, d / mx)
    return h, s, mx


def hsv_to_rgb(h, s, v):
    i = np.floor(h * 6)
    f = h * 6 - i
    p = v * (1 - s)
    q = v * (1 - f * s)
    t = v * (1 - (1 - f) * s)
    i = i.astype(int) % 6
    out = np.stack(
        [np.choose(i, [v, q, p, p, t, v]), np.choose(i, [t, v, v, q, p, p]), np.choose(i, [p, p, t, v, v, q])], -1
    )
    return np.where((s == 0)[..., None], np.stack([v, v, v], -1), out)


def curve_samples(points):
    # Linear-tangent Curve points sampled the way the scene does it: 256 samples over 0..1.
    xs = np.array([p[0] for p in points])
    ys = np.array([p[1] for p in points])
    return np.interp(np.linspace(0, 1, 256), xs, ys)


def sample_curve(samples, x):
    return np.interp(np.clip(x, 0, 1), np.linspace(0, 1, len(samples)), samples)


DEFAULT = dict(
    color_filter=(1, 1, 1),
    hue_shift=0.0,
    vibrance=0.0,
    lift=(0, 0, 0),
    gamma=(1, 1, 1),
    gain=(1, 1, 1),
    shadows=(1, 1, 1),
    midtones=(1, 1, 1),
    highlights=(1, 1, 1),
    shadows_start=0.0,
    shadows_end=0.3,
    highlights_start=0.55,
    highlights_end=1.0,
    mixer=((1, 0, 0), (0, 1, 0), (0, 0, 1)),
    split_shadows=(0.5, 0.5, 0.5),
    split_highlights=(0.5, 0.5, 0.5),
    split_balance=0.0,
    curves={},
)


def grade(p, c):
    lin = s2l(c)
    lin = lin * s2l(np.array(p["color_filter"]))
    lin = lin @ np.array(p["mixer"], dtype=np.float64).T
    luma = lin @ LUMA
    fs = 1 - smoothstep(p["shadows_start"], p["shadows_end"], luma)
    fh = smoothstep(p["highlights_start"], p["highlights_end"], luma)
    fm = 1 - fs - fh
    mult = (
        fs[..., None] * s2l(np.array(p["shadows"]))
        + fm[..., None] * s2l(np.array(p["midtones"]))
        + fh[..., None] * s2l(np.array(p["highlights"]))
    )
    lin = lin * mult
    c = l2s(lin)
    lift, gamma, gain = (np.array(p[k], dtype=np.float64) for k in ("lift", "gamma", "gain"))
    c = np.maximum(gain * (c + lift * (1 - c)), 0)
    c = c ** (1 / np.maximum(gamma, 0.001))
    if tuple(p["split_shadows"]) != (0.5, 0.5, 0.5) or tuple(p["split_highlights"]) != (0.5, 0.5, 0.5):
        t = np.clip(np.clip(c, 0, 1) @ LUMA + p["split_balance"], 0, 1)[..., None]
        c = soft_light(c, 0.5 + (np.array(p["split_shadows"]) - 0.5) * (1 - t))
        c = soft_light(c, 0.5 + (np.array(p["split_highlights"]) - 0.5) * t)
    cv = p["curves"]
    if p["hue_shift"] != 0 or any(k in cv for k in ("hue_vs_hue", "hue_vs_sat", "sat_vs_sat", "lum_vs_sat")):
        h, s, v = rgb_to_hsv(np.clip(c, 0, 1))
        lum = c @ LUMA
        nh = h + p["hue_shift"] / 360.0
        if "hue_vs_hue" in cv:
            nh = nh + sample_curve(cv["hue_vs_hue"], h) - 0.5
        changed = nh != h
        nh = nh - np.floor(nh)
        c = np.where(changed[..., None], hsv_to_rgb(nh, s, v), c)
        sm = np.ones_like(h)
        if "hue_vs_sat" in cv:
            sm = sm * 2 * sample_curve(cv["hue_vs_sat"], h)
        if "sat_vs_sat" in cv:
            sm = sm * 2 * sample_curve(cv["sat_vs_sat"], s)
        if "lum_vs_sat" in cv:
            sm = sm * 2 * sample_curve(cv["lum_vs_sat"], lum)
        lum_c = (c @ LUMA)[..., None]
        c = lum_c + sm[..., None] * (c - lum_c)
    if p["vibrance"] != 0:
        lum_c = (c @ LUMA)[..., None]
        sat = (c.max(-1) - c.min(-1))[..., None]
        amount = np.maximum(1 + p["vibrance"] * (1 - np.sign(p["vibrance"]) * sat), 0)
        c = lum_c + amount * (c - lum_c)
    for i, ch in enumerate(("red", "green", "blue")):
        if "master" in cv:
            c[..., i] = sample_curve(cv["master"], c[..., i])
        if ch in cv:
            c[..., i] = sample_curve(cv[ch], c[..., i])
    return np.clip(c, 0, 1)


# The user LUTs of cases.gd, as 8-bit textures.
def user_lut_3d():
    n = 17
    b, g, r = np.meshgrid(*(np.arange(n),) * 3, indexing="ij")
    x = np.stack([(r + 0.5) / n, (g + 0.5) / n, (b + 0.5) / n], -1)
    v = np.stack([x[..., 1] ** 0.8, x[..., 2] ** 1.2, x[..., 0]], -1)
    return np.round(v * 255) / 255  # [b, g, r, ch]


def user_lut_1d():
    x = (np.arange(256) + 0.5) / 256
    return np.round(np.stack([x * x, np.sqrt(x), 1 - x], -1) * 255) / 255


def trilinear(lut, coord):
    # lut [z(b), y(g), x(r), ch]; coord in texture space 0..1 (r, g, b), clamp-to-edge, texel centers at (i+0.5)/n.
    n = lut.shape[0]
    t = np.clip(coord * n - 0.5, 0, n - 1)
    i0 = np.minimum(np.floor(t).astype(int), n - 2)
    f = t - i0
    out = 0
    for dz in (0, 1):
        for dy in (0, 1):
            for dx in (0, 1):
                w = (
                    (f[..., 0] if dx else 1 - f[..., 0])
                    * (f[..., 1] if dy else 1 - f[..., 1])
                    * (f[..., 2] if dz else 1 - f[..., 2])
                )
                out = out + w[..., None] * lut[i0[..., 2] + dz, i0[..., 1] + dy, i0[..., 0] + dx]
    return out


def linear1d(lut, x):
    n = lut.shape[0]
    t = np.clip(x * n - 0.5, 0, n - 1)
    i0 = np.minimum(np.floor(t).astype(int), n - 2)
    f = t - i0
    return lut[i0] * (1 - f) + lut[i0 + 1] * f


def case_params(name):
    p = {k: (dict(v) if isinstance(v, dict) else v) for k, v in DEFAULT.items()}
    user = None
    bcs = None

    def lin(pts):
        return curve_samples(pts)

    if name == "filter":
        p["color_filter"] = (1.0, 0.8, 0.55)
    elif name == "hue_shift":
        p["hue_shift"] = 60.0
    elif name == "vibrance":
        p["vibrance"] = 0.6
    elif name == "vibrance_neg":
        p["vibrance"] = -0.7
    elif name == "vibrance_min":
        p["vibrance"] = -1.0
    elif name == "lift":
        p["lift"] = (0.1, 0.0, 0.2)
    elif name == "gamma":
        p["gamma"] = (1.5, 1.0, 0.7)
    elif name == "gain":
        p["gain"] = (1.2, 0.9, 1.0)
    elif name == "smh":
        p.update(
            shadows=(0.6, 0.8, 1.4),
            midtones=(1.1, 1.0, 0.9),
            highlights=(1.3, 1.1, 0.7),
            shadows_end=0.25,
            highlights_start=0.4,
        )
    elif name == "mixer":
        p["mixer"] = ((0.8, 0.3, -0.1), (0.1, 0.7, 0.2), (0.0, 0.4, 0.6))
    elif name == "split":
        p.update(split_shadows=(0.2, 0.4, 0.9), split_highlights=(0.9, 0.6, 0.2), split_balance=0.1)
    elif name == "curve_master":
        p["curves"]["master"] = lin([(0, 0.05), (0.3, 0.2), (0.7, 0.85), (1, 0.95)])
    elif name == "curve_rgb":
        p["curves"].update(
            red=lin([(0, 0), (0.5, 0.65), (1, 1)]),
            green=lin([(0, 0.1), (1, 0.9)]),
            blue=lin([(0, 0), (0.5, 0.35), (1, 1)]),
        )
    elif name == "hue_vs_hue":
        p["curves"]["hue_vs_hue"] = lin([(0, 0.5), (0.33, 0.6), (0.66, 0.4), (1, 0.5)])
    elif name == "hue_vs_sat":
        p["curves"]["hue_vs_sat"] = lin([(0, 0.25), (0.5, 0.75), (1, 0.25)])
    elif name == "sat_vs_sat":
        p["curves"]["sat_vs_sat"] = lin([(0, 0.5), (1, 0.2)])
    elif name == "lum_vs_sat":
        p["curves"]["lum_vs_sat"] = lin([(0, 0.0), (0.5, 0.5), (1, 0.9)])
    elif name.startswith("all"):
        p.update(
            color_filter=(1.0, 0.95, 0.85),
            hue_shift=-10.0,
            vibrance=0.3,
            lift=(0.02, 0.0, 0.04),
            gamma=(1.1, 1.0, 0.95),
            gain=(1.05, 1.0, 0.95),
            shadows=(0.9, 0.95, 1.1),
            highlights=(1.1, 1.0, 0.9),
            mixer=((0.9, 0.1, 0.0), (0, 1, 0), (0, 0, 1)),
            split_shadows=(0.4, 0.45, 0.6),
            split_highlights=(0.6, 0.55, 0.4),
        )
        p["curves"].update(
            master=lin([(0, 0.02), (0.25, 0.2), (0.75, 0.82), (1, 0.98)]), sat_vs_sat=lin([(0, 0.55), (1, 0.45)])
        )
        if name in ("all_user3d", "all_adjust"):
            user = "3d"
        if name == "all_user1d":
            user = "1d"
        if name == "all_adjust":
            bcs = (1.1, 1.1, 0.8)
    elif name in ("neutral", "off"):
        pass
    elif name == "user3d_only":
        return None, "3d", None
    elif name == "user1d_only":
        return None, "1d", None
    else:
        raise SystemExit("unknown case " + name)
    return (None if name == "off" else p), user, bcs


def apply_user(user, c):
    if user == "3d":
        return trilinear(user_lut_3d(), np.clip(c, 0, 1))
    lut = user_lut_1d()
    c = np.clip(c, 0, 1)
    return np.stack([linear1d(lut[:, i], c[..., i]) for i in range(3)], -1)


def pipeline(name, c, analytic):
    """The whole post chain from the chart color (sRGB-encoded, already through the linear tonemapper)."""
    p, user, bcs = case_params(name)
    if bcs is not None:
        br = s2l(bcs[0])
        c = l2s(s2l(c) * br)
        c = 0.5 + (c - 0.5) * bcs[1]
        avg = c.mean(-1, keepdims=True)
        c = avg + (c - avg) * bcs[2]
    if p is None:
        return apply_user(user, c) if user else c
    c = np.clip(c, 0, 1)
    if analytic:
        g = grade(p, c)
        return apply_user(user, g) if user else g
    # The LUT: lattice points, half floats, composed with the user LUT, then sampled at lattice-remapped coordinates.
    b, gg, r = np.meshgrid(*(np.arange(N) / (N - 1),) * 3, indexing="ij")
    lat = np.stack([r, gg, b], -1)
    lut = grade(p, lat).astype(np.float16).astype(np.float64)
    if user:
        lut = apply_user(user, lut).astype(np.float16).astype(np.float64)
    return trilinear(lut, c * (N - 1) / N + 0.5 / N)


def chart_colors(chart):
    levels, side, offset = (8, 23, 0.5) if chart == "off" else (9, 27, 0.0)
    cols = []
    for idx in range(levels**3):
        cols.append((
            (idx % levels + offset) / 8,
            ((idx // levels) % levels + offset) / 8,
            (idx // levels**2 + offset) / 8,
        ))
    return np.array(cols), side


def read_patches(png, chart):
    img = np.asarray(Image.open(png).convert("RGB")).astype(np.float64)
    cols, side = chart_colors(chart)
    h, w = img.shape[:2]
    out = []
    for idx in range(len(cols)):
        x, y = idx % side, idx // side
        px, py = int((x + 0.5) * w / side), int((y + 0.5) * h / side)
        out.append(np.median(img[py - 1 : py + 2, px - 1 : px + 2].reshape(-1, 3), 0))
    return cols, np.array(out)


def main():
    png, name, chart = sys.argv[1], sys.argv[2], sys.argv[3]
    cols, got = read_patches(png, chart)
    lin_out = "--linear-output" in sys.argv  # Compatibility's viewport image holds linear values
    enc = (lambda v: s2l(v)) if lin_out else (lambda v: v)
    lut_model = np.round(enc(pipeline(name, cols.copy(), False)) * 255)
    analytic = enc(pipeline(name, cols.copy(), True)) * 255
    d_lut = np.abs(got - lut_model)
    d_an = np.abs(got - analytic)
    print(
        f"{name:13s} {chart:7s} vs LUT model: max {d_lut.max():5.2f} mean {d_lut.mean():5.3f}  "
        f"vs analytic: max {d_an.max():5.2f} mean {d_an.mean():5.3f}  (/255, {len(cols)} patches)"
    )
    if "--worst" in sys.argv:
        i = np.argmax(d_lut.max(-1))
        print("  worst", cols[i], "got", got[i], "lut", lut_model[i], "analytic", analytic[i])


if __name__ == "__main__":
    main()
