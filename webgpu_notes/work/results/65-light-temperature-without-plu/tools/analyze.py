#!/usr/bin/env python3
# ruff: noqa: UP031
"""analyze.py <run txt>...: neutral pairs and constant luminance from lt.gd's [LT] lines (linear HDR means)."""

import re
import sys


def load(path):
    d = {}
    for line in open(path):
        m = re.match(r"\[LT\] (\S+) ([-\d.]+) ([-\d.]+) ([-\d.]+) fmt (\d+)", line)
        if m:
            c = [float(x) for x in m.groups()[1:4]]
            if m.group(5) != "14":
                # Not an RGBAH (HDR, linear) capture: Compatibility returns sRGB-encoded 8-bit. Decode.
                c = [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c]
            if max(c) > 0:
                d[m.group(1)] = tuple(c)
    return d


def Y(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


for path in sys.argv[1:]:
    d = load(path)
    print("==", path)
    for prefix, ref in (("n64_lum_t", "n64_lum_t6500"), ("n64_max_t", "n64_max_t6500"), ("n65_t", "n65_t6500")):
        keys = [k for k in d if k.startswith(prefix)]
        if not keys:
            continue
        r = d[ref]
        worst_abs = worst_rel = 0
        for k in keys:
            c = d[k]
            ab = max(abs(c[0] / c[1] - 1), abs(c[2] / c[1] - 1))
            rel = max(abs(c[i] / r[i] - 1) for i in range(3))
            worst_abs = max(worst_abs, ab)
            worst_rel = max(worst_rel, rel)
            print(
                "  %-16s rgb %.5f %.5f %.5f  r/g %.4f b/g %.4f  vs 6500K-light default-WB: %+.3f%% %+.3f%% %+.3f%%"
                % (k, *c, c[0] / c[1], c[2] / c[1], *[(c[i] / r[i] - 1) * 100 for i in range(3)])
            )
        print(
            f"  {prefix}: worst |r/g-1|,|b/g-1| {worst_abs * 100:.3f}%; worst deviation from the 6500 K light at default WB {worst_rel * 100:.3f}%"
        )
    keys = [k for k in d if re.match(r"l65_t\d+$", k)]
    if keys:
        ys = [Y(d[k]) for k in keys]
        mean = sum(ys) / len(ys)
        for k in sorted(keys, key=lambda k: int(k[5:])):
            print("  %-10s Y %.5f (%+.3f%%)" % (k, Y(d[k]), (Y(d[k]) / mean - 1) * 100))
        print("  constant luminance 2000-10000 K: spread (max-min)/mean %.3f%%" % ((max(ys) - min(ys)) / mean * 100))
