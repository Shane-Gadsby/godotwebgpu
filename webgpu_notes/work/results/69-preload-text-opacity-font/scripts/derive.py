#!/usr/bin/env python3
"""derive.py <src_export> <dst_export> <key>: copies an export and removes GODOT_CONFIG[key] (or startButton[key])."""

import json
import shutil
import sys
from pathlib import Path

src, dst, key = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
shutil.rmtree(dst, ignore_errors=True)
shutil.copytree(src, dst)
html = (dst / "index.html").read_text()
head = "const GODOT_CONFIG = "
i = html.index(head) + len(head)
j = html.index(";\n", i)
config = json.loads(html[i:j])
if key in config:
    del config[key]
else:
    del config["startButton"][key]
new = json.dumps(config, separators=(",", ":")).replace("<", "\\u003c")
(dst / "index.html").write_text(html[:i] + new + html[j:])
print(f"{dst.name}: {key} removed")
