import json
import math
import sys

import numpy as np

d = json.load(open(sys.argv[1]))
fps = d["fps"]
D = {k: np.array(v) for k, v in d["data"].items()}
fails = 0


def check(name, ok, detail):
    global fails
    print(("PASS " if ok else "FAIL ") + name + "  " + detail)
    fails += 0 if ok else 1


names = ["pitch", "yaw", "roll", "x", "y", "z"]
amp = [0.3, 0.2, 0.1, 0.002, 0.001, 0.003]
freq = [2.0, 3.0, 1.5, 1.0, 1.5, 4.0]
b = D["base"]
n = len(b)
freqs = np.fft.rfftfreq(n, 1 / fps)


def spec(x):
    X = np.fft.rfft(x - x.mean())
    return np.abs(X) ** 2


for i in range(6):
    x = b[:, i]
    rms = math.sqrt(np.mean(x**2))
    P = spec(x)
    m = freqs > 0
    centroid = math.exp(np.sum(P[m] * np.log(freqs[m])) / np.sum(P[m]))
    inband = np.sum(P[(freqs >= freq[i] / 2 * 0.95) & (freqs <= freq[i] * 2 * 1.05)]) / np.sum(P)
    check(f"RMS {names[i]}", abs(rms / amp[i] - 1) < 0.05, f"{rms:.5g} (set {amp[i]})")
    check(
        f"spectrum {names[i]}: log-centroid at set frequency, energy within f/2..2f",
        abs(centroid / freq[i] - 1) < 0.1 and inband > 0.98,
        f"centroid {centroid:.3f} Hz (set {freq[i]}), in band {inband * 100:.1f}%",
    )
# IS: over each axis's own band (f/2..2f), the amplitude ratio must match the model 1 - s * HP(f), HP(f) the
# stabilizer's first-order gyro high-pass at 0.5 Hz; for axes whose band lies above 1.5 Hz that is close to 1 - s.
for key, s in [("is50", 0.5), ("is90", 0.9)]:
    for i in range(6):
        P0 = spec(b[:, i])
        P1 = spec(D[key][:, i])
        band = (freqs >= freq[i] / 2 * 0.95) & (freqs <= freq[i] * 2 * 1.05)
        r = math.sqrt(np.sum(P1[band]) / np.sum(P0[band]))
        hp = freqs[band] / np.sqrt(freqs[band] ** 2 + 0.25)
        exp_r = math.sqrt(np.sum(P0[band] * (1 - s * hp) ** 2) / np.sum(P0[band]))
        hi = freq[i] / 2 >= 1.5  # HP >= 0.95 over the band
        ok = abs(r - exp_r) < 0.005 and (not hi or abs(r - (1 - s)) < 0.02)
        check(
            f"IS {s}: {names[i]} band amplitude ratio",
            ok,
            f"{r:.4f} (model {exp_r:.4f}" + (f", 1-s = {1 - s:.2f})" if hi else ")"),
        )
br = D["breath"][:, 0]
rms = math.sqrt(np.mean(br**2))
P = spec(br)
peak = freqs[np.argmax(P)]
check(
    "breathing RMS / peak",
    abs(rms / 0.15 - 1) < 0.1 and abs(peak - 0.25) < 0.06,
    f"rms {rms:.4f} deg (0.15), peak {peak:.3f} Hz (0.25)",
)
check("breathing alone leaves other axes still", np.max(np.abs(D["breath"][:, 1:])) < 1e-6, "")
t = D["fl_off_tele"]
r = math.sqrt(np.mean(t[:, 0] ** 2)) / math.sqrt(np.mean(b[:, 0] ** 2))
check(
    "scale_with_focal_length off: angular shake x FOV/27 deg",
    abs(r - 0.5) < 0.01,
    f"ratio {r:.4f} at half the reference FOV",
)
rp = math.sqrt(np.mean(t[:, 3] ** 2)) / math.sqrt(np.mean(b[:, 3] ** 2))
check("scale_with_focal_length off: position unchanged", abs(rp - 1) < 1e-6, f"{rp:.6f}")
print(f"ANALYSIS DONE, failures: {fails}")
sys.exit(1 if fails else 0)
