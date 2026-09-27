#!/usr/bin/env bash
#
# Run the whole render/ test suite.
#
#   ./render/run_tests.sh
#
# No network, no sudo, no docker. Every git interaction goes to a throwaway
# local repo, /healthz is a local python server on 127.0.0.1, ping is stubbed,
# and /etc/hosts writes are redirected to a sandbox file.

set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
failed=0

run() { # label cmd...
  local label=$1
  shift
  printf '\n=== %s ===\n' "$label"
  if "$@"; then
    printf 'PASS: %s\n' "$label"
  else
    printf 'FAIL: %s\n' "$label"
    failed=$((failed + 1))
  fi
}

# A subshell rather than `env -C`, which macOS's env(1) does not support.
run keepalive        bash -c 'cd "$1" && python3 -m unittest test_keepalive' _ "$HERE"
run supervisor       bash "$HERE/test_scripts.sh"
run trigger-and-net  bash "$HERE/test_trigger_render.sh"

printf '\n'
if [ "$failed" -gt 0 ]; then
  printf '%s of 3 suites FAILED\n' "$failed"
  exit 1
fi
printf 'all 3 suites passed\n'
exit 0
