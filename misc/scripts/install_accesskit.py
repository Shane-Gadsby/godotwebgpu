#!/usr/bin/env python3

if __name__ != "__main__":
    raise SystemExit(f'Utility script "{__file__}" should not be used as a module!')

import os
import shutil
import sys
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../"))


# Base Godot dependencies path
# If cross-compiling (no LOCALAPPDATA), we install in `bin`
deps_folder = os.getenv("LOCALAPPDATA")
if deps_folder and not os.getenv("MSYSTEM"):
    deps_folder = os.path.join(deps_folder, "Godot", "build_deps")
else:
    deps_folder = os.path.join("bin", "build_deps")

# AccessKit
ac_version = "0.23.1"

# Create dependencies folder
if not os.path.exists(deps_folder):
    os.makedirs(deps_folder)

ac_filename = "accesskit-c-" + ac_version + ".zip"
ac_archive = os.path.join(deps_folder, "accesskit.zip")
ac_folder = os.path.join(deps_folder, "accesskit")
ac_version_file = os.path.join(ac_folder, "version")

# The version file is written last, so it only exists once an install completed. Skip the download when it names
# the version pinned here; `--force` reinstalls anyway.
if "--force" not in sys.argv[1:] and os.path.isfile(ac_version_file):
    with open(ac_version_file) as f:
        installed_version = f.read().strip()
    if installed_version == ac_version:
        print(
            f"AccessKit {ac_version} is already installed in {ac_folder}, skipping download (--force to reinstall).\n"
        )
        sys.exit(0)
    print(f"AccessKit {installed_version} is installed, updating to {ac_version} ...")

if os.path.isfile(ac_archive):
    os.remove(ac_archive)

print(f"Downloading AccessKit {ac_filename} ...")
urllib.request.urlretrieve(
    f"https://github.com/godotengine/godot-accesskit-c-static/releases/download/{ac_version}/{ac_filename}",
    ac_archive,
)
if os.path.exists(ac_folder):
    print(f"Removing existing local AccessKit installation in {ac_folder} ...")
    shutil.rmtree(ac_folder)
print(f"Extracting AccessKit {ac_filename} to {ac_folder} ...")
shutil.unpack_archive(ac_archive, deps_folder)
os.remove(ac_archive)
os.rename(os.path.join(deps_folder, "accesskit-c-" + ac_version), ac_folder)

with open(ac_version_file, "w") as f:
    f.write(ac_version)

print("AccessKit installed successfully.\n")
