#!/usr/bin/env python3
"""Builds the single export-templates .tpz for a draft release.

Used by .github/workflows/runner.yml's `draft-release` job. Takes the
directory `actions/download-artifact` produced (one subdirectory per CI
build-matrix artifact, containing whatever raw files that job uploaded) and
renames every recognized template file into exactly what Godot's Export
Template Manager expects, following the same convention this repo's own
build.sh/build-linux.sh already use in their package_tpz() functions (a zip
containing one "templates/" directory with version.txt alongside every
template file, flattened -- see editor/export/export_template_manager.cpp's
_tpz_file_selected()). The result can be dropped straight into
~/.local/share/godot/export_templates/<version>/ or imported via the editor.

Usage:
    package_release_tpz.py <artifacts_dir> <version> <output_tpz_path>

macOS, Android, and iOS templates are now genuinely release-ready (fixed
alongside this script -- see macos_builds.yml, android_builds.yml, and
ios_builds.yml respectively for the actual bundling/multi-ABI/packaging
changes): macOS runs SCons's real `generate_bundle=yes` step, Android builds
both ABIs x both configs before running gradle once, and iOS assembles the
real Xcode-project-based template. None of these could be verified on real
hardware from this environment (no macOS/Xcode/Android SDK available here),
so treat the first real CI run of each as the actual verification.

Debug templates: every platform now builds and includes a debug template
alongside release (previously only web's WebGPU variant did, and only
release existed at all for several platforms) -- see each *_builds.yml's
added "debug"-suffixed matrix entries/steps.
"""

import glob
import os
import shutil
import sys


def find_one(artifacts_dir, artifact_name, pattern):
    matches = sorted(glob.glob(os.path.join(artifacts_dir, artifact_name, pattern)))
    return matches[0] if matches else None


def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <artifacts_dir> <version> <output_tpz_path>", file=sys.stderr)
        sys.exit(1)

    artifacts_dir, version, output_tpz = sys.argv[1], sys.argv[2], os.path.abspath(sys.argv[3])

    work_dir = os.path.join(os.path.dirname(output_tpz), ".tpz_staging")
    staging = os.path.join(work_dir, "templates")
    if os.path.isdir(work_dir):
        shutil.rmtree(work_dir)
    os.makedirs(staging)

    included = []
    skipped = []

    def take(artifact_name, pattern, dest_name):
        src = find_one(artifacts_dir, artifact_name, pattern)
        if src:
            shutil.copy2(src, os.path.join(staging, dest_name))
            included.append(f"{artifact_name}/{os.path.basename(src)} -> {dest_name}")
        else:
            skipped.append(f"{artifact_name} (pattern {pattern!r}) -- not found, skipping")

    # --- Linux (EditorExportPlatformLinuxBSD::get_template_file_name) ---
    take("linux-template", "godot.linuxbsd.template_release.x86_64", "linux_release.x86_64")
    take("linux-template-debug", "godot.linuxbsd.template_debug.x86_64", "linux_debug.x86_64")

    # --- Windows (EditorExportPlatformWindows::get_template_file_name) ---
    take("windows-template", "godot.windows.template_release.x86_64.exe", "windows_release_x86_64.exe")
    take("windows-template", "godot.windows.template_release.x86_64.console.exe", "windows_release_x86_64_console.exe")
    take("windows-template-debug", "godot.windows.template_debug.x86_64.exe", "windows_debug_x86_64.exe")
    take("windows-template-debug", "godot.windows.template_debug.x86_64.console.exe", "windows_debug_x86_64_console.exe")

    # --- macOS (get_platform_name() + ".zip") ---
    # macos_builds.yml now runs SCons's own generate_bundle step, which
    # already produces a real .app-bundle zip containing both debug+release
    # universal binaries, under exactly this name (godot_macos*.zip) -- just
    # copy it straight through, no renaming logic needed.
    take("macos-template", "godot_macos*.zip", "macos.zip")

    # --- Web / WebGPU (EditorExportPlatformWeb::_get_template_name) ---
    take("web-template", "godot.web.template_release.wasm64*.zip", "web_release.zip")
    take("web-template-debug", "godot.web.template_debug.wasm64*.zip", "web_debug.zip")
    take("web-nothreads-template", "godot.web.template_release.wasm32*.zip", "web_nothreads_release.zip")
    take("web-nothreads-template-debug", "godot.web.template_debug.wasm32*.zip", "web_nothreads_debug.zip")
    take("web-webgpu-template", "godot.web.template_release.wasm32*.zip", "web_dlink_nothreads_release.zip")
    take("web-webgpu-template-debug", "godot.web.template_debug.wasm32*.zip", "web_dlink_nothreads_debug.zip")

    # --- Android ---
    # android_builds.yml now builds both arm32 and arm64, for both
    # template_debug and template_release, in one job before running gradle
    # once, so these are genuinely multi-ABI, genuinely release-optimized
    # APKs (see that workflow's "Compilation (templates, ...)" step comment).
    take("android-template", "android_debug.apk", "android_debug.apk")
    take("android-template", "android_release.apk", "android_release.apk")
    take("android-template", "android_source.zip", "android_source.zip")

    # --- iOS (get_platform_name() + ".zip", via EditorExportPlatformAppleEmbedded) ---
    # ios_builds.yml now assembles the real template shape Godot expects (the
    # Xcode project skeleton plus built .xcframework libraries) via SCons's
    # generate_bundle_apple_embedded(), which names the output godot_ios*.zip.
    take("ios-template", "godot_ios*.zip", "ios.zip")

    if not included:
        print("ERROR: no recognized template files found in any downloaded artifact.", file=sys.stderr)
        sys.exit(1)

    with open(os.path.join(staging, "version.txt"), "w") as f:
        f.write(version + "\n")

    print("Included in .tpz:")
    for line in included:
        print(f"  {line}")
    if skipped:
        print("Not found this run (skipped):")
        for line in skipped:
            print(f"  {line}")

    if output_tpz.endswith(".tpz"):
        base = output_tpz[: -len(".tpz")]
    else:
        base = output_tpz
    zip_path = shutil.make_archive(base, "zip", root_dir=work_dir, base_dir="templates")
    os.replace(zip_path, output_tpz)
    shutil.rmtree(work_dir)
    print(f"Wrote {output_tpz}")


if __name__ == "__main__":
    main()
