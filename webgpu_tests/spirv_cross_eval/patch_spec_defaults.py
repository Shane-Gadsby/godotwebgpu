#!/usr/bin/env python3
"""patch_spec_defaults.py <in.spv> <out.spv> <manifest.json>: rewrites the default of every SpecId constant in a
spec_op_probes.py module to its alternate value, the way webgpu::patch_spirv_spec_constants() patches a SPIR-V module
for the legacy per-specialization path."""

import json
import struct
import sys

src, dst, manifest = sys.argv[1:4]
vals = {int(k): v for k, v in json.load(open(manifest))["_consts"]["alt"].items()}
raw = open(src, "rb").read()
w = list(struct.unpack(f"<{len(raw) // 4}I", raw))
spec_of = {}
i = 5
while i < len(w):
    n, op = w[i] >> 16, w[i] & 0xFFFF
    if op == 71 and w[i + 2] == 1:  # OpDecorate <id> SpecId <n>
        spec_of[w[i + 1]] = w[i + 3]
    i += n
i = 5
patched = 0
while i < len(w):
    n, op = w[i] >> 16, w[i] & 0xFFFF
    if op in (48, 49, 50) and w[i + 2] in spec_of and spec_of[w[i + 2]] in vals:
        v = vals[spec_of[w[i + 2]]]
        if op in (48, 49):
            w[i] = (n << 16) | (48 if v else 49)
        elif isinstance(v, float):
            w[i + 3] = struct.unpack("<I", struct.pack("<f", v))[0]
        else:
            w[i + 3] = v & 0xFFFFFFFF
        patched += 1
    i += n
assert patched > 0, "no spec constants patched in " + src
open(dst, "wb").write(struct.pack(f"<{len(w)}I", *w))
