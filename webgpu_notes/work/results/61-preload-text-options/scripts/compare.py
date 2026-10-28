#!/usr/bin/env python3
"""Pixel-compares the start-screen screenshots of preload_text_test.mjs runs, and the layout of every run against the
default one. usage: compare.py <runs_dir>"""

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


PAIRS = [("base", "default"), ("base", "nokey"), ("base_nosize", "default_nosize"), ("wg_base", "wg_default")]
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
            ia, ib = np.asarray(Image.open(fa).convert("RGBA")), np.asarray(Image.open(fb).convert("RGBA"))
            ta = json.loads((d / f"{browser}_{a}.json").read_text())["states"][state]["text"]
            tb = json.loads((d / f"{browser}_{b}.json").read_text())["states"][state]["text"]
            same_text = ta == tb
            diff = int(np.count_nonzero(np.any(ia != ib, axis=-1))) if ia.shape == ib.shape else -1
            check(same_text and diff == 0, f"{browser} {a} vs {b} {state}: texts {ta!r} / {tb!r}, {diff} pixels differ")
    # Layout: every run's button and line top/height match the default run (a message switched off, or another text,
    # never moves anything).
    refs = {
        shell: json.loads((d / f"{browser}_{shell}default.json").read_text())["states"]["go"] for shell in ("", "wg_")
    }
    for f in sorted(d.glob(f"{browser}_*.json")):
        r = json.loads(f.read_text())
        if r.get("iframe"):
            continue
        ref = refs["wg_" if r["name"].startswith("wg_") else ""]
        for state, s in r["states"].items():
            same = s["button"] == ref["button"] and [s["line"][1], s["line"][3]] == [ref["line"][1], ref["line"][3]]
            if not same:
                check(
                    False,
                    f"{f.stem} {state}: layout {s['button']} {s['line']} vs default {ref['button']} {ref['line']}",
                )
    print(f"{browser}: layout of every direct run checked against its shell's default run")
print(f"COMPARE FAIL ({fails})" if fails else "COMPARE PASS")
sys.exit(1 if fails else 0)
