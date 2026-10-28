#!/usr/bin/env python3
"""analyze.py <outdir> [renderers]: checks task 35's renders against independent references (numpy only)."""
import sys, math, os
import numpy as np
from PIL import Image

O = sys.argv[1]
RMS = sys.argv[2].split(",") if len(sys.argv) > 2 else ["forward_plus", "mobile"]

def load(name):
    a = np.asarray(Image.open(os.path.join(O, name)).convert("RGB")).astype(np.float64) / 255.0
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)

def raw(name):
    return np.asarray(Image.open(os.path.join(O, name)).convert("RGB")).astype(np.int32)

def lum(a):
    return a @ np.array([0.2126, 0.7152, 0.0722])

def smooth(L, n=2):
    for _ in range(n):
        L = (L + np.roll(L, 1, 0) + np.roll(L, -1, 0) + np.roll(L, 1, 1) + np.roll(L, -1, 1)) / 5
    return L

def exists(name):
    return os.path.exists(os.path.join(O, name))

# ---------------------------------------------------------------- defaults
def defaults():
    print("== defaults: base vs new (8-bit max diff, differing pixels)")
    for rm in ["forward_plus", "mobile", "gl_compatibility"]:
        for a in ["physical", "practical", "none"]:
            b, n = f"def_base_{rm}_{a}.png", f"def_new_{rm}_{a}.png"
            if not (exists(b) and exists(n)):
                print(f"  {rm:16s} {a:9s} MISSING"); continue
            d = np.abs(raw(b) - raw(n))
            print(f"  {rm:16s} {a:9s} max {d.max()}  pixels {(d.max(axis=2) > 0).sum()}")

# ---------------------------------------------------------------- bokeh polygon
def boundary_radius(L, cx, cy, angles, rmax):
    """Radius where luminance drops below half the bokeh's plateau, per angle (counterclockwise on screen)."""
    rs = np.arange(0, rmax, 0.25)
    r_out = []
    plateau = np.median(L[int(cy) - 3:int(cy) + 4, int(cx) - 3:int(cx) + 4])
    for t in angles:
        xs = cx + rs * math.cos(t)
        ys = cy - rs * math.sin(t)
        xi = np.clip(xs.round().astype(int), 0, L.shape[1] - 1)
        yi = np.clip(ys.round().astype(int), 0, L.shape[0] - 1)
        v = L[yi, xi]
        below = np.nonzero(v < 0.5 * plateau)[0]
        r_out.append(rs[below[0]] if len(below) else rmax)
    return np.array(r_out), plateau

def centroid(L, x0, y0, w):
    ys, xs = np.mgrid[max(0, y0 - w):min(L.shape[0], y0 + w), max(0, x0 - w):min(L.shape[1], x0 + w)]
    sub = L[ys, xs]
    m = sub > 0.25 * sub.max()
    wgt = sub * m
    return (wgt * xs).sum() / wgt.sum(), (wgt * ys).sum() / wgt.sum()

