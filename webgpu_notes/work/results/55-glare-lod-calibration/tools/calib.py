#!/usr/bin/env python3
"""calib.py jobs|fit <outdir> [chains] [heights] [lod step]: per-lod blur of the glare chain, measured with halation.

With the PRE-CHANGE editor, halation samples lod log2(sigma_frame * chain_h / c) (c = 2.68 Forward+, both chains;
0.80 raster), sigma_frame = 0.112 radius / 24 (physical camera, 36x24 sensor). So a radius is picked per target lod
L: radius = c 2^L 24 / (0.112 chain_h). Each capture's halo is fitted (halofit.py) and reported in frame pixels, in
lod-0 texels and in frame heights.
chains: fp (Forward+ half resolution), hq (Forward+ full resolution, halation_high_quality), mob (Mobile, raster).
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

CHAINS = {
    "fp": ("forward_plus", 1, 2.68, False),
    "hq": ("forward_plus", 0, 2.68, True),
    "mob": ("mobile", 2, 0.80, False),
}
# Points at frame-relative positions (half extents), spread over sub-texel phases, inside +-0.3.
PTS = ";".join(
    f"{-0.3 + 0.6 * ((i * 0.6180339) % 1):.4f},{-0.3 + 0.6 * ((i * 0.4142136 + 0.1) % 1):.4f}" for i in range(9)
)
BASE = [
    "scene=points",
    f"points={PTS}",
    "point_size=0.03",
    "point_energy=40",
    "cam.frustum_focal_length=50.0",
    "cam.frustum_focus_distance=20.0",
    "cam.exposure_aperture=16.0",
    "hdr=1",
]


def heights():
    return [720, 900, 1080, 1440, 2160]


def lods(step, chain_h, max_sigma_frame=0.10, c=2.68):
    out = []
    L = 0.0
    while c * 2**L / chain_h <= max_sigma_frame + 1e-9:
        out.append(round(L, 4))
        L += step
    return out


def jobs(out_dir, chains, hs, step):
    J = []
    for ch in chains:
        rm, red, c, hq = CHAINS[ch]
        for H in hs:
            W = H * 16 // 9
            res = f"{W}x{H}"
            chain_h = H >> red
            J.append({"out": f"{out_dir}/{ch}_{H}_ref.png", "rm": rm, "res": res, "args": BASE})
            for L in lods(step, chain_h, c=c):
                r = c * 2**L * 24 / (0.112 * chain_h)
                a = BASE + ["cam.halation_enabled=true", "cam.halation_strength=12.0", f"cam.halation_radius={r:.6f}"]
                if hq:
                    a.append("cam.halation_high_quality=true")
                J.append({
                    "out": f"{out_dir}/{ch}_{H}_L{L:.2f}.png",
                    "rm": rm,
                    "res": res,
                    "args": a,
                    "L": L,
                    "radius": r,
                })
    return J


if __name__ == "__main__":
    mode, out_dir = sys.argv[1], sys.argv[2]
    chains = sys.argv[3].split(",") if len(sys.argv) > 3 else ["fp", "hq", "mob"]
    hs = [int(h) for h in sys.argv[4].split(",")] if len(sys.argv) > 4 else heights()
    step = float(sys.argv[5]) if len(sys.argv) > 5 else 0.25
    J = jobs(out_dir, chains, hs, step)
    if mode == "jobs":
        json.dump(J, open(f"{out_dir}/jobs.json", "w"), indent=0)
        print(len(J), "jobs ->", f"{out_dir}/jobs.json")
        sys.exit(0)
    from halofit import fit_2d, fit_pair, load

    rows = []
    refs = {}
    for j in J:
        if "L" not in j:
            continue
        ch = os.path.basename(j["out"]).split("_")[0]
        rm, red, c, hq = CHAINS[ch]
        H = int(j["res"].split("x")[1])
        chain_h = H >> red
        refp = f"{out_dir}/{ch}_{H}_ref.png"
        if refp not in refs:
            refs[refp] = load(refp)
        if not os.path.exists(j["out"] + ".f32"):
            print("MISSING", j["out"])
            continue
        A = load(j["out"])
        sig0 = c * 2 ** j["L"] * H / chain_h
        r = fit_pair(refs[refp], A, sig0)
        s = 0.5 * (r["sx"] + r["sy"])
        s2, _ = fit_2d(refs[refp], A, s)
        row = dict(
            chain=ch,
            H=H,
            chain_h=chain_h,
            L=j["L"],
            sx=r["sx"],
            sy=r["sy"],
            s_px=s,
            s_tex=s * chain_h / H,
            s_frame=s / H,
            f=r["f"],
            rel=r["rel"],
            mx=r["mx"],
            my=r["my"],
            s2d_px=s2,
            s2d_tex=s2 * chain_h / H,
        )
        rows.append(row)
        print(
            f"{ch:3s} {H:4d} L {j['L']:5.2f}: sigma {s:8.3f} px (x {r['sx']:7.3f} y {r['sy']:7.3f}) = {row['s_tex']:7.3f} lod-0 texels"
            f" = {row['s_frame']:.5f} FH; 2D {s2:8.3f} px = {row['s2d_tex']:7.3f} tex; f {r['f']:.3f} resid {r['rel']:.3f} shift ({r['mx']:+.2f},{r['my']:+.2f})"
        )
    json.dump(rows, open(f"{out_dir}/fits.json", "w"), indent=1)
