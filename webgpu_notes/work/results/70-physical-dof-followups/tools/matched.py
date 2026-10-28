#!/usr/bin/env python3
# Worker-70: the CameraAttributesPractical settings that give a physical camera's thin-lens blur through upstream's
# physical-mode path (transitions -1): amount = the engine's float32 radius / 64, and the same vertical FOV.
# matched.py f N focus_m sensor_h_mm H  ->  "amount=... fov=... far=... near=..."
import math
import sys

import numpy as np

f, N, u_m, sh, H = (float(x) for x in sys.argv[1:6])
f32 = np.float32
u = max(f32(u_m) * f32(1000.0), f32(f) + f32(1.0))
scale = (f32(f) / (u - f32(f))) * (f32(f) / f32(N))
frac = scale / f32(sh)
radius = f32(0.5) * frac * f32(H)
fov = math.degrees(2 * math.atan(sh / (2 * f)))
print(f"amount={float(radius) / 64.0:.10g} fov={fov:.10g} far={float(u) / 1000:.10g} near={float(u) / 1000:.10g}")
