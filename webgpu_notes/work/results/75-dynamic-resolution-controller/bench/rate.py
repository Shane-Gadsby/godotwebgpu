#!/usr/bin/env python3
# rate.py <run.json|native.txt>...: changes, and changes/min in the settled windows (from 15 s after start or after a
# load change, to the next load change or the end).
import json
import sys


def lines_of(p):
    if p.endswith(".json"):
        j = json.load(open(p))
        return [(r["n"], r["lines"]) for r in j["runs"]]
    return [(1, [json.loads(ln[5:]) for ln in open(p) if ln.startswith("[dr] ")])]


for p in sys.argv[1:]:
    for n, L in lines_of(p):
        start = next((ln for ln in L if ln["kind"] == "start"), None)
        summ = next((ln for ln in L if ln["kind"] == "summary"), None)
        if not summ:
            print(p, n, "no summary")
            continue
        end = summ["t"]
        ch = summ["changes"]
        cuts = [0.0] + [ln["t"] for ln in L if ln["kind"] == "load_change"] + [end]
        parts = []
        for a, b in zip(cuts, cuts[1:]):
            s0 = a + 15.0
            k = [c for c in ch if s0 <= c[0] < b]
            parts.append(f"{s0:.0f}-{b:.0f}s: {len(k)} changes = {len(k) / max(b - s0, 1) * 60:.1f}/min")
        st = {k: v for k, v in summ["states_frames"].items()}
        tot = sum(st.values())
        changes = [[round(c[0], 1), round(c[1], 2)] for c in ch]
        states = {k: f"{100 * v / tot:.0f}%" for k, v in st.items()}
        print(
            f"{p.split('/')[-1]} #{n} start_scale={start and start.get('start_scale')} final={summ['final_scale']:.2f} "
            f"{summ['final_state']} source={summ['source']} | {'; '.join(parts)} | changes at {changes} | states {states}"
        )
