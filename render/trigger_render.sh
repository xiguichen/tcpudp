#!/usr/bin/env bash
#
# Mac-side entry point for the Render tunnel.
#
#   ./render/trigger_render.sh && ./run_github.sh
#
# What it does, in order:
#   1. syncs the local checkout to the push branch
#   2. wakes the Render instance by polling /healthz until it reports a hostname
#   3. reconciles the local tunnel-info file against the live hostname
#   4. pins the hostname to a fast Cloudflare edge IP
#   5. starts the tunnel in the background (listening on 0.0.0.0, so other
#      machines on the LAN can proxy through it) and prints the proxy URLs
#   6. starts a keepalive that keeps hitting /healthz so Render cannot fall
#      asleep mid-browse and take the tunnel's hostname down with it
#
# Why step 3 exists: Render's free tier sleeps after ~15 minutes idle and wipes
# its filesystem, so the tunnel gets a brand-new hostname every time. The
# hostname committed to git is therefore usually stale, and the only authority
# on the current one is /healthz. The live hostname always wins.
#
# Options:
#   --health-url URL   override RENDER_HEALTH_URL
#   --timeout SECONDS  how long to wait for a hostname (default 300)
#   --no-wait          single poll instead of waiting
#
# Env knobs (all optional): RENDER_HEALTH_URL, GITHUB_PUSH_BRANCH,
# TCPUDP_INFO_DIR, TCPUDP_RUN_DIR, TCPUDP_PROXY_PORT, TCPUDP_LAN_IP,
# POLL_INTERVAL, RECONCILE_TIMEOUT, HEALTH_TIMEOUT, HOSTS_FILE, CF_EDGE_IPS,
# PING_COUNT, START_TIMEOUT, KEEPALIVE_INTERVAL.

set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
REPO_ROOT=$(cd "$HERE/.." && pwd -P)

# Must agree with `name:` in render.yaml. Pinned by a test, because a mismatch
# here is a silent 404 rather than an error: Render answers an unknown service
# name with 404 and "x-render-routing: no-server", which reads as a dead
# service rather than a typo.
HEALTH_URL=${RENDER_HEALTH_URL:-https://tcpudp.onrender.com/healthz}
BRANCH=${GITHUB_PUSH_BRANCH:-run}
INFO_DIR=${TCPUDP_INFO_DIR:-github_run}
RUN_DIR=${TCPUDP_RUN_DIR:-$HOME/.tcpudp}
SERVER_PORT=${TCPUDP_PROXY_PORT:-8080}
TIMEOUT=300
NO_WAIT=0
POLL_INTERVAL=${POLL_INTERVAL:-5}
RECONCILE_TIMEOUT=${RECONCILE_TIMEOUT:-120}
HEALTH_TIMEOUT=${HEALTH_TIMEOUT:-10}
START_TIMEOUT=${START_TIMEOUT:-20}
KEEPALIVE_INTERVAL=${KEEPALIVE_INTERVAL:-180}

log()  { printf '%s\n' "$*"; }
warn() { printf 'WARNING: %s\n' "$*" >&2; }
die()  { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --health-url) HEALTH_URL=${2:-}; shift 2 ;;
    --health-url=*) HEALTH_URL=${1#*=}; shift ;;
    --timeout) TIMEOUT=${2:-}; shift 2 ;;
    --timeout=*) TIMEOUT=${1#*=}; shift ;;
    --no-wait) NO_WAIT=1; shift ;;
    -h | --help)
      sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *) die "unknown option: $1" ;;
  esac
done

cd "$REPO_ROOT" || die "cannot enter $REPO_ROOT"

# --------------------------------------------------------------------------
# health_hostname URL
#
# Print the hostname /healthz reports, or nothing at all. Never fails and never
# writes to stderr: Render answers a cold start with an HTML "spinning up"
# page, and before the tunnel is up with a 200 carrying hostname:null. Both are
# normal "not yet" answers, not errors, so the caller just keeps polling.
# --------------------------------------------------------------------------
health_hostname() {
  local body
  body=$(curl -fsS --max-time "$HEALTH_TIMEOUT" "$1" 2>/dev/null) || return 0
  [ -n "$body" ] || return 0
  printf '%s' "$body" | python3 -c '
import json, sys
try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(0)
if isinstance(data, dict):
    host = data.get("hostname")
    if isinstance(host, str) and host:
        print(host)
' 2>/dev/null || true
  return 0
}

