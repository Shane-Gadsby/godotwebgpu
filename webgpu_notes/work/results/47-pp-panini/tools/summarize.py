"""summarize.py <dir>:<case,case...> ...: one block of numbers per case (analyze.py's checks)."""

import sys

from analyze import analyze

for rm_dir in sys.argv[1:]:
    d, cases = rm_dir.split(":")
    print("==", d.split("/w47/")[-1])
    for case in cases.split(","):
        o = analyze(d, case)
        dd = o["dots"]
        r = o.get("rod", {})
        poles = o["poles"]
        print(
            f"{case:<9} fit {o['fit_scale']:.6f} (closed form {o['closed_form_fit']:.6f}) dots {dd['matched']:3d}"
            f" err max {dd['max_err_px']:.3f} mean {dd['mean_err_px']:.3f}"
            f" | unproject-vs-blob max {dd['unproject_vs_measured_max_px']:.3f}"
            f" | poles {len(poles)} ptp max {max(p['peak_to_peak_px'] for p in poles):.3f}"
            f" std max {max(p['std_px'] for p in poles):.3f}"
            f" | rod err max {r.get('max_err_px', -1):.3f} sag {r.get('sag_center_to_edge_px', 0):.1f}"
        )
        shapes = " ".join(
            f"az{a['az']} {a['measured']:.3f}/{a['expected']:.3f} ({a['rectilinear']:.3f}) c{a['centroid_err_px']:.2f}"
            for a in sorted(o["shapes"], key=lambda a: a["az"])
            if a["az"] >= 0
        )
        print("          shape w/h (measured/expected, rectilinear):", shapes)
