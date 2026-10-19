#!/usr/bin/env bash
# Runs the whole congestion matrix and tabulates the verdicts.
#
#   ./sweep.sh            # every fixture, native and web, plus the controls
#   ./sweep.sh loss_modes # one fixture only
#
# Each row is one (fixture, which side is the browser, link) triple. The link
# per fixture is chosen so the run finishes in a sensible time while still
# applying real pressure -- send_ceiling has to drain ~16 MiB, so it gets a
# faster link than backpressure, which only has to build a queue.
#
# Takes ~15 minutes for the full matrix. Needs an export for the web rows
# (./export.sh) and the webrtc-native addon for the native ones.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ONLY="${1:-}"
RESULTS=()

# fixture | peers | rate | delay | loss | label
MATRIX=(
	"backpressure|native|2mbit|25ms|0%|native, queue builds"
	"backpressure|web_a|2mbit|25ms|0%|web offerer"
	"backpressure|unshaped|-|-|-|CONTROL: no shaping"
	"send_ceiling|native|8mbit|10ms|0%|native, probe to 24 MB"
	"send_ceiling|web_a|8mbit|10ms|0%|web offerer, probe to 24 MB"
	"loss_modes|native|4mbit|25ms|5%|native, 5% loss"
	"loss_modes|web_a|4mbit|25ms|5%|web offerer, 5% loss"
	"loss_modes|web_a_clean|4mbit|25ms|0%|CONTROL: web, no loss"
	"mp_modes|native|4mbit|25ms|5%|native, shipped default"
	"mp_modes|web_a|4mbit|25ms|5%|web, shipped default"
	"recv_growth|web_b|8mbit|10ms|0%|web RECEIVER holds 25s"
	"recv_growth|native|8mbit|10ms|0%|native receiver holds 25s"
)

run_one() {
	local fixture="$1" peers="$2" rate="$3" delay="$4" loss="$5" label="$6"
	local env_args=("RTC_TEST=$fixture")
	case "$peers" in
		web_a|web_a_clean) env_args+=("RTC_A=web") ;;
		web_b) env_args+=("RTC_B=web") ;;
	esac

	printf "  %-14s %-22s " "$fixture" "$label"
	local log
	log="$(mktemp)"
	if [[ "$peers" == "unshaped" ]]; then
		(cd "$HERE" && env "${env_args[@]}" node run_congestion.mjs) > "$log" 2>&1
	else
		(cd "$HERE" && ./shape.sh --rate "$rate" --delay "$delay" --loss "$loss" \
			-- env "${env_args[@]}" node run_congestion.mjs) > "$log" 2>&1
	fi

	local verdict
	verdict="$(grep -oE 'verdict=[A-Z_]+' "$log" | tail -1 | cut -d= -f2)"
	[[ -z "$verdict" ]] && verdict="NO_VERDICT"
	printf "%s\n" "$verdict"

	# The numbers worth seeing next to each verdict, per fixture.
	case "$fixture" in
		backpressure)
			grep -oE 'peak_buffered=[0-9]+|goodput_bps=[0-9]+' "$log" | tr '\n' ' ' | sed 's/^/        /' ;;
		send_ceiling)
			grep -oE 'ceiling_at_buffered=[0-9]+|no ceiling up to [0-9]+ bytes|graceful=[a-z]+|recovered_after_drain=[a-z]+' "$log" \
				| tr '\n' ' ' | sed 's/^/        /' ;;
		loss_modes|mp_modes)
			grep -E '^RTC\|   ' "$log" | awk '{printf "%s=%s ", $2, $4}' | sed 's/^/        /' ;;
		recv_growth)
			grep -oE 'peer peak_held=[0-9]+ packets = [0-9]+ bytes|input buffer grew to [0-9]+ bytes' "$log" \
				| tr '\n' ';' | sed 's/^/        /' ;;
	esac
	echo
	RESULTS+=("$fixture/$peers: $verdict")
	rm -f "$log"
}

echo "WebRTC congestion sweep"
echo "  shaping: unprivileged netns + netem (shape.sh), nothing outside the test is touched"
echo ""
for row in "${MATRIX[@]}"; do
	IFS='|' read -r fixture peers rate delay loss label <<< "$row"
	if [[ -n "$ONLY" && "$fixture" != "$ONLY" ]]; then
		continue
	fi
	run_one "$fixture" "$peers" "$rate" "$delay" "$loss" "$label"
done

echo ""
echo "summary:"
for r in "${RESULTS[@]}"; do
	echo "  $r"
done
echo ""
echo "Expected, as of 2026-10-13 (webgpu_notes/TASKS.md §4.20):"
cat <<'EOF'
  backpressure native/web  TRACKED              get_buffered_amount tracks a real backlog
  backpressure unshaped    NO_PRESSURE          the control: no shaping, nothing to measure
  send_ceiling native      NO_CEILING_UP_TO_PROBE
  send_ceiling web         CEILING_GRACEFUL     F9: Chrome caps at ~16 MiB, recovers
  loss_modes   native/web  MODES_DIFFER         F8's fix demonstrably does something
  loss_modes   web clean   MODES_INDISTINGUISHABLE  the control: no loss, no divergence
  mp_modes     native/web  DEFAULT_SANE         F10 fixed: the shipped default now agrees
  recv_growth  native/web  GREW_INTACT          F3's growth is lossless and shrinks back
EOF
