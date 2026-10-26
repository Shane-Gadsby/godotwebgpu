#!/usr/bin/env bash
# lease.sh take|drop|show [name] [owner] [hours]: leases on shared resources (webgpu_notes/work/README.md).
# take: succeeds if the lease is free, expired or already the owner's; exit 1 otherwise.
set -euo pipefail
dir="$HOME/.cache/godotwebgpu-leases"
mkdir -p "$dir"
cmd="${1:-show}" name="${2:-}" owner="${3:-}" hours="${4:-2}"
now=$(date +%s)
case "$cmd" in
	take)
		[[ -n "$name" && -n "$owner" ]] || { echo "usage: lease.sh take <name> <owner> [hours]"; exit 2; }
		exec 9>"$dir/$name.lock"
		flock 9
		if [[ -f "$dir/$name" ]]; then
			read -r who until <"$dir/$name"
			if [[ "$who" != "$owner" && "$until" -gt "$now" ]]; then
				echo "held by $who until $(date -d @"$until" +%H:%M)"
				exit 1
			fi
		fi
		echo "$owner $((now + hours * 3600))" >"$dir/$name"
		echo "taken: $name by $owner for ${hours}h"
		;;
	drop)
		[[ -n "$name" && -n "$owner" ]] || { echo "usage: lease.sh drop <name> <owner>"; exit 2; }
		exec 9>"$dir/$name.lock"
		flock 9
		if [[ -f "$dir/$name" ]]; then
			read -r who _ <"$dir/$name"
			if [[ "$who" != "$owner" ]]; then
				echo "NOT dropped: $name is held by $who, not by '$owner'" >&2
				exit 1
			fi
			rm -f "$dir/$name"
		fi
		echo "dropped: $name"
		;;
	show)
		for f in "$dir"/*; do
			[[ -f "$f" && "$f" != *.lock ]] || continue
			read -r who until <"$f"
			state=$([[ "$until" -gt "$now" ]] && echo "until $(date -d @"$until" +%H:%M)" || echo expired)
			echo "$(basename "$f"): $who ($state)"
		done
		;;
	*)
		echo "usage: lease.sh take|drop|show [name] [owner] [hours]"
		exit 2
		;;
esac
