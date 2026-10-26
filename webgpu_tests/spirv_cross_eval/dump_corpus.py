"""Dump every engine shader variant the precompiler builds, as SPIR-V and as bin/wgsl_convert_cli's WGSL.

Runs drivers/webgpu/wgsl_precompile.py exactly as a web build does, with its batch converter wrapped so each
module's SPIR-V lands in <out>/raw/<key>.spv and the converter's output in <out>/wgsl/<key>.wgsl
The generated header goes
to <out>/gen.h, never into the tree. Not SCons: safe to run while a build is going, but it uses every core.

Usage: python3 dump_corpus.py <repo_root> <out_dir> [glslangValidator]
"""

import os
import re
import shutil
import sys

repo_root, out_dir = sys.argv[1], sys.argv[2]
glslang = sys.argv[3] if len(sys.argv) > 3 else "glslangValidator"
sys.path.insert(0, os.path.join(repo_root, "drivers", "webgpu"))

import wgsl_precompile  # noqa: E402

for sub in ("raw", "wgsl"):
    os.makedirs(os.path.join(out_dir, sub), exist_ok=True)

_convert = wgsl_precompile.convert_spirv_batch


def _safe(key):
    return re.sub(r"[^A-Za-z0-9_.-]", "_", key)


def _dumping_convert(spv_files, cli_path):
    for key, path in spv_files:
        shutil.copy(path, os.path.join(out_dir, "raw", _safe(key) + ".spv"))
    results = _convert(spv_files, cli_path)
    for key, wgsl in results.items():
        if isinstance(wgsl, str):
            with open(os.path.join(out_dir, "wgsl", _safe(key) + ".wgsl"), "w") as f:
                f.write(wgsl)
    return results


wgsl_precompile.convert_spirv_batch = _dumping_convert
wgsl_precompile.precompile_wgsl(repo_root, os.path.join(out_dir, "gen.h"), glslang)
