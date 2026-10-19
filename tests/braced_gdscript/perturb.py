"""Mangles a .gdb file's free-form whitespace: indentation, blank-line runs, trailing spaces, spaces near brackets."""

import random
import re
import sys

src, dst, seed = sys.argv[1], sys.argv[2], int(sys.argv[3])
rng = random.Random(seed)
out = []
in_block_comment = False
for line in open(src, encoding="utf-8").read().split("\n"):
    if in_block_comment or "/*" in line and "*/" not in line:
        in_block_comment = "*/" not in line if in_block_comment else True
        if in_block_comment and "*/" in line:
            in_block_comment = False
        out.append(line)
        continue
    body = line.lstrip(" \t")
    if not body:
        out.extend([""] * rng.randint(1, 3) if out and out[-1] != "" else [""])
        continue
    if '"' not in body and "'" not in body and "//" not in body and "#" not in body and "/*" not in body:
        body = re.sub(r" +", lambda m: " " * rng.randint(1, 3), body)
        body = re.sub(r"([(\[]) +", r"\1", body) if rng.random() < 0.5 else body
        body = re.sub(r" +([)\],])", r"\1", body) if rng.random() < 0.5 else body
    lead = "".join(rng.choice([" ", "\t"]) for _ in range(rng.randint(0, 6)))
    out.append(lead + body + " " * rng.randint(0, 2))
open(dst, "w", encoding="utf-8").write("\n".join(out))
