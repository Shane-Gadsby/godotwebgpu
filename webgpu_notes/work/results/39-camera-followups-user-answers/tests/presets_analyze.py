# presets_analyze.py <presets.json>: each preset's recorded shake against its documented values. Per axis, from the
# spectrum (Parseval): the RMS in the axis's own band (f/2..2f) must be the set amplitude times the stabilizer's gain
# (1 - s HP(f), HP a first-order 0.5 Hz high-pass, averaged over the band) within 5%, and its log-power centroid the
# set frequency (moved down by the stabilizer's gain, for the stabilized preset) within 10%; pitch's breathing band (+-0.3 octave) is checked the same way within 12% (a narrow band
# decorrelates slowly, so 120 s reads a few % low, task 38).
import json
import math
import sys

import numpy as np

d = json.load(open(sys.argv[1]))
fps = d["fps"]
fails = 0
names = ["pitch", "yaw", "roll", "x", "y", "z"]
labels = {
    1: "Handheld",
    2: "Handheld Stabilized",
    3: "Handheld Walking",
    4: "Running",
    5: "Shoulder Rig",
    6: "Gimbal",
    7: "Tripod in Wind",
    8: "Vehicle Mount",
}


def gain(f0, lo, hi, s):
    f = np.exp(np.linspace(math.log(f0 * lo), math.log(f0 * hi), 4000))
    hp = f / np.sqrt(f * f + 0.25)
    return math.sqrt(np.mean((1 - s * hp) ** 2))


def centroid(f0, lo, hi, s):
    # The stabilizer cuts the high end of a band more than the low end, so the band's centroid moves down with it.
    f = np.exp(np.linspace(math.log(f0 * lo), math.log(f0 * hi), 4000))
    w = (1 - s * f / np.sqrt(f * f + 0.25)) ** 2
    return math.exp(np.sum(w * np.log(f)) / np.sum(w))


for key, doc in d["doc"].items():
    rot_a, rot_f, pos_a, pos_f, br_a, br_f, s = doc
    amp = rot_a + pos_a
    freq = rot_f + pos_f
    X = np.array(d["data"][key])
    n = len(X)
    freqs = np.fft.rfftfreq(n, 1 / fps)
    line = []
    for i in range(6):
        x = X[:, i] - X[:, i].mean()
        P = np.abs(np.fft.rfft(x)) ** 2
        P[1:-1] *= 2  # one-sided
        band = (freqs >= freq[i] / 2 * 0.95) & (freqs <= freq[i] * 2 * 1.05)
        rms = math.sqrt(P[band].sum()) / n
        exp = amp[i] * gain(freq[i], 0.5, 2.0, s)
        cen = math.exp(np.sum(P[band] * np.log(freqs[band])) / np.sum(P[band]))
        cexp = centroid(freq[i], 0.5, 2.0, s)
        ok = abs(rms / exp - 1) < 0.05 and abs(cen / cexp - 1) < 0.10
        fails += 0 if ok else 1
        line.append(f"{names[i]} {rms:.4g}/{exp:.4g} @{cen:.2f}/{cexp:.2f}Hz" + ("" if ok else " FAIL"))
        if i == 0 and br_a > 0:
            bb = (freqs >= br_f * 2**-0.3 * 0.95) & (freqs <= br_f * 2**0.3 * 1.05)
            brms = math.sqrt(P[bb].sum()) / n
            bexp = br_a * gain(br_f, 2**-0.3, 2**0.3, s)
            bcen = math.exp(np.sum(P[bb] * np.log(freqs[bb])) / np.sum(P[bb]))
            bcexp = centroid(br_f, 2**-0.3, 2**0.3, s)
            ok = abs(brms / bexp - 1) < 0.12 and abs(bcen / bcexp - 1) < 0.10
            fails += 0 if ok else 1
            line.append(f"breath {brms:.4g}/{bexp:.4g} @{bcen:.2f}/{bcexp:.2f}Hz" + ("" if ok else " FAIL"))
    bad = any("FAIL" in item for item in line)
    print(
        ("FAIL " if bad else "PASS ")
        + f"{labels[int(key)]:20s} measured/expected RMS @ centroid/set: "
        + "; ".join(line)
    )
print(f"PRESET ANALYSIS DONE, failures: {fails}")
sys.exit(1 if fails else 0)
