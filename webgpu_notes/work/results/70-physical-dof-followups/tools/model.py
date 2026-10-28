#!/usr/bin/env python3
# Thin-lens blur radius in pixels of a point at distance d (m), lens f (mm), N, focus u (m), frame_mm, frame_px.
import math
import sys


def radius_px(f, N, u, d, frame_mm, frame_px):
    u_mm = max(u * 1000.0, f + 1.0)
    c_inf = f * f / (N * (u_mm - f))
    return 0.5 * c_inf * abs(d * 1000 - u_mm) / (d * 1000) / frame_mm * frame_px


# Predicted 10-90 % widths of a defocused vertical edge for each gather: circle (disc LSF, semicircle), box (uniform
# +-L), hexagon (the two diagonals at 30 degrees from horizontal: uniform +-0.866 L).
def predictions(L):
    # disc: edge = integral of semicircle LSF; 10-90 % solve numerically
    def disc_cdf(x):
        t = max(-1.0, min(1.0, x / L))
        return 0.5 + (t * math.sqrt(1 - t * t) + math.asin(t)) / math.pi

    def solve(p):
        lo, hi = -L, L
        for _ in range(60):
            mid = 0.5 * (lo + hi)
            if disc_cdf(mid) < p:
                lo = mid
            else:
                hi = mid
        return lo

    return {"circle_r2s": L, "circle_w": solve(0.9) - solve(0.1), "box_w": 1.6 * L, "hex_w": 1.6 * 0.866 * L}


if __name__ == "__main__":
    a = [float(x) for x in sys.argv[1:]]
    L = radius_px(*a)
    print(f"L={L:.3f}", {k: round(v, 3) for k, v in predictions(L).items()})