# --------------------------------------------------------------------------
# health_published URL
#
# Print "true" or "false" as /healthz reports the published flag, or nothing at
# all when the service does not report one. Never fails and never writes to
# stderr, for the same reasons as health_hostname.
#
# This is the difference between "the push has not landed yet" and "no push is
# coming". Without it a service with GITHUB_PAT unset is polled for the full
# reconcile timeout on every run, always ending in the same fallback.
# --------------------------------------------------------------------------
health_published() {
  local body
  body=$(curl -fsS --max-time "$HEALTH_TIMEOUT" "$1" 2>/dev/null) || return 0
  [ -n "$body" ] || return 0
  printf '%s' "$body" | python3 -c '
import json, sys
try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(0)
if isinstance(data, dict) and isinstance(data.get("published"), bool):
    print("true" if data["published"] else "false")
' 2>/dev/null || true
  return 0
}

# --------------------------------------------------------------------------
# branch_has_hostname HOST
#
# True when the push branch already carries this hostname in the tunnel-info
# file, i.e. the supervisor's git write-back beat us to it.
# --------------------------------------------------------------------------
branch_has_hostname() {
  local content
  content=$(git show "origin/$BRANCH:$INFO_DIR/cloudflare.sh" 2>/dev/null) || return 1
  case "$content" in
    *"--hostname $1"*) return 0 ;;
  esac
  return 1
}

# --------------------------------------------------------------------------
# write_local_tunnel_info HOST
#
# Write the tunnel-info file in the exact one-line command format that
# run_github.sh sources. It must never be reduced to a bare hostname: that file
# is executed, not parsed, so a bare name would fail with "command not found".
# --------------------------------------------------------------------------
write_local_tunnel_info() {
  local host=$1 file="$INFO_DIR/cloudflare.sh"
  mkdir -p "$INFO_DIR" || return 1
  printf 'cloudflared access tcp --url tcp://0.0.0.0:%s --hostname %s\n' \
    "$SERVER_PORT" "$host" >"$file" || return 1
  printf 'cloudflared access tcp --url tcp://0.0.0.0:%s --hostname %s\n' \
    "$SERVER_PORT" "$host" >"$INFO_DIR/cloudflare.bat" || return 1
  return 0
}

# --------------------------------------------------------------------------
# lan_ip
#
# The address other machines on the LAN should use to reach this Mac's tunnel -
# the proxy server field on their side. Override with TCPUDP_LAN_IP. Falls back
# to 127.0.0.1 when no address can be found, which simply means "this machine
# only".
# --------------------------------------------------------------------------
lan_ip() {
  local ifc ip
  [ -n "${TCPUDP_LAN_IP:-}" ] && { printf '%s\n' "$TCPUDP_LAN_IP"; return 0; }
  ifc=$(route -n get default 2>/dev/null | awk '/interface:/{print $2}')
  [ -n "$ifc" ] && ip=$(ipconfig getifaddr "$ifc" 2>/dev/null)
  printf '%s\n' "${ip:-127.0.0.1}"
}

