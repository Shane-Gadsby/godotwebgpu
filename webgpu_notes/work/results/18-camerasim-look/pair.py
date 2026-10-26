#!/usr/bin/env python3
# Task 18: side-by-side PNGs (native left, WebGPU right, labeled, <= 1600 px wide) and per-view mean absolute
# difference (0-255, RGB, whole frame and the 3D-only region right of the UI panel).
#   pair.py <native_dir> <web_dir> <out_dir> <label_right> [views...]
# Prints one TSV row per view: view, MAD whole, MAD 3D region, % pixels differing by > 16 in any channel.
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

nat, web, out, label = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]), sys.argv[4]
views = sys.argv[5:] or ["default", "face_200mm", "dof_f1_4", "hair_side_200mm", "face_200mm_lights"]
out.mkdir(parents=True, exist_ok=True)
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 22)
except OSError:
    font = ImageFont.load_default()
print("view\tmad\tmad_3d\tpct_gt16\tsize")
for v in views:
    a = Image.open(nat / f"{v}.png").convert("RGB")
    b = Image.open(web / f"{v}.png").convert("RGB")
    if a.size != b.size:
        print(f"{v}\tSIZE MISMATCH {a.size} vs {b.size}")
        b = b.resize(a.size)
    A = np.asarray(a, dtype=np.int16)
    B = np.asarray(b, dtype=np.int16)
    d = np.abs(A - B)
    x0 = int(A.shape[1] * 0.22)  # the camera panel covers the left ~21% of the frame
    print(
        f"{v}\t{d.mean():.2f}\t{d[:, x0:].mean():.2f}\t{(d.max(axis=2) > 16).mean() * 100:.2f}\t{a.size[0]}x{a.size[1]}"
    )
    w = 800
    h = round(a.size[1] * w / a.size[0])
    canvas = Image.new("RGB", (2 * w, h + 34), (20, 20, 20))
    canvas.paste(a.resize((w, h), Image.LANCZOS), (0, 34))
    canvas.paste(b.resize((w, h), Image.LANCZOS), (w, 34))
    dr = ImageDraw.Draw(canvas)
    dr.text((8, 5), f"native Vulkan  |  {v}", fill=(255, 255, 255), font=font)
    dr.text((w + 8, 5), f"WebGPU ({label})  |  {v}", fill=(255, 255, 255), font=font)
    canvas.save(out / f"{v}.png", optimize=True)
