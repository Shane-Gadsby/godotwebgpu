"""Reads tm_test.gd's ramp chart back from a render and compares it with curve_ref.py's model.

usage: analyze_chart.py <png> <renderer: forward_plus|mobile|gl_compatibility|web> [slope= toe= shoulder= black= white=
       exposure=] [--json]
Prints the max and mean error in 8-bit code values over the 768 channel samples (256 patches x RGB), and how many
exceed 1. The model's input is what the renderer stores: half floats in Forward+ (and the web), RGB10A2 holding x / 2
in Mobile (so nothing above 2.0), full floats in Compatibility (which tonemaps in the scene shader and encodes with
its own sRGB approximation).
"""

import json
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, __import__("os").path.dirname(__file__))
from curve_ref import DEFAULTS, chart_levels, curve, srgb_encode, srgb_encode_compat  # noqa: E402


def renderer_input(x, renderer):
    if renderer == "mobile":
        return np.round(np.clip(x / 2.0, 0.0, 1.0) * 1023.0) / 1023.0 * 2.0
    if renderer == "gl_compatibility":
        return x
    return x.astype(np.float16).astype(np.float64)


def sample_chart(png):
    img = np.asarray(Image.open(png).convert("RGB"), dtype=np.float64)
    h, w, _ = img.shape
    out = np.zeros((256, 3))
    for i in range(256):
        cy, cx = i // 16, i % 16
        y0 = int((cy + 0.5) * h / 16)
        x0 = int((cx + 0.5) * w / 16)
        out[i] = img[y0 - 4 : y0 + 4, x0 - 4 : x0 + 4].reshape(-1, 3).mean(axis=0)
    return out


def expected(renderer, params, exposure=1.0, tm="custom"):
    x = renderer_input(chart_levels(), renderer) * exposure
    y = curve(x, **params) if tm == "custom" else np.clip(x, 0.0, 1.0)
    enc = srgb_encode_compat(y) if renderer == "gl_compatibility" else srgb_encode(y)
    return enc * 255.0


def main():
    png, renderer = sys.argv[1], sys.argv[2]
    params = dict(DEFAULTS)
    exposure = 1.0
    tm = "custom"
    for a in sys.argv[3:]:
        if "=" in a:
            k, v = a.split("=", 1)
            if k == "exposure":
                exposure = float(v)
            elif k == "tm":
                tm = v
            else:
                params[k] = float(v)
    got = sample_chart(png)
    exp = expected(renderer, params, exposure, tm)
    err = np.abs(got - exp)
    res = dict(
        png=png,
        renderer=renderer,
        params=params,
        max=float(err.max()),
        mean=float(err.mean()),
        over1=int((err > 1.0).sum()),
        worst_patch=int(err.max(axis=1).argmax()),
    )
    if "--json" in sys.argv:
        print(json.dumps(res))
    else:
        print(
            f"{renderer:16s} max {res['max']:.3f} mean {res['mean']:.3f} >1: {res['over1']}/768 worst patch {res['worst_patch']}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
