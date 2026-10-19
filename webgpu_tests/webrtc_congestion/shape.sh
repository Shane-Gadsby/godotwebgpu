#!/usr/bin/env bash
# Builds a shaped two-namespace link and runs a command inside it.
#
#   ./shape.sh [--rate 2mbit] [--delay 25ms] [--loss 0%] -- <command...>
#
# The command runs in the "A" namespace at 10.77.0.1. To run something in the
# "B" namespace at 10.77.0.2, prefix it with `ip netns exec rtcb`, which is on
# PATH inside. Both directions of the veth carry the same netem qdisc, so the
# shaped figures are per-direction and the RTT is 2x --delay.
#
# NO ROOT, AND NOTHING OUTSIDE THE TEST IS TOUCHED. `unshare -rnm` maps the
# invoking user to root inside a new user + network + mount namespace, which
# grants CAP_NET_ADMIN *there only*. Every interface, qdisc and address below
# exists inside that namespace and disappears when the command exits. This
# matters more than convenience: the alternative the plan originally suggested
# --
#     sudo tc qdisc add dev lo root netem rate 1mbit delay 50ms
# -- needs root and rate-limits ALL loopback traffic on the machine for as long
# as it is applied, so an interrupted run leaves the host shaped. This cannot.
#
# Requires unprivileged user namespaces (`/proc/sys/kernel/unprivileged_userns_clone`
# = 1, the default on Ubuntu/Zorin). Check with `./shape.sh --selftest`.
set -euo pipefail

RATE="${RATE:-2mbit}"
DELAY="${DELAY:-25ms}"
LOSS="${LOSS:-0%}"
SELFTEST=false

while [[ $# -gt 0 ]]; do
	case "$1" in
		--rate) RATE="$2"; shift 2 ;;
		--delay) DELAY="$2"; shift 2 ;;
		--loss) LOSS="$2"; shift 2 ;;
		--selftest) SELFTEST=true; shift ;;
		--) shift; break ;;
		*) echo "unknown option $1" >&2; exit 2 ;;
	esac
done

if [[ "$SELFTEST" == false && $# -eq 0 ]]; then
	echo "usage: $0 [--rate R] [--delay D] [--loss L] -- <command...>" >&2
	exit 2
fi

export RTC_RATE="$RATE" RTC_DELAY="$DELAY" RTC_LOSS="$LOSS" RTC_SELFTEST="$SELFTEST"
export RTC_A_IP=10.77.0.1 RTC_B_IP=10.77.0.2

# The inner script runs as root inside the new namespaces.
inner() {
	set -euo pipefail

	# `ip netns add` needs a writable /run/netns. We have our own mount
	# namespace, so mounting a tmpfs over it affects nothing outside.
	mkdir -p /run/netns
	mount -t tmpfs none /run/netns

	ip netns add rtcb
	ip link add veth-a type veth peer name veth-b
	ip link set veth-b netns rtcb

	ip addr add "$RTC_A_IP/24" dev veth-a
	ip link set veth-a up
	ip link set lo up

	ip netns exec rtcb ip addr add "$RTC_B_IP/24" dev veth-b
	ip netns exec rtcb ip link set veth-b up
	ip netns exec rtcb ip link set lo up

	# netem on both egress paths. `rate` is the shaped bandwidth, `delay` the
	# one-way latency, `loss` random drop -- which for a data channel is what
	# makes an unreliable transfer mode behave differently from a reliable one.
	local NETEM=(netem rate "$RTC_RATE" delay "$RTC_DELAY")
	if [[ "$RTC_LOSS" != "0%" && "$RTC_LOSS" != "0" ]]; then
		NETEM+=(loss "$RTC_LOSS")
	fi
	tc qdisc add dev veth-a root "${NETEM[@]}"
	ip netns exec rtcb tc qdisc add dev veth-b root "${NETEM[@]}"

	echo "[shape] link ready: rate=$RTC_RATE delay=$RTC_DELAY (RTT 2x) loss=$RTC_LOSS" >&2
	echo "[shape] A=$RTC_A_IP  B=$RTC_B_IP (reach B with: ip netns exec rtcb ...)" >&2

	if [[ "$RTC_SELFTEST" == true ]]; then
		# Proves the shaping is real before anything is concluded from a run.
		# "Did the shaping apply?" is the first question to ask of any
		# surprising result here, so make it answerable in one command.
		echo "[shape] --- RTT across the link ---" >&2
		ip netns exec rtcb ping -c 3 -q "$RTC_A_IP" 2>&1 | tail -2 >&2
		echo "[shape] --- throughput, 1 MB ---" >&2
		python3 -c "
import socket, time
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('$RTC_A_IP', 9098)); s.listen(1)
import sys
print('[shape] sink ready', file=sys.stderr)
c, _ = s.accept(); n = 0
t = time.time()
while True:
    b = c.recv(65536)
    if not b: break
    n += len(b)
d = time.time() - t
print(f'[shape] sank {n} bytes in {d:.2f}s = {n*8/d/1e6:.2f} Mbit/s', file=sys.stderr)
" &
		sleep 1
		ip netns exec rtcb python3 -c "
import socket
s = socket.create_connection(('$RTC_A_IP', 9098))
data = b'x' * 65536
for _ in range(16):
    s.sendall(data)
s.close()
"
		wait
		exit 0
	fi

	exec "$@"
}

export -f inner
unshare -rnm bash -c 'inner "$@"' bash "$@"
