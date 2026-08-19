#!/usr/bin/env bash
#
# Set up the tcpudp tunnel path through the dedicated vpn2 server.
#
# Everything needed so the client can connect through a Cloudflare Quick
# Tunnel hosted on vpn2:
#   1. Via SSH, restart the tcpudp server and the cloudflared Quick Tunnel
#      on vpn2, then extract the fresh trycloudflare.com hostname.
#   2. Fetch the tunnel info back to github_run/ over SCP.
#   3. Pin the tunnel hostname to a low-RTT Cloudflare edge IP in /etc/hosts.
#   4. Start the client-side `cloudflared access` tunnel in the background.
#   5. Write the client config.
#   6. Print the command for the user to run the client.
#
# Usage:
#   ./trigger_vpn2.sh [--host HOST] [--server PATH] [--release TAG]
#
# Options (defaults in parentheses):
#   --host HOST     SSH host/alias for vpn2        (vpn2)
#   --server PATH   tcpudp server binary on vpn2   (~/tcpudp/server)
#   --release TAG   GitHub release tag to download (v1.1.16)
#   --help          Show this message
#
# Prerequisites:
#   - cloudflared installed on the Mac (brew install cloudflared)
#   - SSH key auth to vpn2 works non-interactively (ssh vpn2)
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

HOST="vpn2"
SERVER="~/tcpudp/server"
RELEASE="v1.1.16"
RUNDIR="~/tcpudp"
LOCAL_RUN_DIR="github_run"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --host) HOST="$2"; shift 2 ;;
        --server) SERVER="$2"; shift 2 ;;
        --release) RELEASE="$2"; shift 2 ;;
        --help) sed -n '2,32p' "$0"; exit 0 ;;
        *) echo "Unknown option: $1"; echo "Run '$0 --help' for usage."; exit 1 ;;
    esac
done

# ---- Prereqs ----
if ! command -v cloudflared &>/dev/null; then
    echo "ERROR: cloudflared not found locally. Install: brew install cloudflared"
    exit 1
fi

if ! ssh -o ConnectTimeout=5 -o BatchMode=yes "$HOST" true; then
    echo "ERROR: cannot SSH to $HOST. Check your SSH key/config."
    exit 1
fi

# ---- Step 1: restart server + tunnel on vpn2 ----
echo "=== Step 1: Restarting server + cloudflared tunnel on $HOST ==="
REMOTE_OUTPUT=$(SERVER="$SERVER" RELEASE="$RELEASE" RUNDIR="$RUNDIR" \
    ssh "$HOST" 'bash -s' <<'REMOTE_EOF'
set -euo pipefail

RUNDIR="${RUNDIR:=$HOME/tcpudp}"
SERVER="${SERVER:=$RUNDIR/server}"
SERVER="${SERVER/#\~/$HOME}"
RUNDIR="${RUNDIR/#\~/$HOME}"
RELEASE="${RELEASE:-v1.1.16}"

fail() {
    echo "ERROR: $1" >&2
    exit 1
}

command -v cloudflared >/dev/null || fail "cloudflared not found on $(hostname)"
mkdir -p "$RUNDIR"

if [ ! -x "$SERVER" ]; then
    echo "  Downloading release $RELEASE..." >&2
    cd "$RUNDIR"
    curl -fsSL "https://github.com/xiguichen/tcpudp/releases/download/$RELEASE/tcpudp-ubuntu-latest.tar.gz" -o tcpudp-ubuntu-latest.tar.gz
    tar xzf tcpudp-ubuntu-latest.tar.gz
    cp tcpudp-ubuntu-latest/server "$SERVER"
    chmod +x "$SERVER"
fi

echo "  Restarting tcpudp server..." >&2
pkill -f "^$SERVER" 2>/dev/null && sleep 1 || true
for i in $(seq 1 10); do
    if ! (ss -tln 2>/dev/null | grep -q ':7001 ' || netstat -tln 2>/dev/null | grep -q ':7001 '); then
        break
    fi
    sleep 1
done
nohup "$SERVER" --log-level=INFO > "$RUNDIR/server.log" 2>&1 &
echo "  server pid $!" >&2
for i in $(seq 1 10); do
    if ss -tln 2>/dev/null | grep -q ':7001 ' || netstat -tln 2>/dev/null | grep -q ':7001 '; then
        break
    fi
    sleep 1
done
if ! (ss -tln 2>/dev/null | grep -q ':7001 ' || netstat -tln 2>/dev/null | grep -q ':7001 '); then
    echo "ERROR: server not listening on 7001" >&2
    tail -20 "$RUNDIR/server.log" >&2 || true
    exit 1
fi
echo "  server listening on TCP 7001" >&2

echo "  Restarting cloudflared tunnel..." >&2
pkill -f '^cloudflared tunnel --url tcp://localhost:7001' 2>/dev/null && sleep 1 || true
: > "$RUNDIR/cloudflared.log"
nohup cloudflared tunnel --url tcp://localhost:7001 --logfile "$RUNDIR/cloudflared.log" </dev/null >/dev/null 2>&1 &
echo "  cloudflared pid $!" >&2

HOSTNAME=""
for i in $(seq 1 12); do
    if grep -q 'https://.*trycloudflare.com' "$RUNDIR/cloudflared.log" 2>/dev/null; then
        HOSTNAME=$(grep -o 'https://.*trycloudflare.com' "$RUNDIR/cloudflared.log" | head -1)
        break
    fi
    echo "  waiting for tunnel hostname (${i}/12)..." >&2
    sleep 5
done
[ -n "$HOSTNAME" ] || fail "no trycloudflare hostname after 60s"
HOSTNAME_BARE="${HOSTNAME#https://}"

echo -n "cloudflared access tcp --url tcp://localhost:7001 --hostname " > "$RUNDIR/cloudflare.sh"
echo "$HOSTNAME_BARE" >> "$RUNDIR/cloudflare.sh"
cp "$RUNDIR/cloudflare.sh" "$RUNDIR/cloudflare.bat"
chmod +x "$RUNDIR/cloudflare.sh"
echo "{\"hostname\":\"$HOSTNAME\",\"timestamp\":\"$(date -u +%Y-%m-%dT%H:%M:%SZ)\",\"port\":7001}" > "$RUNDIR/run_info.json"

# Machine-parseable output for the local side
echo "TUNNEL_HOSTNAME=$HOSTNAME_BARE"
echo "REMOTE_RUNDIR=$RUNDIR"
echo "REMOTE_SERVER=$SERVER"
REMOTE_EOF
)
echo "$REMOTE_OUTPUT"

