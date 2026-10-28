#!/usr/bin/env python3
"""base_zip.py <new_template.zip> <old_shell.html> <out.zip>: the new template with its godot.html replaced."""

import sys
import zipfile

src, shell, out = sys.argv[1:4]
with zipfile.ZipFile(src) as zi, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zo:
    names = zi.namelist()
    assert "godot.html" in names, names
    for n in names:
        zo.writestr(n, open(shell, "rb").read() if n == "godot.html" else zi.read(n))
print(out)