def blades():
    print("== blades: dominant harmonic of r(theta) = vertex count; vertex angle (ccw from +x) vs setting; min/max r vs cos(pi/N)")
    for rm in RMS:
        for n in [5, 6, 7]:
            for rot in [0, 20]:
                name = f"blades_{rm}_{n}_{rot}.png"
                if not exists(name):
                    continue
                L = smooth(lum(load(name))); H, W = L.shape
                cx, cy = centroid(L, W // 2, H // 2, 120)
                ang = np.linspace(0, 2 * math.pi, 1440, endpoint=False)
                r, plateau = boundary_radius(L, cx, cy, ang, 150)
                spec = np.abs(np.fft.rfft(r - r.mean()))
                harm = int(np.argmax(spec[2:20]) + 2)
                cN = np.sum(r * np.exp(-1j * n * ang))
                vert = (-np.angle(cN) / n) % (2 * math.pi / n)
                exp_v = math.radians(rot) % (2 * math.pi / n)
                err = (math.degrees(vert - exp_v) + 180 / n) % (360 / n) - 180 / n
                print(f"  {rm:13s} phys N={n} rot={rot:2d}: harmonic {harm}  vertex {math.degrees(vert):6.2f} deg (expected {math.degrees(exp_v):6.2f}, err {err:+.2f})  r {r.mean():.1f}px  min/max {r.min()/r.max():.3f} (cos(pi/N) {math.cos(math.pi/n):.3f})")
        for n in [5, 6, 7]:
            name = f"pblades_{rm}_{n}_20.png"
            if not exists(name):
                continue
            L = smooth(lum(load(name))); H, W = L.shape
            cx, cy = centroid(L, W // 2, H // 2, 120)
            ang = np.linspace(0, 2 * math.pi, 1440, endpoint=False)
            r, plateau = boundary_radius(L, cx, cy, ang, 150)
            spec = np.abs(np.fft.rfft(r - r.mean()))
            harm = int(np.argmax(spec[2:20]) + 2)
            cN = np.sum(r * np.exp(-1j * n * ang))
            vert = (-np.angle(cN) / n) % (2 * math.pi / n)
            exp_v = math.radians(20) % (2 * math.pi / n)
            err = (math.degrees(vert - exp_v) + 180 / n) % (360 / n) - 180 / n
            print(f"  {rm:13s} prac N={n} rot=20: harmonic {harm}  vertex {math.degrees(vert):6.2f} deg (expected {math.degrees(exp_v):6.2f}, err {err:+.2f})  r {r.mean():.1f}px  min/max {r.min()/r.max():.3f} (cos(pi/N) {math.cos(math.pi/n):.3f})")
        for tag in ["round", "6_curved", "off"]:
            name = f"blades_{rm}_{tag}.png"
            if not exists(name):
                continue
            L = lum(load(name)); H, W = L.shape
            cx, cy = centroid(L, W // 2, H // 2, 120)
            ang = np.linspace(0, 2 * math.pi, 1440, endpoint=False)
            r, _ = boundary_radius(L, cx, cy, ang, 150)
            spec = np.abs(np.fft.rfft(r - r.mean()))
            print(f"  {rm:13s} {tag:9s}: r {r.mean():.1f}px  min/max {r.min()/r.max():.3f}  harmonic-6 amplitude {spec[6]/len(r)*2:.2f}px")
        name = f"blades_{rm}_anamorphic.png"
        if exists(name):
            L = lum(load(name)); H, W = L.shape
            cx, cy = centroid(L, W // 2, H // 2, 160)
            ys, xs = np.mgrid[0:H, 0:W]
            m = (L > 0.5 * L[int(cy), int(cx)]) & (np.abs(xs - cx) < 200) & (np.abs(ys - cy) < 200)
            w = np.ptp(xs[m]) + 1; h = np.ptp(ys[m]) + 1
            sx = math.sqrt(((xs[m] - cx) ** 2).mean()); sy = math.sqrt(((ys[m] - cy) ** 2).mean())
            # The image is the aperture's shape convolved with the source's disc, and variances add: take the disc's
            # (radius^2 / 4 per axis) out. The desqueezed frame of a 2x anamorphic lens is 72 mm wide: vertical FOV from it.
            vfov = 2 * math.atan(0.5 * 72.0 / (16 / 9) / 50.0)
            rs = 0.08 / (20 * math.tan(vfov / 2)) * (H / 2)
            corr = math.sqrt(max(sy * sy - rs * rs / 4, 1e-9) / max(sx * sx - rs * rs / 4, 1e-9))
            print(f"  {rm:13s} anamorphic 2x: height/width {h/w:.3f} (bbox), {sy/sx:.3f} (moments), {corr:.3f} (moments less the {rs:.1f}px source); expected 2.000")

# ---------------------------------------------------------------- cat's eye
def two_circle_moments(c, R):
    """Second moments of {|u| <= 1} intersect {|u - c| <= R}, in aperture radii (grid integration)."""
    g = np.linspace(-1, 1, 801)
    X, Y = np.meshgrid(g, g)
    m = (X ** 2 + Y ** 2 <= 1) & ((X - c[0]) ** 2 + (Y - c[1]) ** 2 <= R * R)
    x, y = X[m], Y[m]
    cov = np.cov(np.vstack([x, y]))
    ev, evec = np.linalg.eigh(cov)
    return math.sqrt(ev[0] / ev[1]), evec[:, 0], m.mean() / (math.pi / 4)

def ellipse_ratio(L, x0, y0, rs):
    cx, cy = centroid(L, x0, y0, 90)
    H, W = L.shape
    ys, xs = np.mgrid[max(0, int(cy) - 90):min(H, int(cy) + 90), max(0, int(cx) - 90):min(W, int(cx) + 90)]
    sub = L[ys, xs]
    plateau = np.percentile(sub[sub > 0.3 * sub.max()], 50)
    m = sub > 0.5 * plateau
    x = xs[m] - cx; y = -(ys[m] - cy)
    cov = np.cov(np.vstack([x, y])) - np.eye(2) * rs * rs / 4
    ev, evec = np.linalg.eigh(cov)
    return math.sqrt(max(ev[0], 1e-9) / ev[1]), evec[:, 0]

def catseye():
    print("== cat's eye: minor/major axis ratio per point vs the two-circle model; minor axis along the radius")
    pts = [(0, 0), (0.45, 0), (0.9, 0), (0, 0.85), (0.6, 0.6), (-0.9, -0.85)]
    # Frame (desqueezed mm) for a 16:9 output on a 36x24 sensor (vertical fit): 42.67 x 24.
    fw, fh = 24 * 16 / 9, 24.0
    fr = 0.5 * math.hypot(fw, fh); sr = 0.5 * math.hypot(36, 24)
    hx, hy = 0.5 * fw / fr, 0.5 * fh / fr
    for rm in RMS:
        for f in ["1.4", "2.0", "2.8"]:
            name = f"catseye_{rm}_f{f}.png"
            if not exists(name):
                continue
            L = lum(load(name)); H, W = L.shape
            ratio_n = float(f) / 1.4
            R = ratio_n
            off = 0.5 * fr / sr * ratio_n
            rows = []
            rs = 0.08 / (20 * math.tan(math.radians(26.99147) / 2)) * (H / 2)
            for (px, py) in pts:
                x0 = int((px + 1) * 0.5 * W); y0 = int((1 - py) * 0.5 * H)
                meas, minor = ellipse_ratio(smooth(L), x0, y0, rs)
                evec = np.array([minor]).T
                q = np.array([px * hx, py * hy])
                exp_ratio, exp_minor, area = two_circle_moments(-off * q, R)
                radial = q / np.linalg.norm(q) if np.linalg.norm(q) > 0 else None
                align = abs(float(np.dot(evec[:, 0], radial))) if radial is not None and meas < 0.97 else float("nan")
                rows.append(f"({px:+.2f},{py:+.2f}) {meas:.3f}/{exp_ratio:.3f} |cos| {align:.3f}")
            print(f"  {rm:13s} f/{f}: " + "  ".join(rows))
        name = f"pcatseye_{rm}.png"
        if exists(name):
            L = smooth(lum(load(name))); H, W = L.shape
            rows = []
            rs = 0.08 / (20 * math.tan(math.radians(75) / 2)) * (H / 2)
            # Practical: a 24 mm tall frame, the lens wide open (barrel radius 1, offset 0.5 per frame unit).
            fw2, fh2 = 24 * 16 / 9, 24.0; fr2 = 0.5 * math.hypot(fw2, fh2)
            hx2, hy2 = 0.5 * fw2 / fr2, 0.5 * fh2 / fr2
            for (px, py) in pts:
                x0 = int((px + 1) * 0.5 * W); y0 = int((1 - py) * 0.5 * H)
                meas, minor = ellipse_ratio(L, x0, y0, rs)
                q = np.array([px * hx2, py * hy2])
                exp_ratio, _, _ = two_circle_moments(-0.5 * q, 1.0)
                align = abs(float(np.dot(minor, q / np.linalg.norm(q)))) if np.linalg.norm(q) > 0 and meas < 0.97 else float("nan")
                rows.append(f"({px:+.2f},{py:+.2f}) {meas:.3f}/{exp_ratio:.3f} |cos| {align:.3f}")
            print(f"  {rm:13s} practical: " + "  ".join(rows))

# ---------------------------------------------------------------- tilt-shift
def tilt():
    print("== tilt-shift: sharpest image row vs where the ground meets the plane of focus (hinge rule, computed here)")
    H, W = 720, 1280
    vfov = math.radians(26.99147); pitch = math.radians(-20); height = 1.5; s = float(os.environ.get('TILT_FOCUS', '5.0')); f = 0.05
    def expected_row(theta_deg, axis_deg=0.0):
        th = math.radians(theta_deg)
        best = None; rows = []
        for y in range(H):
            ndc_y = 1 - 2 * (y + 0.5) / H
            a = ndc_y * math.tan(vfov / 2)
            # world direction of the camera ray (0, a, -1) pitched about x
            dy = a * math.cos(pitch) + (-1) * (-math.sin(pitch)) * (-1) * -1
            dy = a * math.cos(pitch) - (-1) * math.sin(pitch) * -1
            # rotate (0, a, -1) by pitch about x: y' = a cos p - (-1) sin p ; z' = a sin p + (-1) cos p
            yw = a * math.cos(pitch) + math.sin(pitch)
            if yw >= 0:
                rows.append(float("inf")); continue
            t = height / -yw  # depth along -z (camera space), since the ray has z = -1
            # plane of focus: through the hinge line at y = -J (J = f / sin(theta)) and (0, 0, -s)
            if th == 0:
                sp = s
            else:
                J = f / math.sin(th)
                denom = 1 - s * a / J
                sp = s / denom if denom > 0 else float("inf")
            rows.append(t - sp)
        rows = np.array(rows)
        sign = np.sign(rows)
        cross = np.nonzero(np.diff(sign) != 0)[0]
        return [c + 0.5 for c in cross if np.isfinite(rows[c]) and np.isfinite(rows[c + 1])]
    for rm in RMS:
        for t in ["0", "-2.0", "-6.0", "1.0"]:
            name = f"tilt_{rm}_{t}.png"
            if not exists(name):
                continue
            def sharp(nm):
                L = lum(load(nm))
                # The checker's contrast per row: what defocus takes away.
                c = L[:, W // 4:3 * W // 4].std(axis=1)
                return np.convolve(c, np.ones(9) / 9, mode="same")[1:-1], L
            sm, L = sharp(name)
            ref = f"tilt_{rm}_ref.png"
            if exists(ref):
                # Sharpness relative to an all-in-focus render of the same view: the texture's own detail cancels.
                sr, _ = sharp(ref)
                sm = np.where(sr > 1e-4, sm / np.maximum(sr, 1e-4), 0)
            # Only the ground has texture; skip the horizon's edge against the black background.
            horizon = int(np.nonzero(L[:, W // 2] > 0.01)[0][0])
            sm[:horizon + 40] = 0
            peak = int(np.argmax(sm[5:-5]) + 5) + 1
            exp = expected_row(float(t))
            half = sm.max() * 0.5
            band = np.nonzero(sm > half)[0]
            print(f"  {rm:13s} tilt {t:>5s}: sharpest row {peak}, expected {', '.join(f'{e:.1f}' for e in exp) or 'none'}; rows above half sharpness {band.min()+1}-{band.max()+1}")
        name = f"tilt_{rm}_-6.0_axis90.png"
        if exists(name):
            L = lum(load(name))
            lap = np.abs(4 * L[1:-1, 1:-1] - L[:-2, 1:-1] - L[2:, 1:-1] - L[1:-1, :-2] - L[1:-1, 2:])
            colprof = lap[H // 2 + 50:, :].mean(axis=0)
            sm = np.convolve(colprof, np.ones(31) / 31, mode="same")
            print(f"  {rm:13s} tilt -6 axis 90: sharpness left/center/right thirds {sm[:W//3].mean():.4f} {sm[W//3:2*W//3].mean():.4f} {sm[2*W//3:].mean():.4f} (vertical band: one side sharper)")

# ---------------------------------------------------------------- starburst
def ring_profile(L, cx, cy, r0, r1, n=1440):
    ang = np.linspace(0, 2 * math.pi, n, endpoint=False)
    prof = np.zeros(n)
    for r in np.linspace(r0, r1, 24):
        xs = np.clip((cx + r * np.cos(ang)).round().astype(int), 0, L.shape[1] - 1)
        ys = np.clip((cy - r * np.sin(ang)).round().astype(int), 0, L.shape[0] - 1)
        prof += L[ys, xs]
    return ang, prof / 24

def peaks(prof, frac=0.5):
    base = np.median(prof); top = prof.max()
    m = prof > base + frac * (top - base)
    # count runs (circular)
    starts = np.nonzero(m & ~np.roll(m, 1))[0]
    out = []
    for s0 in starts:
        e = s0
        while m[(e + 1) % len(m)]:
            e += 1
        idx = np.arange(s0, e + 1) % len(m)
        out.append(idx[np.argmax(prof[idx])])
    return out

def starburst():
    print("== starburst: spike count on a ring (0.05-0.12 frame heights), spike angles, f/8 vs f/16 light")
    for rm in RMS:
        res = {}
        for tag, n, rot in [("6_f16", 6, 0), ("7_f16", 7, 0), ("8_f16", 8, 0), ("6_f16_rot15", 6, 15), ("6_f8", 6, 0)]:
            name = f"star_{rm}_{tag}.png"
            if not exists(name):
                continue
            L = lum(load(name)); H, W = L.shape
            cx, cy = centroid(L, W // 2, H // 2, 10)
            ang, prof = ring_profile(L, cx, cy, 0.05 * H, 0.12 * H)
            pk = peaks(prof)
            angles = sorted(math.degrees(ang[p]) for p in pk)
            exp_count = n if n % 2 == 0 else 2 * n
            first = (rot + 180 / n)
            exp_angles = sorted(((first + k * 360 / (n if n % 2 == 0 else n)) % 360) for k in range(n))
            if n % 2 == 1:
                exp_angles = sorted(set([round((a) % 360, 3) for a in exp_angles] + [round((a + 180) % 360, 3) for a in exp_angles]))
            errs = [min(abs((a - e + 180) % 360 - 180) for e in exp_angles) for a in angles]
            res[tag] = prof.max() - np.median(prof)
            print(f"  {rm:13s} {tag:11s}: {len(pk)} spikes (expected {exp_count}); max angle error {max(errs) if errs else float('nan'):.2f} deg; spike light {res[tag]:.5f}")
        if "6_f8" in res and "6_f16" in res:
            print(f"  {rm:13s} f/8 / f/16 spike light: {res['6_f8'] / res['6_f16']:.3f} (expected 0.500: lambda f l / A grows with N)")
        name = f"star_{rm}_off.png"
        if exists(name):
            L = lum(load(name)); H, W = L.shape
            ang, prof = ring_profile(L, W / 2, H / 2, 0.05 * H, 0.12 * H)
            print(f"  {rm:13s} off: ring max {prof.max():.5f}")

# ---------------------------------------------------------------- lens flare
def flare():
    print("== lens flare: ghost centroids on the line through the center and the source; k = position / source position")
    expected = [-0.55, -1.1, -1.7, 0.45]
    for rm in RMS:
        name = f"flare_{rm}_4.png"
        if not exists(name):
            continue
        L = lum(load(name)); H, W = L.shape
        sx, sy = 0.5, 0.4  # in half extents
        src = np.array([sx * W / 2, sy * H / 2])  # pixels from center, y up
        # Find maxima of a blurred image away from the source.
        B = L.copy()
        for _ in range(3):
            B = (B + np.roll(B, 1, 0) + np.roll(B, -1, 0) + np.roll(B, 1, 1) + np.roll(B, -1, 1)) / 5
        ys, xs = np.mgrid[0:H, 0:W]
        px = xs - W / 2 + 0.5; py = -(ys - H / 2 + 0.5)
        mask = np.hypot(px - src[0], py - src[1]) > 40
        found = []
        work = np.where(mask, B, 0)
        for _ in range(6):
            i = np.argmax(work)
            y, x = divmod(i, W)
            if work[y, x] < 0.02 * B[mask].max() or work[y, x] <= 1e-4:
                break
            # centroid of the ghost around the maximum
            w = 40
            yy, xx = np.mgrid[max(0, y - w):min(H, y + w), max(0, x - w):min(W, x + w)]
            sub = L[yy, xx]; m = sub > 0.3 * sub.max(); wt = sub * m
            gx = (wt * xx).sum() / wt.sum() - W / 2 + 0.5; gy = -((wt * yy).sum() / wt.sum() - H / 2 + 0.5)
            g = np.array([gx, gy])
            k = float(np.dot(g, src) / np.dot(src, src))
            perp = abs(g[0] * src[1] - g[1] * src[0]) / np.linalg.norm(src)
            found.append((k, perp, work[y, x]))
            work[max(0, y - 2 * w):y + 2 * w, max(0, x - 2 * w):x + 2 * w] = 0
        found.sort()
        print(f"  {rm:13s} " + "  ".join(f"k {k:+.3f} (perp {p:.1f}px)" for k, p, _ in found) + f"   expected k {sorted(expected)}")

# ---------------------------------------------------------------- halation
def halation():
    print("== halation: red halo sigma vs the model (0.112 mm x radius on a 24 mm frame); green/red; blue; flat field")
    for rm in RMS:
        base = f"halation_{rm}_point.png"
        if not exists(base):
            continue
        A0 = load(base); H, W = A0.shape[:2]
        cx, cy = centroid(lum(A0), W // 2, H // 2, 20)
        ys, xs = np.mgrid[0:H, 0:W]
        rr = np.hypot(xs - cx, ys - cy)
        rdisc = rr[lum(A0) > 0.5 * lum(A0).max()].max()
        for r in ["1.0", "4.0", "16.0"]:
            name = f"halation_{rm}_r{r}.png"
            if not exists(name):
                continue
            A = load(name)
            D = A - A0
            out = (rr > rdisc + 2) & (rr < 0.45 * H)
            red = D[..., 0]
            sel = out & (red > 2e-4)
            # Fit log(red) = a - r^2 / (2 sigma^2) beyond the disc.
            x = rr[sel] ** 2; y = np.log(red[sel])
            wts = red[sel]
            Aw = np.vstack([np.ones_like(x), x]).T * wts[:, None]
            coef = np.linalg.lstsq(Aw, y * wts, rcond=None)[0]
            sigma = math.sqrt(-1 / (2 * coef[1])) if coef[1] < 0 else float("nan")
            expected = 0.112 * float(r) / 24.0 * H
            g_over_r = D[..., 1][sel].sum() / red[sel].sum()
            b_over_r = D[..., 2][sel].sum() / red[sel].sum()
            print(f"  {rm:13s} radius x{r:>4s}: halo sigma {sigma:6.2f}px (model {expected:6.2f}px; disc radius {rdisc:.1f}px)  green/red {g_over_r:.3f} (0.300)  blue/red {b_over_r:+.4f} (0)")
        fl, off = f"halation_{rm}_flat.png", f"halves_{rm}_off.png"
        if exists(fl) and exists(off):
            a, b = load(fl), load(off)
            Wd = a.shape[1]
            inner = (slice(100, 620), slice(100, Wd // 2 - 150))
            print(f"  {rm:13s} flat field (strength 4): mean change in the bright interior R {np.mean(a[inner][..., 0] - b[inner][..., 0]):+.5f} G {np.mean(a[inner][..., 1] - b[inner][..., 1]):+.5f} B {np.mean(a[inner][..., 2] - b[inner][..., 2]):+.5f} (of {np.mean(b[inner][..., 0]):.4f})")

# ---------------------------------------------------------------- veiling + diffusion
def halves():
    print("== veiling glare (2% / 10% of the mean) and diffusion on a half-bright frame (linear values)")
    for rm in RMS:
        off = f"halves_{rm}_off.png"
        if not exists(off):
            continue
        b = lum(load(off)); H, W = b.shape
        mean = b.mean()
        dark = (slice(100, H - 100), slice(W // 2 + 200, W - 100))
        bright = (slice(100, H - 100), slice(100, W // 2 - 200))
        for tag, v in [("veil1", 0.02), ("veil5", 0.10)]:
            name = f"halves_{rm}_{tag}.png"
            if not exists(name):
                continue
            a = lum(load(name))
            print(f"  {rm:13s} {tag}: dark side {a[dark].mean():.5f} (expected {v*mean:.5f}), bright side {a[bright].mean():.5f} (expected {(1-v)*b[bright].mean()+v*mean:.5f}); mean of frame {mean:.4f}")
        for tag in ["diff1", "diffblack1"]:
            name = f"halves_{rm}_{tag}.png"
            if not exists(name):
                continue
            a = lum(load(name))
            edge = None
            prof = a[H // 2 - 50:H // 2 + 50].mean(axis=0)
            pb = b[H // 2 - 50:H // 2 + 50].mean(axis=0)
            # the card's edge is where the off image falls below half
            e = int(np.nonzero(pb < 0.5 * pb.max())[0][0])
            lifts = [prof[e + d] - pb[e + d] for d in (10, 40, 120, 300)]
            print(f"  {rm:13s} {tag:10s}: bright interior {a[bright].mean():.4f} (was {b[bright].mean():.4f}); dark-side lift at 10/40/120/300 px from the edge: " + " ".join(f"{x:.4f}" for x in lifts) + f"; frame mean {a.mean():.4f} (was {mean:.4f})")

if __name__ == "__main__":
    for fn in [defaults, blades, catseye, tilt, starburst, flare, halation, halves]:
        try:
            fn()
        except Exception as e:
            print(f"  {fn.__name__} FAILED: {e!r}")
