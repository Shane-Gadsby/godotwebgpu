"""Compare the resource interface of two WGSL directories, file by file.

For every <name>.wgsl in <reference_dir>, checks that <candidate_dir>/<name>.wgsl declares the same
(group, binding, kind) set and the same `@id(N) override` ids. Those are what the driver's bind-group layouts and
pipeline constants are built from, so a mismatch is a real incompatibility, not a cosmetic one.
Also prints the total WGSL bytes of each side. Exit status 1 on any mismatch.

Usage: python3 compare_interface.py <reference_dir> <candidate_dir>
"""

import os
import re
import sys

BINDING = re.compile(r"@group\((\d+)u?\)\s*@binding\((\d+)u?\)\s*var(<[^>]*>)?\s*\w+\s*:\s*([^;]+);")
OVERRIDE = re.compile(r"@id\((\d+)u?\)\s*override")


def interface(text):
    bindings = set()
    for m in BINDING.finditer(text):
        ty = re.sub(r"\s", "", m.group(4))
        space = re.sub(r"\s", "", m.group(3) or "")
        if space:
            kind = space
        elif ty.startswith("texture_storage"):
            kind = ty
        elif "comparison" in ty:
            kind = "sampler_comparison"
        else:
            kind = re.sub(r"<.*", "", ty)
        bindings.add((int(m.group(1)), int(m.group(2)), kind))
    return bindings, {int(x) for x in OVERRIDE.findall(text)}


ref_dir, cand_dir = sys.argv[1], sys.argv[2]
same = missing = 0
mismatches = []
ref_bytes = cand_bytes = 0
names = sorted(f for f in os.listdir(ref_dir) if f.endswith(".wgsl"))
for name in names:
    ref = open(os.path.join(ref_dir, name)).read()
    cand_path = os.path.join(cand_dir, name)
    if not os.path.exists(cand_path):
        missing += 1
        continue
    cand = open(cand_path).read()
    ref_bytes += len(ref)
    cand_bytes += len(cand)
    rb, ro = interface(ref)
    cb, co = interface(cand)
    if rb == cb and ro == co:
        same += 1
    else:
        mismatches.append((name, sorted(rb - cb)[:4], sorted(cb - rb)[:4], sorted(ro ^ co)[:6]))

print(f"identical interface: {same}/{len(names)}, mismatched: {len(mismatches)}, missing in candidate: {missing}")
if ref_bytes:
    print(f"WGSL bytes: reference {ref_bytes}, candidate {cand_bytes} ({cand_bytes / ref_bytes:.3f}x)")
for name, only_ref, only_cand, ovr in mismatches[:20]:
    print(
        f"  {name}\n    only in reference: {only_ref}\n    only in candidate: {only_cand}\n    override ids differing: {ovr}"
    )
sys.exit(1 if mismatches else 0)
