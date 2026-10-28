#!/usr/bin/env python3
"""sim.py: the glare chain's blur per lod, from the shaders' sampling arithmetic (1D: every pass is separable).

Forward+ (copy.glsl MODE_GAUSSIAN_BLUR + MODE_GLOW, _render_camera_glare_chain()): dest texel i of each level is
sum_k w_k bilinear(source, u = (i + k + 0.5) / n_dest), k = -4..4, w = {0.2024, 0.1790, 0.1240, 0.0672, 0.0285}.
Level 0 reads the frame (n = H >> 1, or H for the full-resolution chain), level L reads level L - 1 (n >> 1).
Mobile (blur_raster.glsl GLOW_GATHER then GLOW_DOWNSAMPLE): level 0 texel j is the mean of bilinear reads at frame
pixel coordinates 4j + 1 and 4j + 3 (a 4-pixel box); level L texel j averages bilinear reads at (2j + 1 -+ 0.75) of
level L - 1. The tonemap pass reads level L bilinearly at the frame pixel's UV.
For a point source at many sub-texel positions, prints the output's standard deviation (frame pixels) per lod.
"""

import sys

import numpy as np

ABOUT_SOURCE = True
W = np.array([0.0285, 0.0672, 0.1240, 0.1790, 0.2024, 0.1790, 0.1240, 0.0672, 0.0285])


def bilinear_matrix(n_src, coords):
    """Rows: bilinear weights of n_src texels at texel coordinates `coords` (texel centers at i + 0.5), clamped."""
    x = coords - 0.5
    i0 = np.floor(x).astype(int)
    t = x - i0
    M = np.zeros((len(coords), n_src))
    for r, (a, tt) in enumerate(zip(i0, t)):
        M[r, min(max(a, 0), n_src - 1)] += 1 - tt
        M[r, min(max(a + 1, 0), n_src - 1)] += tt
    return M


def fp_level(n_src, n_dst):
    M = np.zeros((n_dst, n_src))
    for k, w in zip(range(-4, 5), W):
        i = np.arange(n_dst)
        # The shader clamps the read UV to [0.5, n - 1.5] texels of the destination.
        u = np.clip(i + k + 0.5, 0.5, n_dst - 1.5) / n_dst
        M += w * bilinear_matrix(n_src, u * n_src)
    return M


def mob_gather(n_src, n_dst):
    j = np.arange(n_dst)
    bp = np.clip(j * 4.0, 0, max(n_src - 4, 0))
    return 0.5 * (bilinear_matrix(n_src, bp + 1.0) + bilinear_matrix(n_src, bp + 3.0))


def mob_down(n_src, n_dst):
    j = np.arange(n_dst)
    tc = 2 * j + 1.0
    return 0.5 * (bilinear_matrix(n_src, tc - 0.75) + bilinear_matrix(n_src, tc + 0.75))


def chain(H, kind):
    red = {"fp": 1, "hq": 0, "mob": 2}[kind]
    sizes = [H >> red]
    while sizes[-1] > 1:
        sizes.append(sizes[-1] >> 1)
    mats = []
    if kind == "mob":
        mats.append(mob_gather(H, sizes[0]))
        for a, b in zip(sizes, sizes[1:]):
            mats.append(mob_down(a, b))
    else:
        mats.append(fp_level(H, sizes[0]))
        for a, b in zip(sizes, sizes[1:]):
            mats.append(fp_level(a, b))
    # Cumulative: level L from the frame.
    cum = []
    acc = None
    for m in mats:
        acc = m if acc is None else m @ acc
        cum.append(acc)
    return sizes, cum


def per_lod_sigma(H, kind, positions):
    sizes, cum = chain(H, kind)
    y = (np.arange(H) + 0.5) / H
    out = []
    for L, (n, C) in enumerate(zip(sizes, cum)):
        R = bilinear_matrix(n, y * n) @ C  # frame px -> frame px
        sig = []
        for p in positions:
            col = R[:, p]
            if col.sum() <= 0:
                continue
            # About the source's own position: a mip's texel grid also moves the halo's center, which a fit over
            # several points (each at its own sub-texel position) sees as width.
            m = p if ABOUT_SOURCE else (col * np.arange(H)).sum() / col.sum()
            sig.append(np.sqrt((col * (np.arange(H) - m) ** 2).sum() / col.sum()))
        out.append((L, n, float(np.sqrt(np.mean(np.square(sig)))), float(np.std(sig))))
    return out


if __name__ == "__main__":
    kinds = sys.argv[1].split(",") if len(sys.argv) > 1 else ["fp", "hq", "mob"]
    Hs = [int(h) for h in sys.argv[2].split(",")] if len(sys.argv) > 2 else [720, 900, 1080, 1440, 2160]
    for kind in kinds:
        red = {"fp": 1, "hq": 0, "mob": 2}[kind]
        for H in Hs:
            pos = list(range(int(0.35 * H), int(0.65 * H), 3))
            res = per_lod_sigma(H, kind, pos)
            line = " ".join(f"{s * (H >> red) / H:7.3f}" for (L, n, s, sd) in res[:9])
            print(f"{kind:3s} {H:4d} sigma per lod in lod-0 texels: {line}")
