#!/usr/bin/env python3
"""Side-by-side crop, native left, web right, 3x nearest-neighbour (task 17).

usage: crop.py NATIVE.png WEB.png OUT.png [x y w h]   (default: the checker sphere and the arch)
"""

import sys

from PIL import Image

nat, web, out = sys.argv[1:4]
x, y, w, h = (int(v) for v in sys.argv[4:8]) if len(sys.argv) >= 8 else (200, 90, 200, 120)
s, gap = 3, 8
a = Image.open(nat).convert("RGB").crop((x, y, x + w, y + h)).resize((w * s, h * s), Image.NEAREST)
b = Image.open(web).convert("RGB").crop((x, y, x + w, y + h)).resize((w * s, h * s), Image.NEAREST)
o = Image.new("RGB", (w * s * 2 + gap, h * s), (255, 255, 255))
o.paste(a, (0, 0))
o.paste(b, (w * s + gap, 0))
o.save(out)