TUNNEL_HOSTNAME=$(echo "$REMOTE_OUTPUT" | grep '^TUNNEL_HOSTNAME=' | cut -d= -f2-)
REMOTE_RUNDIR=$(echo "$REMOTE_OUTPUT" | grep '^REMOTE_RUNDIR=' | cut -d= -f2-)
REMOTE_SERVER=$(echo "$REMOTE_OUTPUT" | grep '^REMOTE_SERVER=' | cut -d= -f2-)
[ -n "$TUNNEL_HOSTNAME" ] || { echo "ERROR: no tunnel hostname returned from $HOST"; exit 1; }
[ -n "$REMOTE_RUNDIR" ] || { echo "ERROR: no remote rundir returned from $HOST"; exit 1; }

# ---- Step 2: fetch tunnel info ----
echo "=== Step 2: Fetching tunnel info ==="
mkdir -p "$LOCAL_RUN_DIR"
scp -q "$HOST:$REMOTE_RUNDIR/cloudflare.sh" "$HOST:$REMOTE_RUNDIR/cloudflare.bat" "$HOST:$REMOTE_RUNDIR/run_info.json" "$LOCAL_RUN_DIR/"
cp "$LOCAL_RUN_DIR/cloudflare.sh" cloudflare.sh
cp "$LOCAL_RUN_DIR/cloudflare.bat" cloudflare.bat
echo "  saved to $LOCAL_RUN_DIR/"

# ---- Step 3: pin DNS ----
HOSTNAME_BARE=$(grep -o '[^ ]*\.trycloudflare\.com' "$LOCAL_RUN_DIR/cloudflare.sh" | head -1)
echo "=== Step 3: Pinning DNS for $HOSTNAME_BARE ==="
sudo sed -i.bak "/ $HOSTNAME_BARE\$/d" /etc/hosts 2>/dev/null || true
if grep -q " $HOSTNAME_BARE\$" /etc/hosts 2>/dev/null; then
    echo "  /etc/hosts already has an entry for $HOSTNAME_BARE"
else
    CANDIDATE_IPS=("162.159.38.209" "104.17.213.97")
    PING_COUNT=5
    PING_TIMEOUT_SEC=2
    echo "  Probing ${#CANDIDATE_IPS[@]} candidate IPs..."
    tmpdir=$(mktemp -d)
    i=0
    for ip in "${CANDIDATE_IPS[@]}"; do
        (
            avg=$(ping -c "$PING_COUNT" -W $((PING_TIMEOUT_SEC * 1000)) "$ip" 2>/dev/null \
                | awk -F' = ' '/^round-trip/{split($2,a,"/"); print a[2]}')
            [ -n "$avg" ] && echo "$avg $ip" || echo "999999 $ip unreachable"
        ) > "$tmpdir/$i" &
        i=$((i + 1))
    done
    wait
    BEST_INFO=$(sort -n "$tmpdir"/* 2>/dev/null | head -1)
    BEST_IP=$(echo "$BEST_INFO" | awk '{print $2}')
    if echo "$BEST_INFO" | grep -q 'unreachable'; then
        BEST_IP="${CANDIDATE_IPS[0]}"
        echo "  Warning: all candidates unreachable, falling back to $BEST_IP"
    fi
    rm -rf "$tmpdir"
    echo "$BEST_IP $HOSTNAME_BARE" | sudo tee -a /etc/hosts > /dev/null
    echo "  Added $BEST_IP $HOSTNAME_BARE to /etc/hosts"
fi

# ---- Step 4: start client-side access tunnel ----
echo "=== Step 4: Starting client-side access tunnel ==="
pkill -f '^cloudflared access tcp' 2>/dev/null && sleep 1 || true
bash "$LOCAL_RUN_DIR/cloudflare.sh" > /tmp/tcpudp-access.log 2>&1 &
ACCESS_PID=$!
echo "  access tunnel pid $ACCESS_PID"
sleep 3
if ! kill -0 "$ACCESS_PID" 2>/dev/null; then
    echo "ERROR: cloudflared access failed to start"
    cat /tmp/tcpudp-access.log
    exit 1
fi

# ---- Step 5: write client config ----
echo "=== Step 5: Writing client config ==="
mkdir -p run
cat > run/config.json <<EOF
{
  "localHostUdpPort": 5003,
  "peerTcpPort": 7001,
  "peerAddress": "127.0.0.1",
  "clientId": 4
}
EOF
echo "  wrote run/config.json"

# ---- Step 6: handoff ----
echo ""
echo "=== Tunnel ready ==="
echo "  Hostname: $TUNNEL_HOSTNAME"
echo "  Server:   $HOST:$REMOTE_SERVER"
echo ""
echo "Run:  cd run && ./udp_client"
