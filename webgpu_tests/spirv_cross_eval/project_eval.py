"""Convert a project's captured stages (project_export.sh) with both converters and tally the differences.

Usage: python3 project_eval.py <spv_dir> <out_dir> <sx_cli> <tint_cli>
Writes <out>/sx/<hash>.wgsl, <out>/tint/<hash>.wgsl, <out>/failures.json ({hash: {sx: err|null, tint: err|null}}),
and prints stages / converted per backend. Then run compare_interface.py <out>/tint <out>/sx (stages both converted)
and validate_wgsl.mjs <out>/sx.
"""
import json, os, subprocess, sys

spv_dir, out, sx_cli, tint_cli = sys.argv[1:5]
files = sorted(f for f in os.listdir(spv_dir) if f.endswith(".spv"))
fail = {}
for label, cli in (("sx", sx_cli), ("tint", tint_cli)):
    os.makedirs(os.path.join(out, label), exist_ok=True)
    ok = 0
    for i in range(0, len(files), 100):
        chunk = [os.path.join(spv_dir, f) for f in files[i:i + 100]]
        r = subprocess.run([cli, "--batch"] + chunk, capture_output=True, text=True)
        try:
            res = json.loads(r.stdout)
        except Exception:
            res = {}
        for p in chunk:
            h = os.path.basename(p)[:-4]
            v = res.get(p)
            if isinstance(v, str):
                ok += 1
                open(os.path.join(out, label, h + ".wgsl"), "w").write(v)
                fail.setdefault(h, {})[label] = None
            else:
                fail.setdefault(h, {})[label] = json.dumps(v) if v is not None else "no result (converter crashed?)"
    print(f"{label}: {len(files)} stages, {ok} converted")
json.dump(fail, open(os.path.join(out, "failures.json"), "w"), indent=1)
only = [h for h, v in fail.items() if v["sx"] and not v["tint"]]
print("sx-only failures:", len(only), " tint-only failures:", len([h for h, v in fail.items() if v["tint"] and not v["sx"]]),
      " both:", len([h for h, v in fail.items() if v["tint"] and v["sx"]]))
