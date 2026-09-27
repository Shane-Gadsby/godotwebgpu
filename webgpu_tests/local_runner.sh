#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# local_runner.sh — run this repo's GitHub Actions on local hardware, safely
#
# The workflow picks its runner with `runs-on: ${{ vars.CI_RUNNER || 'ubuntu-24.04' }}`,
# so setting the repository variable CI_RUNNER moves CI onto a self-hosted runner
# and unsetting it moves CI back to GitHub's.
#
# The trap is the whole point. GitHub does NOT fall back to hosted runners when a
# self-hosted one is offline: it resolves runs-on to a label and waits for a runner
# carrying it, and a job queued longer than ~24h fails. So a variable left set
# while this machine is off does not mean "CI ran in the cloud", it means "CI hung
# until tomorrow". This script ties the variable to the runner process's lifetime:
# set on start, unset on exit, however the exit happens (Ctrl-C, kill, crash,
# logout).
#
#   ./webgpu_tests/local_runner.sh start    # set CI_RUNNER, run the runner, unset on exit
#   ./webgpu_tests/local_runner.sh status   # where CI would run right now, and why
#   ./webgpu_tests/local_runner.sh stop     # unset CI_RUNNER (repair after a hard kill)
#
# Environment:
#   RUNNER_DIR      Runner install dir            (default: ~/actions-runner)
#   RUNNER_LABEL    Label given at config.sh time (default: godotwebgpu-local)
#   CI_RUNNER_VAR   Variable name to toggle       (default: CI_RUNNER)
#   GH_REPO         owner/repo                    (default: from origin remote)
#
# Requires: gh, authenticated, with admin on the repo. Registering the runner
# itself is a separate, one-time `./config.sh` — this only flips the switch.
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

RUNNER_DIR="${RUNNER_DIR:-$HOME/actions-runner}"
RUNNER_LABEL="${RUNNER_LABEL:-godotwebgpu-local}"
CI_RUNNER_VAR="${CI_RUNNER_VAR:-CI_RUNNER}"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[0;33m'; BOLD='\033[1m'; NC='\033[0m'

die() { printf "${RED}error:${NC} %s\n" "$*" >&2; exit 1; }
info() { printf "${BOLD}%s${NC}\n" "$*"; }

command -v gh >/dev/null 2>&1 || die "gh not found — install the GitHub CLI."
gh auth status >/dev/null 2>&1 || die "gh is not authenticated — run: gh auth login"

# Repo: explicit override, else the origin remote, so this works from a clone with
# a different name without editing anything.
if [[ -n "${GH_REPO:-}" ]]; then
	REPO="$GH_REPO"
else
	origin="$(git -C "$(dirname "$0")/.." remote get-url origin 2>/dev/null || true)"
	[[ -n "$origin" ]] || die "no origin remote and GH_REPO is unset."
	REPO="$(printf '%s' "$origin" | sed -E 's#^git@github\.com:##; s#^https://github\.com/##; s#\.git$##')"
fi

var_get() {
	# Prints the value, or nothing when unset. `gh variable list` is used rather
	# than a get subcommand because older gh builds do not have one.
	gh variable list --repo "$REPO" --json name,value \
		--jq ".[] | select(.name == \"$CI_RUNNER_VAR\") | .value" 2>/dev/null || true
}

var_set() { gh variable set "$CI_RUNNER_VAR" --body "$RUNNER_LABEL" --repo "$REPO" >/dev/null; }
var_unset() { gh variable delete "$CI_RUNNER_VAR" --repo "$REPO" >/dev/null 2>&1 || true; }

runner_state() {
	# Empty when the API is not readable (listing runners needs repo admin) or when
	# no runner carries the label.
	gh api "repos/$REPO/actions/runners" \
		--jq ".runners[] | select([.labels[].name] | index(\"$RUNNER_LABEL\")) | .status" 2>/dev/null || true
}

cmd_status() {
	local current state
	current="$(var_get)"
	state="$(runner_state)"

	info "Repository:     $REPO"
	info "Runner label:   $RUNNER_LABEL"
	printf "${BOLD}Registered:${NC}     %s\n" "${state:-not registered (or runner list not readable)}"

	if [[ -z "$current" ]]; then
		printf "${BOLD}%s:${NC}      unset\n" "$CI_RUNNER_VAR"
		printf "${GREEN}→ CI runs on GitHub's runners.${NC}\n"
	else
		printf "${BOLD}%s:${NC}      %s\n" "$CI_RUNNER_VAR" "$current"
		if [[ "$state" == "online" ]]; then
			printf "${GREEN}→ CI runs here.${NC}\n"
		else
			printf "${RED}→ CI is pointed at '%s', which is not online.${NC}\n" "$current"
			printf "${RED}  Jobs will queue (~24h) instead of falling back. Run: %s stop${NC}\n" "$0"
		fi
	fi
}

cmd_stop() {
	if [[ -z "$(var_get)" ]]; then
		info "$CI_RUNNER_VAR is already unset — CI is on GitHub's runners."
		return 0
	fi
	var_unset
	[[ -z "$(var_get)" ]] || die "failed to unset $CI_RUNNER_VAR — check it in the repo settings."
	printf "${GREEN}%s unset — CI is back on GitHub's runners.${NC}\n" "$CI_RUNNER_VAR"
}

cmd_start() {
	[[ -d "$RUNNER_DIR" ]] || die "no runner at $RUNNER_DIR — see webgpu_notes/HANDOFF.md for setup."
	[[ -x "$RUNNER_DIR/run.sh" ]] || die "$RUNNER_DIR/run.sh not found or not executable."
	# .runner is written by config.sh; without it run.sh exits immediately and we
	# would have set the variable for a runner that never starts.
	[[ -f "$RUNNER_DIR/.runner" ]] || die "$RUNNER_DIR is not configured — run ./config.sh there first."

	# Unset on the way out, whatever the reason. Registered before the variable is
	# set so a failure in between cannot leave it behind.
	cleanup() {
		local rc=$?
		printf "\n${YELLOW}Stopping — restoring CI to GitHub's runners...${NC}\n"
		var_unset
		if [[ -n "$(var_get)" ]]; then
			printf "${RED}WARNING: could not unset %s. Pushes will queue until you do.${NC}\n" "$CI_RUNNER_VAR"
			printf "${RED}         Run: %s stop${NC}\n" "$0"
		else
			printf "${GREEN}%s unset — CI is back on GitHub's runners.${NC}\n" "$CI_RUNNER_VAR"
		fi
		exit $rc
	}
	trap cleanup EXIT INT TERM HUP

	info "Pointing $REPO CI at '$RUNNER_LABEL'..."
	var_set
	[[ "$(var_get)" == "$RUNNER_LABEL" ]] || die "failed to set $CI_RUNNER_VAR."
	printf "${GREEN}%s=%s${NC}\n" "$CI_RUNNER_VAR" "$RUNNER_LABEL"
	printf "${YELLOW}Leave this running. Ctrl-C returns CI to GitHub's runners.${NC}\n\n"

	# Not exec: that would replace this shell and the trap would never fire.
	"$RUNNER_DIR/run.sh"
}

case "${1:-}" in
start) cmd_start ;;
stop) cmd_stop ;;
status) cmd_status ;;
*)
	echo "usage: $0 {start|stop|status}" >&2
	exit 1
	;;
esac
