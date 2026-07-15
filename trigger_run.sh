#!/usr/bin/env bash
#
# Trigger a GitHub Actions run by pushing a trigger file to the 'run' branch,
# then poll via git fetch until the Action completes and commits cloudflare info.
#
# The GitHub Action (run.yml) triggers on any push to 'run' that modifies
# trigger/*.  When it finishes, it commits an "Auto-update" with the cloudflare
# tunnel scripts.
#
# This script uses ONLY git — no API access needed (works through firewalls
# and restrictive networks where git operations succeed but GitHub API calls
# might be blocked).
#
# Usage:
#   ./trigger_run.sh [--timeout SECONDS] [--no-wait]
#
# After completion:
#   ./run_github.sh    # start the client in GitHub-run mode
#

set -euo pipefail

TIMEOUT=${TIMEOUT:-300}
NO_WAIT=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --no-wait) NO_WAIT=true; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ---- Ensure we are on the run branch ----
CURRENT_BRANCH=$(git branch --show-current)
if [ "$CURRENT_BRANCH" != "run" ]; then
    echo "=== Switching to run branch ==="
    git checkout run
fi

# Pull to avoid push conflicts with any previous Auto-update commits
echo "=== Pulling latest run branch ==="
git pull origin run

# Record the commit that WE are about to push (the trigger commit).
# After the Action runs, it will push an "Auto-update" commit on top of this.
TRIGGER_COMMIT_FILE="/tmp/tcpudp_trigger_commit.txt"
PUSH_COMMIT=$(git rev-parse HEAD)

# ---- Create and push the trigger ----
TIMESTAMP="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
TRIGGER_FILE="trigger/$(date +%s)"

mkdir -p trigger
echo "trigger $TIMESTAMP" > "$TRIGGER_FILE"

echo "=== Triggering GitHub run via git push ==="
git add "$TRIGGER_FILE"
git commit -m "trigger: $TIMESTAMP"
git push origin run

# Record the commit we just pushed (this is what the Auto-update builds on)
PUSH_COMMIT=$(git rev-parse HEAD)
echo "$PUSH_COMMIT" > "$TRIGGER_COMMIT_FILE"
echo "  Trigger commit: ${PUSH_COMMIT:0:7}"
echo "  Timestamp:      $TIMESTAMP"

if $NO_WAIT; then
    echo ""
    echo "Run triggered. To fetch results later, run:"
    echo "  git pull origin run"
    exit 0
fi

# ---- Poll via git fetch until the Auto-update commit appears ----
echo ""
echo "=== Waiting for GitHub Action to complete (timeout: ${TIMEOUT}s) ==="
echo -n "  polling"

START_TIME=$(date +%s)
POLL_INTERVAL=10

while true; do
    ELAPSED=$(($(date +%s) - START_TIME))
    if [ "$ELAPSED" -gt "$TIMEOUT" ]; then
        echo ""
        echo "ERROR: Timed out after ${TIMEOUT}s."
        echo "The workflow may still be running. Check:"
        echo "  https://github.com/xiguichen/tcpudp/actions"
        echo ""
        echo "To fetch results when it finishes:"
        echo "  git pull origin run"
        exit 1
    fi

    git fetch origin run 2>/dev/null
    REMOTE_HEAD=$(git rev-parse origin/run)
    REMOTE_MSG=$(git log --format=%s -1 origin/run)

    # The Action is done when origin/run has moved past our trigger commit
    # AND the top commit is an "Auto-update" from the Action.
    if [ "$REMOTE_HEAD" != "$PUSH_COMMIT" ] && echo "$REMOTE_MSG" | grep -q "Auto-update"; then
        echo ""
        echo "  GitHub Action completed!"
        break
    fi

    echo -n "."
    sleep "$POLL_INTERVAL"
done

# ---- Pull the results ----
echo ""
echo "=== Fetching cloudflare tunnel info ==="
git pull origin run

# ---- Display results ----
if [ -f github_run/cloudflare.sh ]; then
    echo ""
    echo "=== Cloudflare tunnel ready ==="
    echo ""
    HOSTNAME=$(grep -o 'https://[^ ]*trycloudflare\.com' github_run/cloudflare.sh 2>/dev/null || echo "unknown")
    echo "  Hostname: $HOSTNAME"
    echo "  Command:  $(cat github_run/cloudflare.sh)"
    echo ""
    if [ -f github_run/run_info.json ]; then
        echo "  Run info: $(cat github_run/run_info.json)"
    fi
    echo ""
    echo "Next step:  ./run_github.sh"
else
    echo "ERROR: github_run/cloudflare.sh not found."
    echo "The run may still be in progress or may have failed."
    echo "Check: https://github.com/xiguichen/tcpudp/actions"
    exit 1
fi
