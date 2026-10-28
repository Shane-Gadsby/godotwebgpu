"""Compare the resource interface of WGSL modules against a reference, file by file.

For every <name>.wgsl in the reference, checks that <candidate_dir>/<name>.wgsl declares the same
(group, binding, kind) set and the same `@id(N) override` ids. Those are what the driver's bind-group layouts and
pipeline constants are built from, so a mismatch is a real incompatibility, not a cosmetic one.

The reference is either a directory of WGSL files (two converter builds side by side) or a manifest written by
--write. interface_reference.json beside this script is such a manifest: the interface of every engine variant
wgsl_precompile.py builds, as Tint (the converter before SPIRV-Cross) produced it when SPIRV-Cross was accepted
as its replacement (webgpu_notes/TASKS.md §2.35). webgpu_tests/converter_smoke/run.sh compares against it. Against a
manifest, a reference module missing from the candidate fails too (a renamed or removed variant must be a
deliberate manifest update, or the check would silently cover less); a candidate module the manifest does not
know is listed, not failed. After a deliberate interface change (a pass that moves bindings, a new variant),
regenerate the manifest from the candidate with --write and say why in the commit.

Usage: python3 compare_interface.py <reference_dir | manifest.json> <candidate_dir>
       python3 compare_interface.py --write <manifest.json> <wgsl_dir> [source description]
Exit status 1 on any mismatch.
"""

import json
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


def read_dir(path):
    out = {}
    for name in sorted(f for f in os.listdir(path) if f.endswith(".wgsl")):
        with open(os.path.join(path, name)) as f:
            text = f.read()
        out[name] = (interface(text), len(text))
    return out


if len(sys.argv) >= 4 and sys.argv[1] == "--write":
    manifest_path, wgsl_dir = sys.argv[2], sys.argv[3]
    modules = read_dir(wgsl_dir)
    source = sys.argv[4] if len(sys.argv) > 4 else ""
    entries = {
        name: {"bindings": sorted(list(b) for b in bindings), "overrides": sorted(overrides)}
        for name, ((bindings, overrides), _) in modules.items()
    }
    # One line per module, so a regenerated manifest diffs as the modules that changed.
    with open(manifest_path, "w") as f:
        f.write(f'{{"source": {json.dumps(source)},\n "modules": {{\n')
        f.write(",\n".join(f"  {json.dumps(n)}: {json.dumps(e)}" for n, e in entries.items()))
        f.write("\n }\n}\n")
    print(f"wrote {len(modules)} modules to {manifest_path}")
    sys.exit(0)

if len(sys.argv) != 3:
    sys.exit(__doc__)

ref_path, cand_dir = sys.argv[1], sys.argv[2]
from_manifest = os.path.isfile(ref_path)
if from_manifest:
    with open(ref_path) as f:
        manifest = json.load(f)
    reference = {
        name: ({tuple(b) for b in entry["bindings"]}, set(entry["overrides"]))
        for name, entry in manifest["modules"].items()
    }
    ref_bytes = None
else:
    ref_modules = read_dir(ref_path)
    reference = {name: iface for name, (iface, _) in ref_modules.items()}
    ref_bytes = 0
candidate = read_dir(cand_dir)

same = 0
missing = []
mismatches = []
cand_bytes = 0
for name in sorted(reference):
    if name not in candidate:
        missing.append(name)
        continue
    (cb, co), size = candidate[name]
    cand_bytes += size
    if ref_bytes is not None:
        ref_bytes += ref_modules[name][1]
    rb, ro = reference[name]
    if rb == cb and ro == co:
        same += 1
    else:
        mismatches.append((name, sorted(rb - cb)[:4], sorted(cb - rb)[:4], sorted(ro ^ co)[:6]))
unknown = sorted(set(candidate) - set(reference))

print(
    f"identical interface: {same}/{len(reference)}, mismatched: {len(mismatches)}, missing in candidate: {len(missing)}"
    + (f", not in reference: {len(unknown)}" if from_manifest else "")
)
if ref_bytes:
    print(f"WGSL bytes: reference {ref_bytes}, candidate {cand_bytes} ({cand_bytes / ref_bytes:.3f}x)")
for name, only_ref, only_cand, ovr in mismatches[:20]:
    print(
        f"  {name}\n    only in reference: {only_ref}\n    only in candidate: {only_cand}\n    override ids differing: {ovr}"
    )
if from_manifest:
    for name in missing[:10]:
        print(f"  missing in candidate: {name}")
    for name in unknown[:10]:
        print(f"  not in reference (no check): {name}")
sys.exit(1 if mismatches or (from_manifest and missing) else 0)