# --------------------------------------------------------------------------
# start_tunnel HOST
#
# Run `cloudflared access tcp` in the background, listening on 0.0.0.0 so any
# machine on the LAN can proxy through it (their proxy server is this Mac's
# LAN IP; see lan_ip).
#
# Always restarts: the previous run's daemon is killed via the pidfile, and any
# stray tunnel matching our port is pkilled, so a fresh listener can bind. The
# listener is then probed for up to START_TIMEOUT half-seconds; a failure to
# come up is not fatal - cloudflared may still be registering with the edge -
# but it is reported together with the log path.
# --------------------------------------------------------------------------
start_tunnel() {
  local host=$1 pidfile="$RUN_DIR/cloudflared.pid" logfile="$RUN_DIR/cloudflared.log"
  local prev pid killed=0 i

  mkdir -p "$RUN_DIR" || return 1

  if [ -f "$pidfile" ]; then
    prev=$(cat "$pidfile" 2>/dev/null || true)
    rm -f "$pidfile"
    if [ -n "$prev" ]; then
      if kill "$prev" 2>/dev/null; then
        killed=1
        log "  stopped previous tunnel pid $prev"
      fi
    fi
  fi
  if pkill -f "cloudflared.*access tcp --url tcp://.*:$SERVER_PORT" 2>/dev/null; then
    killed=1
  fi
  if [ "$killed" = 1 ]; then
    # Let the old socket drain, so the bind below cannot race EADDRINUSE.
    sleep 1
  fi

  # --no-autoupdate must precede the subcommand: `cloudflared access tcp` on
  # current releases rejects flags after the subcommand name.
  nohup cloudflared --no-autoupdate access tcp --url "tcp://0.0.0.0:$SERVER_PORT" \
    --hostname "$host" \
    >"$logfile" 2>&1 &
  pid=$!
  printf '%s\n' "$pid" >"$pidfile"
  log "  started pid $pid: cloudflared --no-autoupdate access tcp --url tcp://0.0.0.0:$SERVER_PORT --hostname $host"
  log "  log: $logfile"

  i=0
  while [ "$i" -lt "$START_TIMEOUT" ]; do
    if nc -z -G 1 127.0.0.1 "$SERVER_PORT" 2>/dev/null; then
      log "  listening on 0.0.0.0:$SERVER_PORT"
      return 0
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      warn "tunnel process exited early; see $logfile"
      return 1
    fi
    sleep 0.5
    i=$((i + 1))
  done
  warn "tunnel has not opened port $SERVER_PORT after ${START_TIMEOUT}x0.5s; see $logfile"
  return 1
}

# --------------------------------------------------------------------------
# start_keepalive TUNNEL_PID
#
# Render's free tier sleeps after ~15 minutes with no traffic, and a wake wipes
# the filesystem and mints a new hostname - killing whatever tunnel pointed at
# the old one. While the tunnel is up this spawns a detached loop that keeps
# hitting /healthz so the instance cannot fall asleep mid-browse.
#
# The loop watches the tunnel pid and stops when the tunnel dies, so it never
# keeps warming a dead hostname. KEEPALIVE_INTERVAL=0 disables it entirely.
# Replaced on every run like the tunnel, via its own pidfile.
# --------------------------------------------------------------------------
start_keepalive() {
  local tpid=$1 pidfile="$RUN_DIR/keepalive.pid" logfile="$RUN_DIR/keepalive.log"
  local prev
  mkdir -p "$RUN_DIR" || return 1
  if [ -f "$pidfile" ]; then
    prev=$(cat "$pidfile" 2>/dev/null || true)
    rm -f "$pidfile"
    [ -n "$prev" ] && kill "$prev" 2>/dev/null || true
  fi
  if [ "${KEEPALIVE_INTERVAL:-180}" = 0 ]; then
    log "  keepalive disabled (KEEPALIVE_INTERVAL=0)"
    return 0
  fi
  (
    while kill -0 "$tpid" 2>/dev/null; do
      curl -fsS --max-time "$HEALTH_TIMEOUT" "$HEALTH_URL" >/dev/null 2>&1 || true
      sleep "$KEEPALIVE_INTERVAL"
    done
  ) >"$logfile" 2>&1 &
  printf '%s\n' "$!" >"$pidfile"
  log "  keepalive pid $! (hits $HEALTH_URL every ${KEEPALIVE_INTERVAL}s)"
}

# --------------------------------------------------------------------------

log "=== Render tunnel ==="
log "Repo:   $REPO_ROOT"
log "Health: $HEALTH_URL"

# 1. sync the checkout. A failure here is not fatal: the reconcile step below
#    works from the live hostname and can repair the file locally.
current_branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null)
if [ "$current_branch" != "$BRANCH" ]; then
  log "Switching to $BRANCH"
  git checkout "$BRANCH" >/dev/null 2>&1 ||
    warn "could not switch to $BRANCH; continuing on $current_branch"
fi
git pull --quiet origin "$BRANCH" >/dev/null 2>&1 ||
  warn "could not pull $BRANCH; continuing with the local checkout"

# 2. wake the instance and wait for a hostname.
log ""
log "Waiting for a tunnel hostname (up to ${TIMEOUT}s)..."
host=''
if [ "$NO_WAIT" -eq 1 ]; then
  host=$(health_hostname "$HEALTH_URL")
  [ -n "$host" ] || die "no hostname available and --no-wait was given"
