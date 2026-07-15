#!/usr/bin/env bash
#
# Start the tcpudp client in vpn2 mode (direct connection).
#
# Usage:
#   ./run_vpn2.sh
#
# Connects directly to the production vpn2 server without cloudflared.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RUN_DIR="$SCRIPT_DIR/run"
CONFIG_FILE="$RUN_DIR/config.json"

if [ ! -f "$RUN_DIR/udp_client" ]; then
    echo "ERROR: udp_client not found in $RUN_DIR"
    echo "Run 'python3 run.py' first to build/copy the client."
    exit 1
fi

# ---- Write vpn2 config ----
mkdir -p "$RUN_DIR"
cat > "$CONFIG_FILE" << 'EOF'
{
  "localHostUdpPort": 5002,
  "peerTcpPort": 7001,
  "peerAddress": "52.148.65.23",
  "clientId": 4
}
EOF
echo "Config set for vpn2 mode (52.148.65.23:7001)"

echo "=== Starting udp_client (vpn2 mode) ==="
echo "  (Press Ctrl+C to stop)"
cd "$RUN_DIR"
./udp_client
