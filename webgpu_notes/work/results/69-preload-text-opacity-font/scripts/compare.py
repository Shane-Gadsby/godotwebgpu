#!/usr/bin/env python3
"""Task 69: pixel-compares the start-screen screenshots of preload_text_test.mjs runs (defaults against the webgpu-4.8
shells), checks that a chosen colour is drawn exactly (its own value appears in the line, nothing brighter than it),
and checks every run's layout against its shell's default run. usage: compare.py <runs_dir>"""

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

runs = Path(sys.argv[1])
fails = 0


def check(ok, what):
    global fails
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        fails += 1


def load(d, browser, name, state):
    return np.asarray(Image.open(d / f"{browser}_{name}_{state}.png").convert("RGB")).astype(int)


PAIRS = [("base", "default"), ("base", "nokey"), ("base_nosize", "default_nosize"), ("wg_base", "wg_default")]
# The line's colour at the old 85% over the default splash background, per channel, before rounding: 0.85*224+0.15*36.
# (Not wg_styled: in the webgpu-full-size shell the splash image reaches under the 32 px line's box.)
EXACT = {"exact_e0": (224, 224, 224), "styled": (255, 128, 0), "default": (196, 196, 196)}
# --tolerance N: a defaults pair also passes when no pixel differs by more than N levels and the new run adds no colour
# (subpixel antialiasing fringes). The browsers' text antialiasing depends on the text colour, so #c4c4c4 drawn opaque
# cannot match #e0e0e0 at 85% on glyph edges to the level; the strict run reports by how much.
TOL = int(sys.argv[sys.argv.index("--tolerance") + 1]) if "--tolerance" in sys.argv else 0
for browser in ("chrome", "firefox"):
    d = runs / browser
    if not d.exists():
        continue
    for a, b in PAIRS:
        for state in ("hold", "half", "go"):
            fa, fb = d / f"{browser}_{a}_{state}.png", d / f"{browser}_{b}_{state}.png"
            if not (fa.exists() and fb.exists()):
                check(False, f"{browser} {a} vs {b} {state}: screenshot missing")
                continue
            ia, ib = load(d, browser, a, state), load(d, browser, b, state)
            ta = json.loads((d / f"{browser}_{a}.json").read_text())["states"][state]["text"]
            tb = json.loads((d / f"{browser}_{b}.json").read_text())["states"][state]["text"]
            if ia.shape != ib.shape:
                check(False, f"{browser} {a} vs {b} {state}: sizes differ")
                continue
            diff = np.any(ia != ib, axis=-1)
            n = int(np.count_nonzero(diff))
            maxd = int(np.abs(ia - ib).max())
            x, y, w, h = (
                int(round(v)) for v in json.loads((d / f"{browser}_{b}.json").read_text())["states"][state]["line"]
            )
            box = ib[y : y + h, x : x + w]
            chroma = int((box.max(-1) - box.min(-1)).max())
            check(
                ta == tb and maxd <= TOL and chroma <= 2,
                f"{browser} {a} vs {b} {state}: texts {ta!r} / {tb!r}, {n} pixels differ (max {maxd} levels), max chroma {chroma}",
            )
    # A chosen colour reads back exactly: inside the line's box (the "Ready" state), the colour itself is drawn (glyph
    # interiors) and no pixel goes past it on any channel above the background (antialiasing only mixes toward the
    # background and the shadow).
    for name, rgb in EXACT.items():
        j = d / f"{browser}_{name}.json"
        if not j.exists():
            check(False, f"{browser} {name}: run missing")
            continue
        x, y, w, h = (int(round(v)) for v in json.loads(j.read_text())["states"]["go"]["line"])
        crop = load(d, browser, name, "go")[y : y + h, x : x + w].reshape(-1, 3)
        exact = int(np.count_nonzero(np.all(crop == rgb, axis=-1)))
        over = int(np.count_nonzero(np.any(crop > np.maximum(rgb, 36), axis=-1)))
        check(exact > 0 and over == 0, f"{browser} {name}: {exact} pixels exactly {rgb}, {over} brighter than it")
    # Layout: every direct run's button and line top/height match its shell's default run, except the styled runs,
    # whose bigger font makes the line taller (checked to be so).
    refs = {
        shell: json.loads((d / f"{browser}_{shell}default.json").read_text())["states"]["go"] for shell in ("", "wg_")
    }
    for f in sorted(d.glob(f"{browser}_*.json")):
        r = json.loads(f.read_text())
        if r.get("iframe"):
            continue
        ref = refs["wg_" if r["name"].startswith("wg_") else ""]
        for state, s in r["states"].items():
            if r["name"].endswith("styled"):
                if not s["line"][3] > ref["line"][3]:
                    check(
                        False,
                        f"{f.stem} {state}: 32 px line not taller than the default ({s['line']} vs {ref['line']})",
                    )
                continue
            same = s["button"] == ref["button"] and [s["line"][1], s["line"][3]] == [ref["line"][1], ref["line"][3]]
            if not same:
                check(
                    False,
                    f"{f.stem} {state}: layout {s['button']} {s['line']} vs default {ref['button']} {ref['line']}",
                )
    print(f"{browser}: layout of every direct run checked against its shell's default run")
print(f"COMPARE FAIL ({fails})" if fails else "COMPARE PASS")
sys.exit(1 if fails else 0)
