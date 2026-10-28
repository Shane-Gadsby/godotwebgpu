#!/usr/bin/env python3
"""analyze.py <out dir> [ident|model|controls|halo|web ...]: task 57's numbers from native_job.sh's / shoot.mjs's
captures, against le_ref.py's independent model. Prints one line per check; exits non-zero if a check fails."""

import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import le_ref  # noqa: E402

OUT = sys.argv[1]
WHAT = sys.argv[2:] or ["ident", "model", "controls", "halo"]
failed = []
# Tolerance against the model, in EV: the GPU grid is half-float, the sums fixed point (1/4096 EV), the trilinear
# weights of the hardware sampler 8-bit fractions, and the captures half floats.
TOL_P99 = 0.02
TOL_MAX = 0.10


def check(label, ok, text):
    print(("ok   " if ok else "FAIL ") + label + ": " + text)
    if not ok:
        failed.append(label)


def png(path):
    return np.asarray(Image.open(path).convert("RGBA")).astype(int)


def ident():
    base = os.path.join(OUT, "ident")
    for m in ["forward_plus", "mobile", "gl_compatibility"]:
        for case in ["filmic_glow", "filmic_ae", "linear", "phys_ae"]:
            a = png(os.path.join(base, m, "pre", case + ".png"))
            b = png(os.path.join(base, m, "new", case + ".png"))
            d = np.abs(a - b)
            check(
                f"ident {m} {case}", d.max() == 0, f"max diff {d.max()}, differing pixels {(d.max(axis=2) > 0).sum()}"
            )
        if m != "forward_plus":
            a = png(os.path.join(base, m, "new", "filmic_glow.png"))
            b = png(os.path.join(base, m, "new", "filmic_glow_le_on.png"))
            d = np.abs(a - b)
            check(f"unsupported {m}: local exposure on = off", d.max() == 0, f"max diff {d.max()}")


def pair(dirname, off, on, params, label):
    off_img = le_ref.load_f16(os.path.join(dirname, off + ".f16"))
    on_img = le_ref.load_f16(os.path.join(dirname, on + ".f16"))
    p = dict(le_ref.DEFAULTS)
    p.update(params)
    stats, measured, model, valid = le_ref.compare(off_img, on_img, p)
    ok = stats["err_p99"] < TOL_P99 and stats["err_max"] < TOL_MAX
    check(
        label,
        ok,
        f"{off_img.shape[1]}x{off_img.shape[0]} model factor {stats['model_ev_range'][0]:+.2f}..{stats['model_ev_range'][1]:+.2f} EV, "
        f"|measured - model| median {stats['err_median']:.4f} p99 {stats['err_p99']:.4f} p99.9 {stats['err_p999']:.4f} max {stats['err_max']:.4f} EV, "
        f"color kept to {stats['chroma_max']:.4f}",
    )
    return off_img, on_img, measured, model, valid


def model_checks():
    for res in ["1280x720", "1920x1080", "2560x1440"]:
        d = os.path.join(OUT, "model", res)
        for lay in ["room", "chart"]:
            pair(d, f"{lay}_off", f"{lay}_on", {}, f"model {res} {lay}")
    # Resolution independence: the model's factor map at each resolution, resampled to 720p, against 1440p's.
    maps = {}
    for res in ["1280x720", "1920x1080", "2560x1440"]:
        off_img = le_ref.load_f16(os.path.join(OUT, "model", res, "room_off.f16"))
        on_img = le_ref.load_f16(os.path.join(OUT, "model", res, "room_on.f16"))
        y0 = off_img @ le_ref.REC709
        y1 = on_img @ le_ref.REC709
        f = np.log2(np.maximum(y1, 1e-6) / np.maximum(y0, 1e-6))
        h, w = f.shape
        k = h // 720
        maps[res] = f.reshape(720, k, 1280, k).mean(axis=(1, 3)) if k > 1 else f
    # 1080p does not divide to 720p by an integer; compare 720p with 1440p (2x2 means).
    d = np.abs(maps["1280x720"] - maps["2560x1440"])
    check(
        "resolution 720p vs 1440p measured factor",
        np.percentile(d, 99) < 0.05,
        f"|difference| median {np.median(d):.4f} p99 {np.percentile(d, 99):.4f} EV (edges of objects differ by pixel coverage)",
    )


CONTROLS = [
    ("identity", {"hc": 1, "sc": 1, "detail": 1}),
    ("hc0.3", {"hc": 0.3}),
    ("sc0.4", {"sc": 0.4}),
    ("detail0.5", {"detail": 0.5}),
    ("detail2", {"detail": 2}),
    ("blend0", {"blend": 0}),
    ("blend1", {"blend": 1}),
    ("kernel10", {"kernel": 10}),
    ("kernel100", {"kernel": 100}),
    ("bias2", {"bias": 2}),
    ("bias-2", {"bias": -2}),
]


