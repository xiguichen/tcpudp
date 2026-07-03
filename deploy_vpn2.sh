#!/usr/bin/env bash
#
# Deploy a test instance of the tcpudp server on vpn2.
#
# This script is designed to NOT conflict with the production server instance
# that runs on the default port (7001). It uses dedicated test ports:
#   TCP server:  17001
#   UDP echo:    17002
#
# Usage:
#   ./deploy_vpn2.sh [--release nightly|stable] [--tcp-port 17001] [--echo-port 17002]
#
# Prerequisites on vpn2:
#   - python3 (for the UDP echo server)
#   - curl or wget (to download the release)
#   - tar
#

set -euo pipefail

RELEASE="${RELEASE:-nightly}"
REPO="xiguichen/tcpudp"
TCP_PORT="${TCP_PORT:-17001}"
ECHO_PORT="${ECHO_PORT:-17002}"
ARTIFACT_NAME="tcpudp-ubuntu-latest"
INSTALL_DIR="$HOME/tcpudp-test"

# ---- Parse CLI overrides ----
while [[ $# -gt 0 ]]; do
    case "$1" in
        --release) RELEASE="$2"; shift 2 ;;
        --tcp-port) TCP_PORT="$2"; shift 2 ;;
        --echo-port) ECHO_PORT="$2"; shift 2 ;;
        --install-dir) INSTALL_DIR="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

echo "=== tcpudp test server deployment ==="
echo "  Release:     $RELEASE"
echo "  TCP port:    $TCP_PORT"
echo "  Echo port:   $ECHO_PORT"
echo "  Install dir: $INSTALL_DIR"

# ---- Stop any existing test server instance ----
echo ""
echo "--- Stopping existing test server ---"
pkill -f "server.*--port=$TCP_PORT" 2>/dev/null || echo "  No existing server process found"
pkill -f "integration_test.*echo" 2>/dev/null || echo "  No existing echo server found"
# Give processes a moment to exit
sleep 1

# ---- Download and extract release ----
echo ""
echo "--- Downloading release $RELEASE ---"
mkdir -p "$INSTALL_DIR"
cd "$INSTALL_DIR"

DOWNLOAD_URL="https://github.com/$REPO/releases/download/$RELEASE/$ARTIFACT_NAME.tar.gz"
echo "  URL: $DOWNLOAD_URL"

if command -v curl &>/dev/null; then
    curl -fsSL "$DOWNLOAD_URL" -o "$ARTIFACT_NAME.tar.gz"
elif command -v wget &>/dev/null; then
    wget -q "$DOWNLOAD_URL" -O "$ARTIFACT_NAME.tar.gz"
else
    echo "ERROR: Neither curl nor wget found. Install one of them."
    exit 1
fi

echo "  Extracting..."
tar xzf "$ARTIFACT_NAME.tar.gz"
chmod +x "$ARTIFACT_NAME/server"

echo "  Release downloaded and extracted."

# ---- Start UDP echo server ----
echo ""
echo "--- Starting UDP echo server on port $ECHO_PORT ---"
# Kill any lingering echo server on this port
fuser -k "${ECHO_PORT}/udp" 2>/dev/null || true
sleep 0.5

python3 -c "
import socket, sys
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(('0.0.0.0', $ECHO_PORT))
print(f'Echo server listening on UDP :$ECHO_PORT')
while True:
    data, addr = sock.recvfrom(65536)
    sock.sendto(data, addr)
" &

ECHO_PID=$!
echo "  Echo server PID: $ECHO_PID"

# ---- Start tcpudp server ----
echo ""
echo "--- Starting tcpudp test server on TCP port $TCP_PORT ---"
nohup "$ARTIFACT_NAME/server" \
    --port="$TCP_PORT" \
    --udp-target-port="$ECHO_PORT" \
    --log-level=INFO \
    > "$INSTALL_DIR/server.log" 2>&1 &

SERVER_PID=$!
echo "  Server PID: $SERVER_PID"

# Wait a moment and check it's running
sleep 2
if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo ""
    echo "=== Deployment complete ==="
    echo "  TCP server: 0.0.0.0:$TCP_PORT (PID $SERVER_PID)"
    echo "  UDP echo:   0.0.0.0:$ECHO_PORT (PID $ECHO_PID)"
    echo "  Server log: $INSTALL_DIR/server.log"
    echo ""
    echo "To run the performance test from a client:"
    echo "  python3 test/integration_test.py --mode remote \\"
    echo "      --server-addr <vpn2-wireguard-ip> \\"
    echo "      --server-port $TCP_PORT \\"
    echo "      --echo-port $ECHO_PORT \\"
    echo "      --client-port 17003"
    echo ""
    echo "To stop:"
    echo "  kill $SERVER_PID $ECHO_PID"
else
    echo "ERROR: Server failed to start. Check $INSTALL_DIR/server.log"
    kill "$ECHO_PID" 2>/dev/null || true
    exit 1
fi
