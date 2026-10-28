# analyze_awb2.py <dir>: the clamp and bias cases. Predictions:
# - clamp4000: the estimate (3200 K) is clamped to 4000 K on the locus, so the card keeps the cast of a 3200 K light
#   corrected for a 4000 K one: gains white / LMS(Planck 4000 K), Planck from colour-science's blackbody (not Krystek).
# - bias5000: neutral, then the manual white balance (5000 K, tint 20) on top: its gains are the manual model's,
#   ported from RendererCameraAttributes::white_balance_compute_gains().
import json
import sys
import warnings

import numpy as np

warnings.filterwarnings("ignore")
import colour  # noqa: E402 (after silencing its import-time warnings)

d = sys.argv[1]
CARD = (slice(260, 340), slice(600, 680))
SRGB_TO_XYZ = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
BRADFORD = np.array([[0.8951, 0.2664, -0.1614], [-0.7502, 1.7135, 0.0367], [0.0389, -0.0685, 1.0296]])
TO_LMS = BRADFORD @ SRGB_TO_XYZ
FROM_LMS = np.linalg.inv(TO_LMS)


def load(p):
    j = json.load(open(f"{d}/{p}.json"))
    return np.fromfile(f"{d}/{p}.f32", dtype=np.float32).reshape(j["h"], j["w"], 3).astype(np.float64)


def card(p):
    return load(p)[CARD].mean(axis=(0, 1))


def ratios(c):
    return f"r/g {c[0] / c[1]:.4f} b/g {c[2] / c[1]:.4f}"


def balance(c, g):
    return FROM_LMS @ (g * (TO_LMS @ c))


ref = card("ref.10")
white_lms = TO_LMS @ np.ones(3)
# Clamp: illuminant = 4000 K blackbody.
xyz4000 = colour.sd_to_XYZ(colour.sd_blackbody(4000)) / 100
xyz4000 /= xyz4000[1]
g4000 = white_lms / (BRADFORD @ xyz4000)
pred = balance(ref, g4000)
got = card("clamp4000.300")
print(f"clamp 4000 K: measured {ratios(got)}   predicted {ratios(pred)}")


# Bias: the manual model.
def planck_uv(t):
    t2 = t * t
    return (
        (0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t2) / (1.0 + 8.42420235e-4 * t + 7.08145163e-7 * t2),
        (0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t2) / (1.0 - 2.89741816e-5 * t + 1.61456053e-7 * t2),
    )


def white_lms_manual(t, tint):
    u, v = planck_uv(t)
    if tint:
        u0, v0 = planck_uv(t - 1)
        u1, v1 = planck_uv(t + 1)
        du, dv = u1 - u0, v1 - v0
        ln = np.hypot(du, dv)
        duv = tint * 0.05 / 150
        u += dv / ln * duv
        v += -du / ln * duv
    dd = 2 * u - 8 * v + 4
    x, y = 3 * u / dd, 2 * v / dd
    return BRADFORD @ np.array([x / y, 1.0, (1 - x - y) / y])


gm = white_lms_manual(6500, 0) / white_lms_manual(5000, 20)
neutral = np.ones(3) * ref[1]
pred = balance(neutral, gm)
got = card("bias5000.300")
print(f"bias 5000 K / tint 20: measured {ratios(got)}   predicted {ratios(pred)}")
