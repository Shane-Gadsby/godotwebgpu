#!/usr/bin/env python3
# analyze.py <renderer>: bit-identical checks against the base editor, then every case against grade_ref.py.
import subprocess
import sys

import numpy as np
from PIL import Image

W = "/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w45"
m = sys.argv[1]
for name in ("off_lattice", "off_off", "user3d_only_lattice", "user1d_only_lattice"):
    a = np.asarray(Image.open(f"{W}/out/{m}/{name}.png")).astype(int)
    b = np.asarray(Image.open(f"{W}/out/base_{m}/{name}.png")).astype(int)
    d = np.abs(a - b)
    print(f"{m} base vs work/45 {name}: max diff {d.max()}, {int((d.max(-1) > 0).sum())} pixels differ")
CASES = "off user3d_only user1d_only neutral filter hue_shift vibrance vibrance_neg lift gamma gain smh mixer split curve_master curve_rgb hue_vs_hue hue_vs_sat sat_vs_sat lum_vs_sat all all_user3d all_user1d all_adjust".split()
for c in CASES:
    subprocess.run([sys.executable, f"{W}/grade_ref.py", f"{W}/out/{m}/{c}_lattice.png", c, "lattice"])
for c in "off neutral all hue_shift vibrance curve_master".split():
    subprocess.run([sys.executable, f"{W}/grade_ref.py", f"{W}/out/{m}/{c}_off.png", c, "off"])
