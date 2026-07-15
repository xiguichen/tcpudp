#!/usr/bin/env bash
#
# Start the tcpudp client in GitHub-run mode.
#
# This saves your current run/config.json, switches to use the cloudflared
# tunnel (127.0.0.1:7001), starts cloudflared access, and runs the client.
# Your existing config is restored on exit so vpn2 mode is unharmed.
#
# Usage:
#   ./run_github.sh
#
# Prerequisites:
#   - ./trigger_run.sh has been run and completed successfully
#   - cloudflared is installed (brew install cloudflared / apt install cloudflared)
#   - udp_client is built: python3 run.py
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RUN_DIR="$SCRIPT_DIR/run"
CONFIG_FILE="$RUN_DIR/config.json"
CONFIG_BACKUP="$RUN_DIR/config.json.vpn2.bak"
CLOUDFLARE_SCRIPT="$SCRIPT_DIR/github_run/cloudflare.sh"

# ---- Validate prerequisites ----
if [ ! -f "$RUN_DIR/udp_client" ]; then
    echo "ERROR: udp_client not found in $RUN_DIR"
    echo "Run 'python3 run.py' first to build/copy the client."
    exit 1
fi

if ! command -v cloudflared &>/dev/null; then
    echo "ERROR: cloudflared not found in PATH."
    echo "Install: brew install cloudflared  (or apt install cloudflared)"
    exit 1
fi

if [ ! -f "$CLOUDFLARE_SCRIPT" ]; then
    echo "ERROR: $CLOUDFLARE_SCRIPT not found."
    echo "Run './trigger_run.sh' first to get the GitHub tunnel info."
    exit 1
fi

# ---- Pull latest tunnel info ----
echo "=== Pulling latest tunnel info ==="
git pull origin run

# ---- Save existing vpn2 config ----
if [ -f "$CONFIG_FILE" ]; then
    cp "$CONFIG_FILE" "$CONFIG_BACKUP"
    echo "Backed up existing config to $CONFIG_BACKUP"
fi

# ---- Write GitHub-run config ----
# Use port 5003 (different from vpn2's 5002) so they never conflict.
GITHUB_UDP_PORT=5003

# Kill any previous client still holding this port
if lsof -i :$GITHUB_UDP_PORT -t 2>/dev/null | xargs kill 2>/dev/null; then
    echo "Killed stale process on UDP port $GITHUB_UDP_PORT"
fi

mkdir -p "$RUN_DIR"
cat > "$CONFIG_FILE" << EOF
{
  "localHostUdpPort": $GITHUB_UDP_PORT,
  "peerTcpPort": 7001,
  "peerAddress": "127.0.0.1",
  "clientId": 4
}
EOF
echo "Config set for GitHub run mode (127.0.0.1:7001, UDP:$GITHUB_UDP_PORT)"

# ---- Cleanup handler ----
cleanup() {
    echo ""
    echo "=== Cleaning up ==="
    # Kill cloudflared access
    pkill -f "cloudflared access tcp" 2>/dev/null || true
    # Restore vpn2 config
    if [ -f "$CONFIG_BACKUP" ]; then
        mv "$CONFIG_BACKUP" "$CONFIG_FILE"
        echo "Restored vpn2 config."
    fi
}
trap cleanup EXIT

# ---- Start cloudflared access tunnel ----
echo ""
echo "=== Starting cloudflared access tunnel ==="
echo "  Command: $(cat "$CLOUDFLARE_SCRIPT")"
bash "$CLOUDFLARE_SCRIPT" &
CLOUDFLARED_PID=$!
echo "  Cloudflared PID: $CLOUDFLARED_PID"

# Give the tunnel time to establish
sleep 3
if ! kill -0 "$CLOUDFLARED_PID" 2>/dev/null; then
    echo "ERROR: cloudflared failed to start. Check the output above."
    exit 1
fi
echo "  Tunnel established."

# ---- Run the client ----
echo ""
echo "=== Starting udp_client ==="
echo "  (Press Ctrl+C to stop)"
cd "$RUN_DIR"
./udp_client
