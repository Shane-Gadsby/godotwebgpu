#!/usr/bin/env python3
"""verify.py jobs|halation|diffusion|flare <outdir> [heights]: glare sizes against their model, from HDR captures.

Source: emissive spheres scaled with the frame (radius 0.03 x 720 / H at 20 m, a ~2 px disc at every height, the
same light), so the source is point-like everywhere: the models are point spread functions, and a blend of two lods
is not a Gaussian, so its fitted size would otherwise depend on the source's own size.
- halation: radius 0.5, 1, 2, 4, 16, centered point (task 40's case) and nine points (sub-texel phases, and where a
  chain's levels are misplaced); model sigma = 0.112 mm x radius on a 24 mm frame, or the chain's lod 0 floor.
- diffusion: strength 1, normal and black; model (1 - s) S + s mean(G_{b 2^i} * S), b = 1% (0.5% black) of the frame
  height, i = 0..3; fitted with a free size factor k on all four (1 = the model) and s free; blue channel.
- flare: ghost 1 (magnification -0.55) and ghost 2 (-1.1) alone; model c G_sigma * S read at the ghost's mirrored
  UV, sigma = 0.004 + 0.008 |k| frame heights (in the source's frame); green channel, threshold 0.
"""

import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from halofit import _block, fit_2d, fit_pair, load  # noqa: E402

CHAINS = {
    "fp": ("forward_plus", 1, (-1.233, 5.181), False),
    "hq": ("forward_plus", 0, (-1.556, 5.182), True),
    "mob": ("mobile", 2, (-0.157, 0.440), False),
}
PTS9 = ";".join(
    f"{-0.3 + 0.6 * ((i * 0.6180339) % 1):.4f},{-0.3 + 0.6 * ((i * 0.4142136 + 0.1) % 1):.4f}" for i in range(9)
)
RADII = ["0.5", "1.0", "2.0", "4.0", "16.0"]


def base(H, pts="0,0"):
    return [
        "scene=points",
        f"points={pts}",
        f"point_size={0.03 * 720 / H:.6f}",
        "point_energy=40",
        "cam.frustum_focal_length=50.0",
        "cam.frustum_focus_distance=20.0",
        "cam.exposure_aperture=16.0",
        "hdr=1",
    ]


def floor_px(ch, H):
    rm, red, (A, B), hq = CHAINS[ch]
    return math.sqrt(A + B) * H / (H >> red)


def jobs(out_dir, hs):
    J = []
    for ch, (rm, red, _, hq) in CHAINS.items():
        for H in hs:
            res = f"{H * 16 // 9}x{H}"
            for tag, pts in (("c", "0,0"), ("9", PTS9)):
                J.append({"out": f"{out_dir}/halo_{ch}_{H}_{tag}_ref.png", "rm": rm, "res": res, "args": base(H, pts)})
                for r in RADII:
                    a = base(H, pts) + [
                        "cam.halation_enabled=true",
                        "cam.halation_strength=12.0",
                        f"cam.halation_radius={r}",
                    ]
                    if hq:
                        a.append("cam.halation_high_quality=true")
                    J.append({"out": f"{out_dir}/halo_{ch}_{H}_{tag}_r{r}.png", "rm": rm, "res": res, "args": a})
            hqa = (
                ["cam.halation_enabled=true", "cam.halation_strength=0.001", "cam.halation_high_quality=true"]
                if hq
                else []
            )
            J.append({"out": f"{out_dir}/diff_{ch}_{H}_ref.png", "rm": rm, "res": res, "args": base(H) + hqa})
            for black in ("false", "true"):
                J.append({
                    "out": f"{out_dir}/diff_{ch}_{H}_{'black' if black == 'true' else 'std'}.png",
                    "rm": rm,
                    "res": res,
                    "args": base(H)
                    + hqa
                    + [
                        "env.diffusion_filter_enabled=true",
                        "env.diffusion_filter_strength=1.0",
                        f"env.diffusion_filter_black={black}",
                    ],
                })
            src = base(H, "0.3,0.25")
            J.append({"out": f"{out_dir}/flare_{ch}_{H}_ref.png", "rm": rm, "res": res, "args": src + hqa})
            for n in (1, 2):
                J.append({
                    "out": f"{out_dir}/flare_{ch}_{H}_g{n}.png",
                    "rm": rm,
                    "res": res,
                    "args": src
                    + hqa
                    + [
                        "env.lens_flare_enabled=true",
                        "env.lens_flare_intensity=100.0",
                        "env.lens_flare_threshold=0.0",
                        "env.lens_flare_halo_intensity=0.0",
                        f"env.lens_flare_ghost_count={n}",
                        "env.lens_flare_chromatic_aberration=0.0",
                    ],
                })
    return J


def halation(out_dir, hs):
    print(
        f"{'chain':5s} {'H':>4s} {'src':3s} {'radius':>6s} {'model px':>9s} {'floor':>6s} {'expect':>7s} {'2D fit':>7s} {'err%':>6s} {'marg':>7s} {'err%':>6s} {'shift y':>7s}"
    )
    rows = []
    for ch in CHAINS_RUN:
        for H in hs:
            for tag in ("c", "9"):
                S = load(f"{out_dir}/halo_{ch}_{H}_{tag}_ref.png")
                for r in RADII:
                    A = load(f"{out_dir}/halo_{ch}_{H}_{tag}_r{r}.png")
                    model = 0.112 * float(r) / 24.0 * H
                    fl = floor_px(ch, H)
                    exp_ = max(model, fl)
                    s2, _ = fit_2d(S, A, exp_)
                    m = fit_pair(S, A, exp_)
                    s1 = 0.5 * (m["sx"] + m["sy"])
                    row = dict(
                        chain=ch,
                        H=H,
                        src=tag,
                        radius=float(r),
                        model=model,
                        floor=fl,
                        expect=exp_,
                        fit2d=s2,
                        marg=s1,
                        shift=m["my"],
                    )
                    rows.append(row)
                    print(
                        f"{ch:5s} {H:4d} {tag:3s} {r:>6s} {model:9.2f} {fl:6.2f} {exp_:7.2f} {s2:7.2f} {100 * (s2 / exp_ - 1):+6.1f} {s1:7.2f} {100 * (s1 / exp_ - 1):+6.1f} {m['my']:+7.2f}",
                        flush=True,
                    )
    return rows