else
  deadline=$((SECONDS + TIMEOUT))
  while [ "$SECONDS" -lt "$deadline" ]; do
    host=$(health_hostname "$HEALTH_URL")
    if [ -n "$host" ]; then break; fi
    printf '.'
    sleep "$POLL_INTERVAL"
  done
  printf '\n'
  if [ -z "$host" ]; then
    die "timed out after ${TIMEOUT}s waiting for a tunnel hostname from $HEALTH_URL"
  fi
fi
log "Tunnel hostname: $host"

# 3. reconcile the local tunnel-info file against the live hostname.
log ""
if branch_has_hostname "$host"; then
  log "The $BRANCH branch already carries this hostname."
  git checkout "$BRANCH" -- "$INFO_DIR/cloudflare.sh" >/dev/null 2>&1 || true
else
  # Only wait when a publish could still land. The service reports published=false
  # when its own git write-back did not happen, and that is the normal state for
  # a deployment without GITHUB_PAT - polling for a push that is never coming
  # just costs the full timeout. An absent field (a service predating it) keeps
  # the old behaviour, since a push may be in flight.
  case "$(health_published "$HEALTH_URL")" in
    false)
      log "$BRANCH is stale and the service reports no publish, so there is"
      log "nothing to wait for. Taking the live hostname."
      ;;
    *)
      log "Branch does not have it yet; waiting up to ${RECONCILE_TIMEOUT}s for a publish..."
      rdeadline=$((SECONDS + RECONCILE_TIMEOUT))
      while [ "$SECONDS" -lt "$rdeadline" ]; do
        git fetch --quiet origin "$BRANCH" >/dev/null 2>&1 || true
        if branch_has_hostname "$host"; then break; fi
        sleep "$POLL_INTERVAL"
      done
      ;;
  esac
  if branch_has_hostname "$host"; then
    log "The supervisor published it; using the committed file."
    git checkout "$BRANCH" -- "$INFO_DIR/cloudflare.sh" >/dev/null 2>&1 || true
  else
    warn "$BRANCH is stale: it does not carry $host (this is expected when"
    warn "GITHUB_PAT is not set on the Render service). Writing the live"
    warn "hostname into $INFO_DIR/cloudflare.sh locally so the client works."
    write_local_tunnel_info "$host" ||
      die "could not write $INFO_DIR/cloudflare.sh"
  fi
fi

# 4. pin DNS to a fast edge IP.
# shellcheck disable=SC1091
if [ -f "$HERE/net.sh" ]; then
  . "$HERE/net.sh"
  log ""
  log "=== Pinning DNS for lower latency ==="
  if pin_result=$(pin_tunnel_hostname "$host"); then
    case "$pin_result" in
      already-pinned*) log "  /etc/hosts already correct" ;;
      pinned*) log "  Added $pin_result to ${HOSTS_FILE:-/etc/hosts}" ;;
      fallback*) warn "  No candidate edge IP answered; using $pin_result" ;;
      *) log "  $pin_result" ;;
    esac
  else
    # Not fatal - the tunnel works without it - but say so loudly, because the
    # consequence is exactly the slow CN edge IP this step exists to avoid.
    warn "DNS pinning FAILED. The client may be routed to a slow Cloudflare"
    warn "edge IP. Re-run with sudo, or add the line by hand:"
    warn "  echo '<edge-ip> $host' | sudo tee -a ${HOSTS_FILE:-/etc/hosts}"
  fi
else
  warn "net.sh not found next to this script; skipping DNS pinning"
fi

# 5. start the tunnel in the background, then tell the user where to point things.
log ""
log "=== Starting the tunnel ==="
start_tunnel "$host"

# 6. keep the instance awake: a free-tier sleep wipes the filesystem and mints
#    a new hostname, which silently kills the tunnel we just started.
tunnel_pid=$(cat "$RUN_DIR/cloudflared.pid" 2>/dev/null || true)
if [ -n "$tunnel_pid" ]; then
  log ""
  log "=== Keeping the instance awake ==="
  start_keepalive "$tunnel_pid"
fi

log ""
log "Proxy ready - point a browser or app at either address:"
log "  this Mac:       http://127.0.0.1:${SERVER_PORT}"
log "  other machines: http://$(lan_ip):${SERVER_PORT}"
log "  check:          curl -4 -x http://127.0.0.1:${SERVER_PORT} https://ifconfig.me"
