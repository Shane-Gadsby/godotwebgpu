"""Independent numpy model of the Custom tonemapper (task 56), in float64, written from the curve's definition (Unreal
Engine's Film tonemapper parameters: slope, toe, shoulder, black clip, white clip), not from the shader or the C++:
in L = log10(x), a straight line of the given slope, a tanh toe that takes over below the output (1 - toe) and falls to
-black_clip, a tanh shoulder that takes over above the output `shoulder` and rises to 1 + white_clip, both meeting the
line in value and slope; mid gray 0.18 maps to 0.18; where the shoulder starts before the toe ends, toe and shoulder
are blended across the overlap with a smoothstep; output clipped to [0, 1].
"""

import numpy as np

DEFAULTS = dict(slope=0.88, toe=0.55, shoulder=0.26, black=0.0, white=0.04)


def curve(x, slope=0.88, toe=0.55, shoulder=0.26, black=0.0, white=0.04):
    x = np.asarray(x, dtype=np.float64)
    L = np.log10(np.maximum(x, 1e-10))
    ts = 1.0 + black - toe  # toe height: from its end value (1 - toe) down to -black
    ss = 1.0 + white - shoulder  # shoulder height: from `shoulder` up to 1 + white
    Lg = np.log10(0.18)
    if 1.0 - toe > 0.18:
        # Mid gray on the toe: solve (1 - toe) + ts * tanh(slope / ts * (Lg - t0)) = 0.18 for t0.
        t0 = Lg - ts / slope * np.arctanh((0.18 - (1.0 - toe)) / ts)
        a = (1.0 - toe) / slope - t0  # the line passes through (t0, 1 - toe)
    else:
        a = 0.18 / slope - Lg  # mid gray on the line
        t0 = (1.0 - toe) / slope - a
    t1 = shoulder / slope - a  # the line reaches `shoulder` at t1
    line = slope * (L + a)
    toe_y = (1.0 - toe) + ts * np.tanh(slope / ts * (L - t0))
    sh_y = shoulder + ss * np.tanh(slope / ss * (L - t1))
    if t1 >= t0:
        y = np.where(L < t0, toe_y, np.where(L > t1, sh_y, line))
    else:
        s = np.clip((L - t1) / (t0 - t1), 0.0, 1.0)
        w = s * s * (3.0 - 2.0 * s)
        y = np.where(L <= t1, toe_y, np.where(L >= t0, sh_y, (1.0 - w) * toe_y + w * sh_y))
    return np.clip(y, 0.0, 1.0)


def srgb_encode(c):
    c = np.clip(np.asarray(c, dtype=np.float64), 0.0, 1.0)
    return np.where(c < 0.0031308, 12.92 * c, 1.055 * np.power(c, 1.0 / 2.4) - 0.055)


def srgb_encode_compat(c):
    # The Compatibility renderer's approximation (drivers/gles3/shaders/tonemap_inc.glsl, linear_to_srgb()).
    c = np.clip(np.asarray(c, dtype=np.float64), 0.0, 1.0)
    return np.maximum(1.055 * np.power(c, 0.416666667) - 0.055, 0.0)


def chart_levels():
    # tm_test.gd's ramp chart: patch i holds x_i (red), x_(i+85) (green), x_(i+170) (blue).
    i = np.arange(256)
    lv = np.exp2(-12.0 + 18.0 * i / 255.0)
    return np.stack([lv[i % 256], lv[(i + 85) % 256], lv[(i + 170) % 256]], axis=1)


if __name__ == "__main__":
    for x in [0.0, 0.001, 0.01, 0.05, 0.18, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 64.0]:
        print(f"{x:8.3f} -> {float(curve(x)):.5f}")
