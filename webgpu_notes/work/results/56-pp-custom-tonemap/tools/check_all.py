"""Task 56: every check on the renders gpu_job.sh made. usage: check_all.py <out dir> <tools dir> [plots dir]
1. Custom curve chart cases against curve_ref.py, per renderer (and the web).
2. Existing tonemappers: new editor vs pre-change editor, pixel for pixel, per renderer.
3. Web vs native Forward+: max and mean abs difference per case.
4. Plots of each parameter's effect (model curves, with the Forward+ readback as points).
"""

import os
import sys

import numpy as np
from PIL import Image

out, tools = sys.argv[1], sys.argv[2]
plots = sys.argv[3] if len(sys.argv) > 3 else None
sys.path.insert(0, tools)
from analyze_chart import expected, sample_chart  # noqa: E402
from curve_ref import DEFAULTS, chart_levels, curve  # noqa: E402

KEYS = {"slope": "slope", "toe": "toe", "shoulder": "shoulder", "black": "black", "white": "white"}


def read_cases(path):
    cases = []
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, query = line.split(None, 1)
        args = dict(kv.split("=", 1) for kv in query.split("&"))
        cases.append((name, args))
    return cases


custom = read_cases(os.path.join(tools, "cases_custom.txt"))
existing = read_cases(os.path.join(tools, "cases_existing.txt"))
ok = True

print("== 1. Custom curve vs the numpy model (8-bit code values over 768 channel samples)")
for renderer, d in [
    ("forward_plus", "new_forward_plus"),
    ("mobile", "new_mobile"),
    ("gl_compatibility", "new_gl_compatibility"),
    ("web", "web"),
]:
    for name, args in custom:
        if args.get("scene") == "lit":
            continue
        png = os.path.join(out, d, name + ".png")
        if not os.path.exists(png):
            print(f"  MISSING {renderer} {name}")
            ok = False
            continue
        params = dict(DEFAULTS)
        for k in KEYS:
            if k in args:
                params[k] = float(args[k])
        exposure = float(args.get("exposure", 1.0))
        got = sample_chart(png)
        exp = expected("forward_plus" if renderer == "web" else renderer, params, exposure)
        err = np.abs(got - exp)
        flag = "" if err.max() <= 1.0 else "  <-- over 1/255"
        if renderer in ("forward_plus", "web") and err.max() > 1.0:
            ok = False
        print(
            f"  {renderer:16s} {name:12s} max {err.max():.3f} mean {err.mean():.3f} >1: {(err > 1.0).sum():3d}/768{flag}"
        )

print("== 2. Existing tonemappers: new editor vs pre-change editor (pixels differing, max difference)")
for renderer in ["forward_plus", "mobile", "gl_compatibility"]:
    for name, _ in existing:
        a = os.path.join(out, f"new_{renderer}", name + ".png")
        b = os.path.join(out, f"pre_{renderer}", name + ".png")
        if not (os.path.exists(a) and os.path.exists(b)):
            print(f"  MISSING {renderer} {name}")
            ok = False
            continue
        ia = np.asarray(Image.open(a).convert("RGB"), dtype=np.int16)
        ib = np.asarray(Image.open(b).convert("RGB"), dtype=np.int16)
        diff = np.abs(ia - ib)
        n = int((diff.max(axis=2) > 0).sum())
        if n:
            ok = False
        print(f"  {renderer:16s} {name:14s} {n:7d} px differ, max {int(diff.max())}")

print("== 3. Web (WebGPU, Chrome) vs native Forward+")
for name, _ in custom + existing:
    a = os.path.join(out, "web", name + ".png")
    b = os.path.join(out, "new_forward_plus", name + ".png")
    if not (os.path.exists(a) and os.path.exists(b)):
        print(f"  MISSING {name}")
        continue
    ia = np.asarray(Image.open(a).convert("RGB"), dtype=np.int16)
    ib = np.asarray(Image.open(b).convert("RGB"), dtype=np.int16)
    if ia.shape != ib.shape:
        print(f"  {name:14s} size {ia.shape} vs {ib.shape}")
        ok = False
        continue
    diff = np.abs(ia - ib)
    print(f"  {name:14s} max {int(diff.max())} mean {diff.mean():.4f} px>1: {int((diff.max(axis=2) > 1).sum())}")

if plots:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    os.makedirs(plots, exist_ok=True)
    x = np.logspace(-3.6, 1.8, 600)
    lv = chart_levels()
    fig, axes = plt.subplots(2, 3, figsize=(15, 9))
    # (parameter, [(value, the chart case rendered with it)]).
    sweeps: list[tuple[str, list[tuple[float, str]]]] = [
        ("slope", [(0.6, "c_slope_lo"), (0.88, "c_default"), (1.0, "c_slope_hi")]),
        ("toe", [(0.3, "c_toe_lo"), (0.55, "c_default"), (0.8, "c_toe_hi"), (0.9, "c_toe_vhi")]),
        ("shoulder", [(0.0, "c_sh_lo"), (0.26, "c_default"), (0.6, "c_sh_hi")]),
        ("black", [(0.0, "c_default"), (0.05, "c_black")]),
        ("white", [(0.0, "c_white0"), (0.04, "c_default"), (0.3, "c_white_hi")]),
    ]
    for ax, (key, points) in zip(axes.flat, sweeps):
        for v, case in points:
            p = dict(DEFAULTS)
            p[key] = v
            (line,) = ax.plot(
                np.log10(x), curve(x, **p), label=f"{key} = {v}" + (" (default)" if DEFAULTS[key] == v else "")
            )
            png = os.path.join(out, "new_forward_plus", case + ".png")
            if os.path.exists(png):
                got = sample_chart(png) / 255.0
                # Back to linear output for the plot (inverse sRGB of the readback).
                lin = np.where(got <= 0.04045, got / 12.92, ((got + 0.055) / 1.055) ** 2.4)
                ax.plot(np.log10(lv.reshape(-1)), lin.reshape(-1), ".", ms=2.5, color=line.get_color(), alpha=0.6)
        ax.axvline(np.log10(0.18), color="0.7", lw=0.8)
        ax.axhline(0.18, color="0.7", lw=0.8)
        ax.set_title(f"tonemap_custom_{key}" if key not in ("black", "white") else f"tonemap_custom_{key}_clip")
        ax.set_xlabel("log10 scene value")
        ax.set_ylabel("display value (linear)")
        ax.legend(fontsize=8)
    ax = axes.flat[5]
    ax.plot(np.log10(x), curve(x), label="Custom (defaults)")
    ax.plot(np.log10(x), np.clip(x, 0, 1), label="Linear", ls=":")
    ax.set_title("model (lines) and Forward+ readback (dots)")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(plots, "custom_curve_parameters.png"), dpi=80)
    print("plot written")

print("ALL OK" if ok else "SOME CHECKS FAILED")
