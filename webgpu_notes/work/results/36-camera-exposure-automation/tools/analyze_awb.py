# analyze_awb.py <dir> <ref> <series prefix> <frames...>: auto white balance against an independent Python estimator.
# The estimator follows the model text (camera_attributes_storage.cpp, "Auto white balance") with textbook matrices
# (IEC 61966-2-1 sRGB -> XYZ, Lam's Bradford) and colour-science's Ohno 2013 CCT for the range check, iterated to its
# fixed point on the AWB-off frame; it predicts the gray card the converged frames should show.
import json
import sys
import warnings

import numpy as np

warnings.filterwarnings("ignore")
import colour  # noqa: E402 (after silencing its import-time warnings)

d, ref_name, prefix = sys.argv[1], sys.argv[2], sys.argv[3]
frames = [int(f) for f in sys.argv[4:]]
CARD = (slice(260, 340), slice(600, 680))  # the gray card at 1280x720


def load(p):
    j = json.load(open(f"{d}/{p}.json"))
    return np.fromfile(f"{d}/{p}.f32", dtype=np.float32).reshape(j["h"], j["w"], 3).astype(np.float64)


SRGB_TO_XYZ = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
BRADFORD = np.array([[0.8951, 0.2664, -0.1614], [-0.7502, 1.7135, 0.0367], [0.0389, -0.0685, 1.0296]])
TO_LMS = BRADFORD @ SRGB_TO_XYZ
FROM_LMS = np.linalg.inv(TO_LMS)


def uv(rgb):
    X, Y, Z = SRGB_TO_XYZ @ rgb
    dd = X + 15 * Y + 3 * Z
    return np.array([4 * X / dd, 6 * Y / dd])


WHITE_UV = uv(np.ones(3))


def balance(img, gains):
    return np.einsum("ij,...j->...i", FROM_LMS, gains * np.einsum("ij,...j->...i", TO_LMS, img))


F = load(ref_name)
flat = F.reshape(-1, 3)
gains = np.ones(3)
for it in range(200):
    b = balance(flat, gains)
    ok = (flat.max(axis=1) < 1.0) & np.all(b > 0, axis=1)
    dlog = np.log(b[ok][:, [0, 2]] / b[ok][:, [1]])
    w = np.exp(-0.5 * (dlog**2).sum(axis=1) / 0.25**2)
    est = (w[:, None] * flat[ok]).sum(axis=0) / w.sum()
    lms_est = TO_LMS @ (est / (SRGB_TO_XYZ @ est)[1])
    new_gains = (TO_LMS @ np.ones(3)) / lms_est
    if np.abs(new_gains - gains).max() < 1e-9:
        break
    gains = new_gains
xy = colour.XYZ_to_xy(SRGB_TO_XYZ @ est)
cct, duv = colour.uv_to_CCT(colour.xy_to_UCS_uv(xy), method="Ohno 2013")
print(f"python estimator: {it} iterations, illuminant CCT {cct:.0f} K, Duv {duv:+.4f} (light: 3200 K blackbody)")
card_ref = F[CARD].mean(axis=(0, 1))
card_pred = balance(card_ref, gains)
print(
    f"gray card, AWB off:            r/g {card_ref[0] / card_ref[1]:.4f} b/g {card_ref[2] / card_ref[1]:.4f} |duv| from white {np.linalg.norm(uv(card_ref) - WHITE_UV):.5f}"
)
print(
    f"gray card, predicted converged: r/g {card_pred[0] / card_pred[1]:.4f} b/g {card_pred[2] / card_pred[1]:.4f} |duv| {np.linalg.norm(uv(card_pred) - WHITE_UV):.5f}"
)
deltas = json.load(open(f"{d}/{prefix}.deltas.json"))
t = np.cumsum(deltas)
for f in frames:
    c = load(f"{prefix}.{f}")[CARD].mean(axis=(0, 1))
    print(
        f"frame {f:4d} t={t[f - 1]:6.2f}s: r/g {c[0] / c[1]:.4f} b/g {c[2] / c[1]:.4f} |duv| {np.linalg.norm(uv(c) - WHITE_UV):.5f}  vs predicted |duv| {np.linalg.norm(uv(c) - uv(card_pred)):.5f}"
    )
