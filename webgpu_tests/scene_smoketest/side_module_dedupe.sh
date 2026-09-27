#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# side_module_dedupe.sh — shrink the scene-exports CI artifact
#
# Exporting 19 scenes with the dlink web template writes a ~52 MB
# index.side.wasm into every export directory, and all 19 are byte-identical
# because they come from the same template. Uploading them as-is makes the
# scene-exports artifact roughly 1.8 GB raw / 450-500 MB compressed per run.
#
#   pack <exports-dir>     Verify every index.side.wasm is identical, keep one
#                          copy in <exports-dir>/_side_module/ along with a
#                          manifest of which directories had one, and delete the
#                          rest. Run before upload-artifact.
#   unpack <exports-dir>   Copy the kept side module back into each directory
#                          named by the manifest, then remove _side_module/.
#                          Run after download-artifact.
#
# Both are no-ops when there is no side module to handle, so the same workflow
# works if CI ever exports with the non-dlink template instead.
#
# The directory is named _side_module rather than .side_module on purpose:
# actions/upload-artifact omits hidden files unless include-hidden-files is set,
# which would silently drop it and leave every scene without its side module.
#
# Exit codes: 0 = done (including nothing to do), 1 = refused or inconsistent.
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SIDE_NAME="index.side.wasm"
STORE_DIR="_side_module"
MANIFEST="side_wasm_dirs.txt"

die() {
	# ::error:: makes it a GitHub Actions annotation; harmless locally.
	echo "::error::side_module_dedupe: $*" >&2
	exit 1
}

usage() {
	echo "usage: $0 {pack|unpack} <exports-dir>" >&2
	exit 1
}

[[ $# -eq 2 ]] || usage
MODE="$1"
EXPORTS_DIR="$2"
[[ -d "$EXPORTS_DIR" ]] || die "no such directory: $EXPORTS_DIR"

cd "$EXPORTS_DIR"

case "$MODE" in
pack)
	shopt -s nullglob
	side_files=(*/"$SIDE_NAME")
	if [[ ${#side_files[@]} -eq 0 ]]; then
		echo "side_module_dedupe: no $SIDE_NAME found — nothing to dedupe (non-dlink export?)."
		exit 0
	fi

	# The whole scheme rests on them being identical. Check rather than trust:
	# fanning one copy back out over differing modules would hand some scenes an
	# engine that does not match their main module, which is the kind of failure
	# that looks like a driver bug.
	distinct=$(sha256sum "${side_files[@]}" | awk '{ print $1 }' | sort -u | wc -l)
	if [[ "$distinct" -ne 1 ]]; then
		die "$SIDE_NAME differs across exports ($distinct distinct checksums); refusing to dedupe."
	fi

	mkdir -p "$STORE_DIR"
	cp "${side_files[0]}" "$STORE_DIR/$SIDE_NAME"
	# Record which directories had one, so unpack restores exactly those rather
	# than guessing that every directory needs it.
	: >"$STORE_DIR/$MANIFEST"
	for f in "${side_files[@]}"; do
		echo "${f%/$SIDE_NAME}" >>"$STORE_DIR/$MANIFEST"
	done
	rm -f "${side_files[@]}"

	kept=$(du -m "$STORE_DIR/$SIDE_NAME" | cut -f1)
	echo "side_module_dedupe: ${#side_files[@]} copies of $SIDE_NAME -> 1 (${kept} MB kept)."
	;;

unpack)
	if [[ ! -f "$STORE_DIR/$SIDE_NAME" ]]; then
		echo "side_module_dedupe: no $STORE_DIR/$SIDE_NAME — nothing to restore."
		exit 0
	fi
	[[ -f "$STORE_DIR/$MANIFEST" ]] || die "$STORE_DIR/$SIDE_NAME present but $MANIFEST is missing."

	restored=0
	while IFS= read -r dir; do
		[[ -n "$dir" ]] || continue
		[[ -d "$dir" ]] || die "manifest names '$dir', which is not in the artifact."
		cp "$STORE_DIR/$SIDE_NAME" "$dir/$SIDE_NAME"
		restored=$((restored + 1))
	done <"$STORE_DIR/$MANIFEST"

	[[ "$restored" -gt 0 ]] || die "manifest was empty, so no side module was restored."
	rm -rf "$STORE_DIR"
	echo "side_module_dedupe: restored $SIDE_NAME into $restored export(s)."
	;;

*)
	usage
	;;
esac
