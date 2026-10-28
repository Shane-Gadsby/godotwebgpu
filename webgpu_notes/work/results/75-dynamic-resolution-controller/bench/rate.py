#!/usr/bin/env python3
# rate.py <run.json|native.txt>...: changes, and changes/min in the settled windows (from 15 s after start or after a
# load change, to the next load change or the end).
import json, sys
def lines_of(p):
    if p.endswith('.json'):
        j = json.load(open(p))
        return [(r['n'], r['lines']) for r in j['runs']]
    return [(1, [json.loads(l[5:]) for l in open(p) if l.startswith('[dr] ')])]
for p in sys.argv[1:]:
    for n, L in lines_of(p):
        start = next((l for l in L if l['kind'] == 'start'), None)
        summ = next((l for l in L if l['kind'] == 'summary'), None)
        if not summ:
            print(p, n, 'no summary'); continue
        end = summ['t']; ch = summ['changes']
        cuts = [0.0] + [l['t'] for l in L if l['kind'] == 'load_change'] + [end]
        parts = []
        for a, b in zip(cuts, cuts[1:]):
            s0 = a + 15.0
            k = [c for c in ch if s0 <= c[0] < b]
            parts.append('%.0f-%.0fs: %d changes = %.1f/min' % (s0, b, len(k), len(k) / max(b - s0, 1) * 60))
        st = {k: v for k, v in summ['states_frames'].items()}
        tot = sum(st.values())
        print('%s #%d start_scale=%s final=%.2f %s source=%s | %s | changes at %s | states %s' % (p.split('/')[-1], n, start and start.get('start_scale'), summ['final_scale'], summ['final_state'], summ['source'], '; '.join(parts), [[round(c[0], 1), round(c[1], 2)] for c in ch], {k: '%.0f%%' % (100 * v / tot) for k, v in st.items()}))
