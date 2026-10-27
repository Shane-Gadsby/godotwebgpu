# Independent numpy references for task 37's camera finish pass. Usage: analyze.py <check> <files...> [key=value]
import json
import sys

import numpy as np
from PIL import Image

L709 = np.array([0.2126, 0.7152, 0.0722])


def load(p):
    return np.asarray(Image.open(p).convert("RGB")).astype(np.float64) / 255.0


def luma(img):
    return img @ L709


def gauss1d(sigma, radius=None):
    r = int(np.ceil(4 * sigma)) if radius is None else radius
    x = np.arange(-r, r + 1)
    k = np.exp(-x * x / (2 * sigma * sigma))
    return k / k.sum()


def blur_rows(a, sigma):
    k = gauss1d(sigma)
    r = len(k) // 2
    pad = np.pad(a, ((0, 0), (r, r)), mode="edge")
    return np.stack([np.convolve(row, k, mode="valid") for row in pad])


def args_kv():
    return dict(a.split("=", 1) for a in sys.argv[2:] if "=" in a)


def files():
    return [a for a in sys.argv[2:] if "=" not in a]


check = sys.argv[1]
kv = args_kv()
fs = files()

if check == "diff":
    a, b = load(fs[0]), load(fs[1])
    d = np.abs(a - b) * 255
    print(json.dumps({"max_diff": float(d.max()), "pixels_differing": int((d.max(axis=2) > 0).sum())}))