CHAINS_RUN = os.environ.get("CHAINS", "fp,hq,mob").split(",")


def _diff_model(S, sig_px, b):
    from scipy.ndimage import gaussian_filter

    return 0.25 * sum(gaussian_filter(S, sig_px * 2**i / b, mode="constant", truncate=4.0) for i in range(4)) - S


def diffusion(out_dir, hs):
    from scipy.optimize import minimize_scalar

    rows = []
    for ch in CHAINS_RUN:
        for H in hs:
            S3 = load(f"{out_dir}/diff_{ch}_{H}_ref.png")
            for kind, base_frac in (("std", 0.01), ("black", 0.005)):
                A3 = load(f"{out_dir}/diff_{ch}_{H}_{kind}.png")
                sig = base_frac * H
                b = max(1, int(sig / 3))
                S = _block(S3[..., 2], b)
                D = _block(A3[..., 2], b) - S

                def resid(k):
                    M = _diff_model(S, sig * k, b)
                    s = (M * D).sum() / (M * M).sum()
                    return ((D - s * M) ** 2).sum(), s

                r = minimize_scalar(lambda k: resid(k)[0], bounds=(0.3, 3), method="bounded", options={"xatol": 1e-4})
                k = r.x
                s = resid(k)[1]
                fl = floor_px(ch, H)
                note = f" (first octave {sig:.1f} px, floor {fl:.1f} px)" if sig < fl else ""
                rows.append(dict(chain=ch, H=H, kind=kind, k=k, s=s))
                print(
                    f"diffusion {ch:4s} {H:4d} {kind:5s}: size factor {k:.3f} ({100 * (k - 1):+.1f}%), fraction {s:.4f} (model {0.15 if kind == 'std' else 0.075}){note}",
                    flush=True,
                )
    return rows


def flare(out_dir, hs):
    from scipy.ndimage import gaussian_filter, map_coordinates
    from scipy.optimize import minimize_scalar

    rows = []
    mags = {1: -0.55, 2: -1.1}
    for ch in CHAINS_RUN:
        for H in hs:
            S3 = load(f"{out_dir}/flare_{ch}_{H}_ref.png")
            S = S3[..., 1]
            Hh, Ww = S.shape
            prev = S
            for n in (1, 2):
                A = load(f"{out_dir}/flare_{ch}_{H}_g{n}.png")[..., 1]
                D = A - prev
                prev = A
                k = mags[n]
                model = (0.004 + 0.008 * abs(k)) * H
                ys, xs = np.mgrid[0:Hh, 0:Ww]
                gu = 0.5 + ((xs + 0.5) / Ww - 0.5) / k
                gv = 0.5 + ((ys + 0.5) / Hh - 0.5) / k
                coords = [gv * Hh - 0.5, gu * Ww - 0.5]
                # Fit only around the ghost (where the model is not ~0), to keep it fast.
                cy, cx = np.unravel_index(np.argmax(np.abs(D)), D.shape)
                R = int(min(8 * model * abs(k), Hh // 2))
                sl = (slice(max(cy - R, 0), cy + R), slice(max(cx - R, 0), cx + R))
                cs = [c[sl] for c in coords]
                Dw = D[sl]

                def resid(sig):
                    Mf = map_coordinates(
                        gaussian_filter(S, sig, mode="constant", truncate=4.0), cs, order=1, mode="constant"
                    )
                    c = (Mf * Dw).sum() / (Mf * Mf).sum()
                    return ((Dw - c * Mf) ** 2).sum(), c

                r = minimize_scalar(
                    lambda s: resid(s)[0], bounds=(0.3, model * 3), method="bounded", options={"xatol": 1e-3}
                )
                fl = floor_px(ch, H)
                exp_ = max(model, fl)
                rows.append(dict(chain=ch, H=H, ghost=n, model=model, fit=r.x))
                print(
                    f"flare {ch:4s} {H:4d} ghost {n} (k {k}): blur {r.x:7.2f} px in the source's frame, model {model:6.2f} ({100 * (r.x / exp_ - 1):+.1f}%)",
                    flush=True,
                )
    return rows


if __name__ == "__main__":
    mode, out_dir = sys.argv[1], sys.argv[2]
    hs = [int(h) for h in sys.argv[3].split(",")] if len(sys.argv) > 3 else [720, 900, 1080, 1440, 2160]
    if mode == "jobs":
        J = jobs(out_dir, hs)
        os.makedirs(out_dir, exist_ok=True)
        json.dump(J, open(f"{out_dir}/jobs.json", "w"))
        print(len(J), "jobs")
    else:
        rows = {"halation": halation, "diffusion": diffusion, "flare": flare}[mode](out_dir, hs)
        json.dump(rows, open(f"{out_dir}/{mode}.json", "w"), indent=0)
