"""Independent numeric checks for task 33's lens optics. Each check recomputes the expected result from the optics
formulas (in sensor millimeters, not the shader's frame units) and compares with the rendered PNGs."""

import json
import math
import sys
from collections import deque

import numpy as np
from PIL import Image


def load(path):
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.float64) / 255.0
    # sRGB EOTF (the tonemap pass writes sRGB-encoded 8-bit with the linear tonemapper).
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def blobs(img, channel=None, thresh=0.05):
    """Centroids (x, y) of bright blobs, weighted by intensity, per channel or by luminance."""
    v = img.mean(axis=2) if channel is None else img[:, :, channel]
    mask = v > thresh
    h, w = mask.shape
    seen = np.zeros_like(mask)
    out = []
    ys, xs = np.nonzero(mask)
    for y0, x0 in zip(ys, xs):
        if seen[y0, x0]:
            continue
        q = deque([(y0, x0)])
        seen[y0, x0] = True
        pts = []
        while q:
            y, x = q.popleft()
            pts.append((y, x))
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < h and 0 <= xx < w and mask[yy, xx] and not seen[yy, xx]:
                        seen[yy, xx] = True
                        q.append((yy, xx))
        p = np.array(pts)
        wt = v[p[:, 0], p[:, 1]]
        if len(pts) < 3 or p[:, 0].min() == 0 or p[:, 1].min() == 0 or p[:, 0].max() == h - 1 or p[:, 1].max() == w - 1:
            continue  # Clipped by the frame.
        out.append(((p[:, 1] + 0.5) * wt).sum() / wt.sum())
        out[-1] = (out[-1], ((p[:, 0] + 0.5) * wt).sum() / wt.sum())
    return np.array(out)


def match(a, b):
    """Nearest b for each a."""
    d = np.linalg.norm(a[:, None, :] - b[None, :, :], axis=2)
    return b[d.argmin(axis=1)], d.min(axis=1)


def pupil_overlap_numeric(d, rp, n=801):
    """Fraction of a disk of radius rp centered at distance d inside the unit disk, by grid integration."""
    xs = np.linspace(-rp, rp, n)
    X, Y = np.meshgrid(xs, xs)
    inside_pupil = X * X + Y * Y <= rp * rp
    inside_barrel = (X + d) ** 2 + Y * Y <= 1.0
    return (inside_pupil & inside_barrel).sum() / inside_pupil.sum()


