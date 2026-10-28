#!/usr/bin/env python3
# seqtable.py <before.json> <after.json>: web sequential captures, L and d_final per sample (frame relative to change).
import json
import sys


def load(p):
    j = json.load(open(p))
    c = next(ln for ln in j["lines"] if ln["kind"] == "capture")
    return c, j["adapter"]


(b, ab), (a, aa) = load(sys.argv[1]), load(sys.argv[2])
rb, ra = b["rows_rel_lum_dfinal"], a["rows_rel_lum_dfinal"]
pre_b = [r for r in rb if r[0] < 0][-1][1]
pre_a = [r for r in ra if r[0] < 0][-1][1]
print(f"{b['up']} ae={b['ae']} {b['s0']}->{b['s1']}  ({ab.split(' ')[1]})")
print("| sample | before: frame, L (rel), d_final | after: frame, L (rel), d_final |")
print("|---|---|---|")
for i in range(max(len(rb), len(ra))):
    x = rb[i] if i < len(rb) else None
    y = ra[i] if i < len(ra) else None

    def f(r, p):
        return f"{r[0]:+d}, {r[1]:.4f} ({r[1] / p - 1:+.1%}), {r[2]:.4f}" if r else ""

    print(f"| {i} | {f(x, pre_b)} | {f(y, pre_a)} |")


def post(r, p):
    return (max(abs(x[1] / p - 1) for x in r if x[0] >= 0), max(x[2] for x in r if 0 <= x[0] <= 12))


pb, pa = post(rb, pre_b), post(ra, pre_a)
print(
    f"max |dL| after change: before {pb[0]:.1%}, after {pa[0]:.1%}; max d_final within 12 frames: before {pb[1]:.4f}, after {pa[1]:.4f}"
)
