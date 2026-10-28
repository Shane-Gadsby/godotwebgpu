#!/usr/bin/env python3
"""mixcurve.py <caldir> <out.json>: the fitted size of an explicit blend (1 - w) lod L + w lod L+1, from the integer-lod
captures (exact: the blend is linear in the captures). Records y = (sigma_w - sigma_L) / (sigma_L+1 - sigma_L) per w."""

import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from halofit import fit_2d, fit_pair, load

out_dir = sys.argv[1]
out = []
for ch in ["fp", "hq", "mob"]:
    for H in [720, 1080, 2160]:
        S = load(f"{out_dir}/{ch}_{H}_ref.png")
        Ls = sorted(
            int(f.split("_L")[1][:-8].split(".")[0])
            for f in os.listdir(out_dir)
            if f.startswith(f"{ch}_{H}_L") and f.endswith(".00.png.f32")
        )
        for L in Ls:
            if L + 1 not in Ls:
                continue
            a = load(f"{out_dir}/{ch}_{H}_L{L}.00.png")
            b = load(f"{out_dir}/{ch}_{H}_L{L + 1}.00.png")
            sa, _ = fit_2d(S, a, 2.0 * 2**L * H / (H >> {"fp": 1, "hq": 0, "mob": 2}[ch]) / 1.0)
            sb, _ = fit_2d(S, b, 2 * sa)
            ra = fit_pair(S, a, sa)
            rb = fit_pair(S, b, sb)
            ma, mb = (ra["sx"] + ra["sy"]) / 2, (rb["sx"] + rb["sy"]) / 2
            for w in np.linspace(0.1, 0.9, 9):
                m = (1 - w) * a + w * b
                s2, _ = fit_2d(S, m, (1 - w) * sa + w * sb)
                r = fit_pair(S, m, s2)
                s1 = (r["sx"] + r["sy"]) / 2
                row = dict(
                    chain=ch,
                    H=H,
                    L=L,
                    w=w,
                    sa=sa,
                    sb=sb,
                    s=s2,
                    y=(s2 - sa) / (sb - sa),
                    ma=ma,
                    mb=mb,
                    m=s1,
                    ym=(s1 - ma) / (mb - ma),
                )
                out.append(row)
                print(
                    f"{ch} {H} L{L} w {w:.1f}: 2D y {row['y']:.3f} (ratio {sb / sa:.2f})  marg y {row['ym']:.3f}",
                    flush=True,
                )
json.dump(out, open(sys.argv[2], "w"), indent=0)
