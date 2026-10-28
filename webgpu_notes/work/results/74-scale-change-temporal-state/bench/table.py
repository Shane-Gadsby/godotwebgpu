#!/usr/bin/env python3
# table.py <before.jsonl> <after.jsonl> [frames...]: L / d per frame relative to the change, before vs after.
import json
import sys


def load(p):
    for line in open(p):
        if line.startswith("[cap] "):
            j = json.loads(line[6:])
            if j["kind"] == "capture":
                if p.endswith(".json"):
                    pass
                return j
    try:
        j = json.load(open(p))
        return next(ln for ln in j["lines"] if ln["kind"] == "capture")
    except Exception:
        raise SystemExit(f"no capture in {p}")


b, a = load(sys.argv[1]), load(sys.argv[2])
frames = [int(x) for x in sys.argv[3:]] or [-2, -1, 0, 1, 2, 3, 6, 12, 20]
rb = {r[0]: r for r in b["rows_rel_lum_dprev"]}
ra = {r[0]: r for r in a["rows_rel_lum_dprev"]}
base_b = rb[-1][1]
base_a = ra[-1][1]
print(f"{b['up']} ae={b['ae']} {b['s0']}->{b['s1']}")
print("| frame | before: L (rel) / d | after: L (rel) / d |")
print("|---|---|---|")
for f in frames:
    if f in rb and f in ra:
        print(
            f"| {f} | {rb[f][1]:.4f} ({rb[f][1] / base_b - 1:+.1%}) / {rb[f][2]:.4f} | {ra[f][1]:.4f} ({ra[f][1] / base_a - 1:+.1%}) / {ra[f][2]:.4f} |"
        )


def summary(r, base):
    post = [r[k] for k in r if k >= 0]
    pre = [r[k][2] for k in r if -6 <= k < 0 and k != -8]
    return max(abs(x[1] / base - 1) for x in post), max(x[2] for x in post), sum(pre) / max(len(pre), 1)


sb, sa = summary(rb, base_b), summary(ra, base_a)
print(
    f"max |dL| after change: before {sb[0]:.1%}, after {sa[0]:.1%}; max d: before {sb[1]:.4f}, after {sa[1]:.4f}; steady d before change: {sb[2]:.4f} / {sa[2]:.4f}"
)
