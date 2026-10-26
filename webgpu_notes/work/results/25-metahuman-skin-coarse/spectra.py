import sys

import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None


def spec(a, mm_per_px):
    # a: HxW normal xy in [-1,1]; slope energy spectrum, radially binned in cycles/mm
    g = a - a.mean((0, 1))
    win = np.outer(np.hanning(g.shape[0]), np.hanning(g.shape[1]))
    P = sum(np.abs(np.fft.fft2(g[:, :, c] * win)) ** 2 for c in range(2))
    f = np.sqrt(np.fft.fftfreq(g.shape[0])[:, None] ** 2 + np.fft.fftfreq(g.shape[1])[None, :] ** 2) / mm_per_px
    return f.ravel(), P.ravel()


def summary(name, f, P, lo=0.2, hi=None):
    # band: wavelengths 0.2 mm (or the Nyquist) .. 5 mm
    m = (f > 1 / 5.0) & (f < (hi or 1e9))
    ff, pp = f[m], P[m]
    order = np.argsort(ff)
    ff, pp = ff[order], pp[order]
    c = np.cumsum(pp)
    med = ff[np.searchsorted(c, c[-1] / 2)]
    # rms slope in band
    print(
        f"{name:<34} median wavelength of slope energy {1 / med:.2f} mm; slope rms(band) {np.sqrt(pp.sum()) / len(f):.3f}"
    )


H = np.asarray(Image.open(sys.argv[1]).convert("RGB")).astype(np.float32) / 127.5 - 1
D = np.asarray(Image.open(sys.argv[2]).convert("RGB")).astype(np.float32) / 127.5 - 1
cmuv = {"cheek": 38.2, "nose": 33.8, "forehead": 37.5}
boxes = {"cheek": (0.27, 0.47), "nose": (0.45, 0.415), "forehead": (0.46, 0.21)}
for k, (u, v) in boxes.items():
    x, y = int(u * 8192), int(v * 8192)
    crop = H[y : y + 512, x : x + 512, :2]
    mmpx = cmuv[k] * 10 / 8192
    f, P = spec(crop, mmpx)
    summary(f"8K Head_Normal {k} ({mmpx:.3f} mm/px)", f, P)
    # same crop as the 2048 import keeps it (box filter 4x)
    c2 = crop.reshape(128, 4, 128, 4, 2).mean((1, 3))
    f, P = spec(c2, mmpx * 4)
    summary(f"  ...at 2048 ({mmpx * 4:.3f} mm/px)", f, P)
for T in (16, 24, 32, 48):
    mmpx = 38.0 * 10 / T / 1024
    f, P = spec(D[:, :, :2], mmpx)
    summary(f"detail map, tiling {T} on 38 cm/UV ({mmpx:.4f} mm/px)", f, P)
