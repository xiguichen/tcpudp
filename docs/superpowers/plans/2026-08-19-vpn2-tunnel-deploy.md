# vpn2 Cloudflare Tunnel Deployment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a single self-contained Mac-side script `trigger_vpn2.sh` that restarts the tcpudp server and a fresh Cloudflare Quick Tunnel on the dedicated vpn2 server over SSH, fetches the new tunnel hostname back over SCP, pins DNS on the Mac, starts the client-side `cloudflared access` tunnel, writes the client config, and prints the handoff command for the user to run the client.

**Architecture:** One bash script. It runs a small embedded deploy script on vpn2 via `ssh host 'bash -s'` (heredoc stdin), captures a machine-parseable status line (`TUNNEL_HOSTNAME=...`, `REMOTE_RUNDIR=...`), then does the local steps (SCP fetch, `/etc/hosts` pin, background `cloudflared access`, `run/config.json`, handoff).

**Tech Stack:** bash (macOS client, Ubuntu vpn2), SSH/SCP, cloudflared, curl, ping, /dev/tcp-free (uses ping + sort for DNS pinning, no external tools).

## Global Constraints

- macOS client script; vpn2 reachable via `ssh vpn2` (non-interactive key auth, `BatchMode`).
- Always restart fresh: every run kills any existing tcpudp server and `cloudflared tunnel --url tcp://localhost:7001` on vpn2 before starting new ones. Never reuse a running process.
- Poll cloudflared's log every 5 seconds for the `https://<host>.trycloudflare.com` hostname (overall timeout ~60s = 12 polls).
- Server binary path and release tag are parameters: `--server <path>` (default `~/tcpudp/server`), `--release <tag>` (default `v1.1.16`), `--host <ssh-host>` (default `vpn2`).
- No region logic — no `region.json` anywhere.
- Info files written by the deploy portion: `cloudflare.sh`, `cloudflare.bat`, `run_info.json` in `$RUNDIR` on vpn2.
- Local fetch target: `github_run/` plus root `cloudflare.sh` / `cloudflare.bat` (mirrors the GH-flow layout).
- Client config: `run/config.json` = `{"localHostUdpPort": 5003, "peerTcpPort": 7001, "peerAddress": "127.0.0.1", "clientId": 4}`.
- Handoff: print `Run:  cd run && ./udp_client` and exit 0.
- Process-kill patterns must be anchored (`^...`) so they never match the script's own shell.

---

### Task 1: Write the complete `trigger_vpn2.sh`

**Files:**
- Create: `trigger_vpn2.sh`

**Interfaces:**
- Consumes: none.
- Produces: executable `./trigger_vpn2.sh` with options `--host`/`--server`/`--release`/`--help`; on success writes `github_run/cloudflare.sh`, `github_run/cloudflare.bat`, `github_run/run_info.json`, root `cloudflare.sh`/`cloudflare.bat`, `run/config.json`, an `/etc/hosts` entry, and leaves a `cloudflared access tcp` background process running.

- [ ] **Step 1: Write `trigger_vpn2.sh`**

```bash
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
pkill -f 'cloudflared access tcp' 2>/dev/null && sleep 1 || true
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
```

- [ ] **Step 2: Make it executable and syntax-check**

Run:
```bash
chmod +x trigger_vpn2.sh
bash -n trigger_vpn2.sh
```
Expected: no output from `bash -n`.

- [ ] **Step 3: Verify arg parsing and help**

Run:
```bash
./trigger_vpn2.sh --help
./trigger_vpn2.sh --host badhost --server /x/y --release v9.9.9 | head -3
```
Expected: `--help` prints the usage comment block and exits 0. The second command fails early with "ERROR: cannot SSH to badhost" (or similar) before doing anything destructive.

- [ ] **Step 4: Commit**

```bash
git add trigger_vpn2.sh
git commit -m "feat: add trigger_vpn2.sh to deploy server+tunnel on vpn2"
```

---

### Task 2: Live end-to-end verification against vpn2

**Files:**
- Modify: `trigger_vpn2.sh` (only if a defect is found)

**Interfaces:**
- Consumes: `./trigger_vpn2.sh` from Task 1; live vpn2 host.
- Produces: verified working script + any small fixes committed.

- [ ] **Step 1: Run the script**

Run:
```bash
./trigger_vpn2.sh
```
Expected: Step 1 prints server download/restart + cloudflared restart messages and a `TUNNEL_HOSTNAME=<host>.trycloudflare.com`; Steps 2–5 complete; handoff prints `Run:  ./run/udp_client`; script exits 0.

- [ ] **Step 2: Verify remote processes — exactly one each**

Run:
```bash
ssh vpn2 'pgrep -af "/tcpudp/server"; echo ---; pgrep -af "^cloudflared tunnel --url tcp://localhost:7001"'
```
Expected: exactly one line matching the server path and exactly one line matching the cloudflared tunnel. Verify server is listening: `ssh vpn2 'ss -tln | grep ":7001 "'` shows a LISTEN line.

- [ ] **Step 3: Verify local artifacts**

Run:
```bash
ls -la github_run/
cat github_run/cloudflare.sh
grep trycloudflare /etc/hosts
pgrep -af 'cloudflared access tcp'
cat run/config.json
```
Expected: `cloudflare.sh`, `cloudflare.bat`, `run_info.json` exist; `cloudflare.sh` contains `cloudflared access tcp --url tcp://localhost:7001 --hostname <host>.trycloudflare.com`; `/etc/hosts` has a `<ip> <host>.trycloudflare.com` line; a `cloudflared access tcp` process is running; `run/config.json` matches the spec (UDP 5003, peer 127.0.0.1:7001, client 4).

- [ ] **Step 4: Verify tunnel reachability through the access tunnel**

Run:
```bash
nc -z -G 5 127.0.0.1 7001 && echo CONNECT_OK
```
Expected: `CONNECT_OK` (proves Mac → access tunnel → quick tunnel → vpn2 server on TCP 7001).

- [ ] **Step 5: Re-run for idempotency of process counts (fresh restart)**

Run:
```bash
./trigger_vpn2.sh
ssh vpn2 'pgrep -af "/tcpudp/server" | wc -l; pgrep -af "^cloudflared tunnel --url tcp://localhost:7001" | wc -l'
```
Expected: script succeeds again; remote process counts are still exactly `1` and `1` (old ones were killed and replaced, not duplicated).

- [ ] **Step 6: Commit any fixes**

Only if a defect was found and fixed:
```bash
git add trigger_vpn2.sh
git commit -m "fix: adjust trigger_vpn2.sh after live verification"
```

---

## Self-Review

- **Spec coverage:** Every spec item maps to the script: restart-fresh server (`pkill`+`nohup`) ✓; restart-fresh cloudflared ✓; 5s hostname polling (12×5s) ✓; parameters `--host`/`--server`/`--release` ✓; no region files ✓; info files on vpn2 `$RUNDIR` ✓; SCP fetch to `github_run/` + root copies ✓; DNS pin to lowest-RTT edge IP ✓; client access tunnel in background ✓; `run/config.json` exactly per spec ✓; handoff prints `Run:  ./run/udp_client` ✓; anchored kill patterns avoid self-match ✓.
- **Placeholder scan:** No TBD/TODO; all code and commands concrete.
- **Type consistency:** Remote vars (`RUNDIR`, `SERVER`, `RELEASE`) set in the heredoc and echoed as `REMOTE_*` lines, parsed by name locally — consistent across tasks; `TUNNEL_HOSTNAME`/`HOSTNAME_BARE` naming consistent.