#!/usr/bin/env python3
"""Compare Godot AO debug-draw captures against the ray traced reference (rt_reference.py).

Usage: compare_ref.py ref.npz label=image.png [label=image.png ...]
Prints, over geometry pixels: bias (mean signed error), MAE, RMSE, correlation, and the mean AO in three regions
(open ground, contact/crease band, everything occluded in the reference), plus the error after a 5x5 box blur of
both (which removes the reference's Monte Carlo noise and the methods' residual noise).
"""

import sys

import numpy as np
from PIL import Image


def box5(a):
    k = 5
    p = np.pad(a, k // 2, mode="edge")
    c = np.cumsum(np.cumsum(p, 0), 1)
    c = np.pad(c, ((1, 0), (1, 0)))
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)


ref = np.load(sys.argv[1])
ao_ref = ref["ao"]
hit = ref["hit"]
# Pixels whose 3x3 neighborhood is all geometry (silhouette pixels mix depths and are excluded).
inner = hit.copy()
for dy in (-1, 0, 1):
    for dx in (-1, 0, 1):
        inner &= np.roll(np.roll(hit, dy, 0), dx, 1)
occluded = inner & (ao_ref < 0.9)
open_ = inner & (ao_ref > 0.98)
ref_b = box5(ao_ref)
print(f"pixels: geometry {inner.sum()}, reference AO < 0.9: {occluded.sum()}, open (> 0.98): {open_.sum()}")
print(f"reference mean AO: all {ao_ref[inner].mean():.4f}, occluded {ao_ref[occluded].mean():.4f}")
print("label            bias     MAE    RMSE   corr | occl.mean  occl.bias | open.mean | blurred MAE")
for arg in sys.argv[2:]:
    label, path = arg.split("=", 1)
    img = np.asarray(Image.open(path).convert("RGB")).astype(np.float64) / 255.0
    ao = img[..., 0]
    if ao.shape != ao_ref.shape:
        print(f"{label}: size {ao.shape} != {ao_ref.shape}")
        continue
    e = ao - ao_ref
    m = inner
    corr = np.corrcoef(ao[m], ao_ref[m])[0, 1]
    eb = box5(ao) - ref_b
    print(
        f"{label:15s} {e[m].mean():+.4f} {np.abs(e[m]).mean():.4f} {np.sqrt((e[m] ** 2).mean()):.4f} {corr:.4f} |"
        f"   {ao[occluded].mean():.4f}   {e[occluded].mean():+.4f} |   {ao[open_].mean():.4f} | {np.abs(eb[m]).mean():.4f}"
    )
