#!/usr/bin/env python3
# Compatibility check. Its viewport image of this chart differs from Forward+'s before any grading (0.5 reads back as
# 54/255, identically in the base editor, with or without the post pass), so the grade is checked on what the post
# pass actually had in its sRGB-encoded slot: the "neutral" render (grading on, every control neutral = identity LUT)
# is that value per patch, and the case's render must equal the reference grade of it (LUT model and analytic).
import sys

import numpy as np

sys.argv, args = [sys.argv[0]], sys.argv[1:]
import grade_ref as g  # noqa: E402

d = args[0]
_, neutral = g.read_patches(f"{d}/neutral_lattice.png", "lattice")
x = neutral / 255.0
for c in args[1:]:
    _, got = g.read_patches(f"{d}/{c}_lattice.png", "lattice")
    dl = np.abs(got - np.round(g.pipeline(c, x.copy(), False) * 255))
    da = np.abs(got - g.pipeline(c, x.copy(), True) * 255)
    print(
        f"{c:13s} vs LUT model: max {dl.max():5.2f} mean {dl.mean():5.3f}  vs analytic: max {da.max():5.2f} mean {da.mean():5.3f}  (/255, {len(x)} patches)"
    )