def check_vignette(path, f_mm, sensor_h, f_number=None, max_f=None, strength=1.0, practical=None):
    img = load(path)
    h, w, _ = img.shape
    pitch = sensor_h / h
    sensor_w = 36.0 if sensor_h == 24.0 else None
    center = img[h // 2 - 2 : h // 2 + 2, w // 2 - 2 : w // 2 + 2, 1].mean()
    rows = []
    for t in np.linspace(0.0, 1.0, 11):
        px = w / 2 + t * (w / 2 - 1)
        py = h / 2 + t * (h / 2 - 1)
        x_mm = (px + 0.5 - w / 2) * pitch
        y_mm = (py + 0.5 - h / 2) * pitch
        r_mm = math.hypot(x_mm, y_mm)
        measured = img[int(py), int(px), 1] / center
        if practical:
            stops, falloff = practical
            r_frame = r_mm / (0.5 * math.hypot(w * pitch, h * pitch))
            expected = 2.0 ** (-stops * r_frame**falloff)
        else:
            theta = math.atan(r_mm / f_mm)
            expected = math.cos(theta) ** 4
            if f_number:
                sensor_r = 0.5 * math.hypot(sensor_w, sensor_h)
                rp = min(max_f, f_number) / f_number
                expected *= pupil_overlap_numeric(0.5 * r_mm / sensor_r, rp)
            expected = expected**strength
        rows.append((t, r_mm, measured, expected))
    err = max(abs(m - e) for _, _, m, e in rows)
    return {"rows": [[round(x, 4) for x in r] for r in rows], "max_abs_err": err}


def bc_forward(x, y, k1, k2):
    r2 = x * x + y * y
    f = 1 + k1 * r2 + k2 * r2 * r2
    return x * f, y * f


def check_distortion(path_off, path_on, k1, k2, crop=None):
    """Dots without distortion (u) and with (d), in frame units (corner = 1). Expect d = BC(u) / crop, crop from the
    largest inscribed rectangle of the distorted frame border (barrel)."""
    a, b = load(path_off), load(path_on)
    h, w, _ = a.shape
    R = 0.5 * math.hypot(w, h)
    u = blobs(a)
    d = blobs(b)
    uf = (u - [w / 2, h / 2]) / R
    hx, hy = w / 2 / R, h / 2 / R
    if crop is None:
        # Independent crop: sample the frame border, distort, find the closest point in rectangle metric.
        c = 1.0
        for t in np.linspace(-1, 1, 2001):
            for x, y in ((t * hx, -hy), (t * hx, hy), (-hx, t * hy), (hx, t * hy)):
                dx, dy = bc_forward(x, y, k1, k2)
                c = min(c, max(abs(dx) / hx, abs(dy) / hy))
        crop = c
    px, py = bc_forward(uf[:, 0], uf[:, 1], k1, k2)
    pred = np.stack([px, py], 1) / crop * R + [w / 2, h / 2]
    inside = (pred[:, 0] > 4) & (pred[:, 0] < w - 4) & (pred[:, 1] > 4) & (pred[:, 1] < h - 4)
    got, dist = match(pred[inside], d)
    return {
        "dots": int(inside.sum()),
        "crop": crop,
        "max_err_px": float(dist.max()),
        "mean_err_px": float(dist.mean()),
        "max_displacement_px": float(np.linalg.norm(pred[inside] - u[inside], axis=1).max()),
    }


def check_ca(path, strength):
    """Red and blue dot centroids vs green: expected radial offsets +-delta * r (delta = 0.0005 * strength)."""
    img = load(path)
    h, w, _ = img.shape
    g = blobs(img, 1, 0.02)
    r = blobs(img, 0, 0.02)
    bl = blobs(img, 2, 0.02)
    r_m, _ = match(g, r)
    b_m, _ = match(g, bl)
    c = np.array([w / 2, h / 2])
    rad = np.linalg.norm(g - c, axis=1)
    sel = rad > 0.3 * np.linalg.norm(c)
    spread = np.linalg.norm(r_m - b_m, axis=1)[sel]
    expected = (2 * 0.0005 * strength) * rad[sel]  # Red at (1+d), blue at (1-d).
    frac = spread / np.linalg.norm(c)  # As a fraction of the half diagonal.
    return {
        "dots": int(sel.sum()),
        "spread_px_max": float(spread.max()),
        "expected_px_at_same": float(expected[spread.argmax()]),
        "max_err_px": float(np.abs(spread - expected).max()),
        "spread_over_radius": float(np.median(spread / rad[sel])),
        "expected_over_radius": 2 * 0.0005 * strength,
        "corner_spread_frac_of_half_diag": float(np.median(frac / (rad[sel] / np.linalg.norm(c)))),
    }


def check_edge(path, sigma_expected_px):
    """Edge spread function across the vertical center edge: fit a Gaussian sigma from the 10-90% width."""
    img = load(path)
    h, w, _ = img.shape
    rows = img[h // 2 - 20 : h // 2 + 20, :, 1].mean(axis=0)
    lo, hi = rows[: w // 4].mean(), rows[-w // 4 :].mean()
    n = (rows - hi) / (lo - hi)  # 1 on the white (left) side, 0 on the right.
    x = np.arange(w) + 0.5

    # Crossings, interpolated.
    def cross(level):
        i = np.nonzero((n[:-1] >= level) & (n[1:] < level))[0][0]
        return x[i] + (n[i] - level) / (n[i] - n[i + 1])

    width = cross(0.1) - cross(0.9)
    return {"width_10_90_px": float(width), "sigma_px": float(width / 2.5631), "sigma_expected_px": sigma_expected_px}


if __name__ == "__main__":
    fn = globals()[sys.argv[1]]
    kwargs = json.loads(sys.argv[2])
    print(json.dumps(fn(**kwargs), indent=1))
