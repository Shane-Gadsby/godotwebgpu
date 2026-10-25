"""Convert a project's captured stages (project_export.sh) with one converter build, or two to compare them.

Usage: python3 project_eval.py <spv_dir> <out_dir> <cli> [<reference_cli>]
Writes <out>/cli/<hash>.wgsl (and <out>/ref/<hash>.wgsl with a reference build, e.g. one with a SPIRV-Cross patch
reverted), <out>/failures.json ({hash: {cli: err|null, ref: err|null}}), and prints stages / converted per build.
Then run compare_interface.py <out>/ref <out>/cli (stages both converted) and validate_wgsl.mjs <out>/cli.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys

spv_dir, out, cli_path = sys.argv[1:4]
builds = [("cli", cli_path)] + ([("ref", sys.argv[4])] if len(sys.argv) > 4 else [])
files = sorted(f for f in os.listdir(spv_dir) if f.endswith(".spv"))
fail: dict[str, dict[str, str | None]] = {}
for label, cli in builds:
    os.makedirs(os.path.join(out, label), exist_ok=True)
    ok = 0
    for i in range(0, len(files), 100):
        chunk = [os.path.join(spv_dir, f) for f in files[i : i + 100]]
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
if len(builds) > 1:
    print(
        "cli-only failures:",
        len([h for h, v in fail.items() if v["cli"] and not v["ref"]]),
        " ref-only failures:",
        len([h for h, v in fail.items() if v["ref"] and not v["cli"]]),
        " both:",
        len([h for h, v in fail.items() if v["ref"] and v["cli"]]),
    )
