#!/usr/bin/env python3
# Web (WebGPU Forward+) vs native Forward+ (Vulkan) per case, and web vs the reference.
import subprocess
import sys

import numpy as np
from PIL import Image

W = "/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w45"
cases = [
    (c, "lattice")
    for c in "off user3d_only user1d_only neutral filter hue_shift vibrance vibrance_neg vibrance_min lift gamma gain smh mixer split curve_master curve_rgb hue_vs_hue hue_vs_sat sat_vs_sat lum_vs_sat all all_user3d all_user1d all_adjust".split()
]
cases += [(c, "off") for c in "off neutral all hue_shift vibrance curve_master".split()]
for c, ch in cases:
    a = np.asarray(Image.open(f"{W}/out/web/{c}_{ch}.png").convert("RGB")).astype(int)
    b = np.asarray(Image.open(f"{W}/out/forward_plus/{c}_{ch}.png").convert("RGB")).astype(int)
    d = np.abs(a - b)
    r = subprocess.run(
        [sys.executable, f"{W}/grade_ref.py", f"{W}/out/web/{c}_{ch}.png", c, ch], capture_output=True, text=True
    ).stdout.strip()
    print(f"web vs native {c}_{ch}: max {d.max()}, {int((d.max(-1) > 0).sum())} px differ | {r[22:]}")