def controls():
    d = os.path.join(OUT, "controls")
    off_img, on_img, measured, model, valid = pair(
        d, "off", "identity", {"hc": 1, "sc": 1, "detail": 1}, "control identity (contrasts and detail 1)"
    )
    rel = np.abs(on_img - off_img) / np.maximum(np.abs(off_img), 1e-3)
    check(
        "control identity changes nothing",
        rel.max() < 2e-3,
        f"max relative |on - off| {rel.max():.2e} (half-float captures: 1 ulp is 1e-3)",
    )
    for name, p in CONTROLS[1:]:
        pair(d, "off", name, p, f"control {name}")
    pair(d, "ae_off", "ae_on", {}, "auto exposure on")
    pair(d, "phys_off", "phys_on", {}, "physical units + auto exposure")
    # What each control does, on the window (center) and a dark corner, from the captures (EV relative to off).
    off_img = le_ref.load_f16(os.path.join(d, "off.f16"))
    yo = off_img @ le_ref.REC709
    H, W = yo.shape
    win = yo > 4.0 * yo.mean()
    dark = yo < np.percentile(yo, 10)
    rows = []
    for name, _ in [("default", None)] + CONTROLS:
        f = name if name != "default" else None
        path = os.path.join(d, f + ".f16") if f else os.path.join(OUT, "model", "1920x1080", "room_on.f16")
        on_img = le_ref.load_f16(path)
        r = np.log2(np.maximum(on_img @ le_ref.REC709, 1e-6) / np.maximum(yo, 1e-6))
        rows.append(f"  {name:10s} window {np.median(r[win]):+.3f} EV   darkest 10% {np.median(r[dark]):+.3f} EV")
    print("per control, median change (window pixels / darkest 10%):\n" + "\n".join(rows))


def halo():
    # A column through the window's center: measured and model factor, blend 0 (grid only) and the default 0.6.
    d = os.path.join(OUT, "controls")
    off_img = le_ref.load_f16(os.path.join(d, "off.f16"))
    yo = off_img @ le_ref.REC709
    H, W = yo.shape
    win = yo > 4.0 * yo.mean()
    cols = np.where(win.any(axis=0))[0]
    x = int(np.median(cols))
    img = Image.new("RGB", (900, 420), "white")
    dr = ImageDraw.Draw(img)
    colors = {"blend0": (31, 119, 180), "default": (214, 39, 40)}
    lines = []
    for name, p, path in [
        ("blend0", {"blend": 0}, os.path.join(d, "blend0.f16")),
        ("default", {}, os.path.join(OUT, "model", "1920x1080", "room_on.f16")),
    ]:
        on_img = le_ref.load_f16(path)
        q = dict(le_ref.DEFAULTS)
        q.update(p)
        factor, parts = le_ref.model_factor(off_img, q)
        meas = np.log2(np.maximum(on_img[:, x] @ le_ref.REC709, 1e-6) / np.maximum(yo[:, x], 1e-6))
        mod = np.log2(factor[:, x])
        # Halo: just below the window's bottom edge (the wall), how much darker than the wall farther away.
        bottom = np.where(win[:, x])[0].max()
        near = meas[bottom + 3 : bottom + 15].mean()
        far = meas[bottom + 150 : bottom + 200].mean()
        lines.append(
            f"  {name}: column x={x}, wall just below the window {near:+.3f} EV vs 150-200 px below {far:+.3f} EV (model {mod[bottom + 3 : bottom + 15].mean():+.3f} / {mod[bottom + 150 : bottom + 200].mean():+.3f}); max |measured - model| on the column {np.abs(meas - mod).max():.4f} EV"
        )
        sx = 880 / H
        for arr, width in ((mod, 1), (meas, 3)):
            pts = [(10 + i * sx, 210 - arr[i] * 40) for i in range(H)]
            dr.line(pts, fill=colors[name], width=width)
    dr.line([(10, 210), (890, 210)], fill=(150, 150, 150))
    dr.text(
        (12, 5),
        f"log2 factor down column x={x} (thick: measured, thin: model); blue blend 0, red default 0.6; 40 px/EV",
        fill=(0, 0, 0),
    )
    img.save(os.path.join(OUT, "halo_profile.png"))
    print("halo profile:\n" + "\n".join(lines))


def web():
    d = os.path.join(OUT, "web")
    nd = os.path.join(OUT, "web_native")
    for off, on, p in [
        ("room_off", "room_on", {}),
        ("chart_off", "chart_on", {}),
        ("strong_off", "strong_on", {"hc": 0.3, "sc": 0.5}),
    ]:
        pair(d, off, on, p, f"web model {on}")
        for name in (off, on):
            a = le_ref.load_f16(os.path.join(d, name + ".f16"))
            b = le_ref.load_f16(os.path.join(nd, name + ".f16"))
            ya, yb = a @ le_ref.REC709, b @ le_ref.REC709
            v = (ya > 1e-4) & (yb > 1e-4)
            r = np.abs(np.log2(ya[v] / yb[v]))
            check(
                f"web vs native {name}",
                np.percentile(r, 99) < 0.05,
                f"|log2 ratio| median {np.median(r):.4f} p99 {np.percentile(r, 99):.4f} max {r.max():.4f} EV",
            )


for w in WHAT:
    {"ident": ident, "model": model_checks, "controls": controls, "halo": halo, "web": web}[w]()
print("FAILED: " + ", ".join(failed) if failed else "ALL OK")
sys.exit(1 if failed else 0)