elif check == "nr":
    # step scene: top half a 0.2|0.8 step, bottom half flat 0.5.
    out = {}
    for f in fs:
        img = load(f)
        h, w, _ = img.shape
        y = luma(img)
        flat = img[int(h * 0.6) : int(h * 0.95), int(w * 0.05) : int(w * 0.95)]
        fy = luma(flat)
        # Chroma noise: Cb/Cr standard deviation (Rec. 709).
        cb = (flat[..., 2] - fy) / 1.8556
        cr = (flat[..., 0] - fy) / 1.5748
        top = y[int(h * 0.1) : int(h * 0.4)]
        prof = top.mean(axis=0)
        lo, hi = prof[: w // 4].mean(), prof[3 * w // 4 :].mean()
        t = (prof - lo) / (hi - lo)
        c = w // 2
        win = t[c - 40 : c + 40]
        x10 = np.argmax(win >= 0.1)
        x90 = np.argmax(win >= 0.9)
        out[f.split("/")[-1]] = {
            "luma_sigma_8bit": round(float(fy.std() * 255), 3),
            "cb_sigma_8bit": round(float(cb.std() * 255), 3),
            "cr_sigma_8bit": round(float(cr.std() * 255), 3),
            "edge_10_90_px": int(x90 - x10),
            "step_height": round(float(hi - lo), 4),
        }
    print(json.dumps(out, indent=1))

elif check == "sharpen":
    # base and sharpened renders of the step scene; amount, sigma_px, halo from kv.
    base, sh = load(fs[0]), load(fs[1])
    h, w, _ = base.shape
    a, sigma, halo = float(kv["amount"]), float(kv["sigma"]), float(kv["halo"])
    rows = slice(int(h * 0.1), int(h * 0.4))
    yb = luma(base)[rows]
    ys = luma(sh)[rows].mean(axis=0)
    blur = blur_rows(yb, sigma).mean(axis=0)
    yb1 = yb.mean(axis=0)
    ref = yb1 + a * (yb1 - blur)
    reach = max(1, int(round(max(1.0, sigma))))
    pad = np.pad(yb1, reach, mode="edge")
    lo = np.minimum.reduce([pad[i : i + w] for i in (0, reach, 2 * reach)])
    hi = np.maximum.reduce([pad[i : i + w] for i in (0, reach, 2 * reach)])
    ref = np.clip(ref, lo - halo * (hi - lo), hi + halo * (hi - lo))
    ref = np.clip(ref, 0.0, 1.0)  # The 8-bit display target clips.
    c = w // 2
    step_lo, step_hi = yb1[: w // 4].mean(), yb1[3 * w // 4 :].mean()

    def over(p):
        return float((p[c - 60 : c + 60].max() - step_hi) / (step_hi - step_lo))

    def under(p):
        return float((step_lo - p[c - 60 : c + 60].min()) / (step_hi - step_lo))

    # Halo width: pixels from the edge to where the overshoot falls below 10% of its peak.
    def width(p):
        seg = p[c : c + 200] - step_hi
        pk = seg.max()
        if pk <= 1e-3:
            return 0
        idx = np.where(seg >= 0.1 * pk)[0]
        return int(idx.max() + 1)

    print(
        json.dumps({
            "overshoot": round(over(ys), 4),
            "overshoot_ref": round(over(ref), 4),
            "undershoot": round(under(ys), 4),
            "undershoot_ref": round(under(ref), 4),
            "halo_width_px": width(ys),
            "halo_width_ref_px": width(ref),
            "max_abs_err_8bit": round(float(np.abs(ys - ref)[c - 60 : c + 60].max() * 255), 2),
        })
    )

elif check == "zebra":
    base, z = load(fs[0]), load(fs[1])
    level = float(kv["level"]) / 100.0
    window = kv.get("window") == "1"
    h, w, _ = base.shape
    region = (slice(0, int(h * 0.7) - 2), slice(2, w - 2))
    yb = luma(base)[region]
    changed = np.abs(z - base)[region].max(axis=2) > 1.5 / 255
    marked = (np.abs(yb - level) <= 0.05) if window else (yb >= level)
    frac_above = float(marked.mean())
    frac_changed = float(changed.mean())
    # Stripes are half on, half off: coverage = changed / 0.5. Columns: any change vs above threshold.
    col_marked = marked.any(axis=0)
    col_changed = changed.any(axis=0)
    print(
        json.dumps({
            "fraction_above_level": round(frac_above, 4),
            "stripe_coverage_x2": round(2 * frac_changed, 4),
            "changed_outside_marked": int((changed & ~marked).sum()),
            "column_mismatch": int((col_marked != col_changed).sum()),
            "columns_marked": int(col_marked.sum()),
        })
    )

elif check == "false_color":
    base, fc = load(fs[0]), load(fs[1])
    h, w, _ = base.shape
    region = (slice(0, int(h * 0.7) - 2), slice(2, w - 2))
    ire = luma(base)[region] * 100
    out = fc[region]
    bands = [
        (0, 2.5, (0.5, 0, 0.75)),
        (2.5, 4, (0, 0.35, 1)),
        (38, 42, (0, 0.8, 0)),
        (52, 56, (1, 0.5, 0.75)),
        (97, 99, (1, 1, 0)),
        (99, 1e9, (1, 0, 0)),
    ]
    exp = np.repeat((ire / 100)[..., None], 3, axis=2)
    for lo, hi, col in bands:
        m = (ire >= lo) & (ire < hi)
        exp[m] = col
    err = np.abs(out - exp).max(axis=2) * 255
    # Pixels at a band boundary can fall either way through 8-bit rounding of the luma.
    print(
        json.dumps({
            "pixels": int(err.size),
            "match_within_2": round(float((err <= 2).mean()), 5),
            "max_err_8bit": round(float(err.max()), 1),
        })
    )

elif check == "histogram":
    base, hi_img = load(fs[0]), load(fs[1])
    h, w, _ = base.shape
    size, pos = float(kv["size"]), int(kv.get("pos", 3))
    rgb = kv.get("rgb") == "1"
    rect_w = round(w * size)
    rect_h = round(min(rect_w * 0.5, h * 0.9))
    margin = round(h * 0.03)
    right = pos in (1, 3)
    bottom = pos in (2, 3)
    x0 = max(w - margin - rect_w, 0) if right else margin
    y0 = max(h - margin - rect_h, 0) if bottom else margin
    x1, y1 = min(x0 + rect_w, w), min(y0 + rect_h, h)
    b8 = np.round(base * 255).astype(int)
    lum = np.round((b8 / 255.0) @ L709 * 255).astype(int)
    chans = [b8[..., 0], b8[..., 1], b8[..., 2]] if rgb else [lum]
    errs = []
    for ci, ch in enumerate(chans):
        hist = np.bincount(ch.ravel(), minlength=256).astype(float)
        v = hist / hist.max()
        for x in range(int(x0), int(x1)):
            bin_ = min(int((x + 0.5 - x0) / (x1 - x0) * 256), 255)
            expected = int(np.floor(v[bin_] * (y1 - y0) + 0.5))
            col = hi_img[int(y0) : int(y1), x, ci if rgb else 1]
            got = int((col > 0.5).sum())
            errs.append(abs(got - expected))
    errs = np.array(errs)
    print(
        json.dumps({
            "rect": [x0, y0, x1, y1],
            "columns": int(errs.size),
            "exact": round(float((errs == 0).mean()), 4),
            "within_1px": round(float((errs <= 1).mean()), 4),
            "max_err_px": int(errs.max()),
        })
    )

elif check == "peaking":
    base, pk = load(fs[0]), load(fs[1])
    sens = float(kv["sens"])
    thr = 0.4 - 0.37 * sens
    y = luma(np.clip(base, 0, 1))
    p = np.pad(y, 1, mode="edge")

    def s(dy, dx):
        return p[1 + dy : 1 + dy + y.shape[0], 1 + dx : 1 + dx + y.shape[1]]

    gx = (s(-1, 1) + 2 * s(0, 1) + s(1, 1)) - (s(-1, -1) + 2 * s(0, -1) + s(1, -1))
    gy = (s(1, -1) + 2 * s(1, 0) + s(1, 1)) - (s(-1, -1) + 2 * s(-1, 0) + s(-1, 1))
    ref = np.hypot(gx, gy) * 0.25 >= thr
    got = (pk[..., 0] > 0.99) & (pk[..., 1] < 0.01) & (pk[..., 2] < 0.01)
    h, w = y.shape
    inter = (ref & got).sum()
    union = (ref | got).sum()
    left = (slice(None), slice(0, w // 2))
    rightr = (slice(None), slice(w // 2, w))
    print(
        json.dumps({
            "iou_vs_cpu_sobel": round(float(inter / max(union, 1)), 4),
            "marked_left_in_focus": int(got[left].sum()),
            "marked_right_defocused": int(got[rightr].sum()),
            "ref_left": int(ref[left].sum()),
            "ref_right": int(ref[rightr].sum()),
        })
    )
