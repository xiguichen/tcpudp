#!/usr/bin/env bash
#
# render_supervisor.sh - PID 1 inside the Render container.
#
# Owns three children - the release's `server` binary, keepalive.py on $PORT,
# and a cloudflared quick tunnel - writes $TCPUDP_STATE_DIR/tunnel.json for
# keepalive.py to serve, and publishes the discovered tunnel hostname to the
# push branch so the Mac side can hand off to the unmodified run_github.sh.
#
# Sourcing this file defines functions and nothing else; `main` only runs when
# the file is executed. Every global comes from resolve_config, which reads the
# environment with ${VAR:-default}, so no unset variable can trip `set -u` in a
# caller. Functions assume resolve_config has run at least once.
#
# Deliberately not `set -e`: publishing is best-effort and must never take the
# service down. Every failure is logged and degraded past.

# --------------------------------------------------------------------------
# logging
# --------------------------------------------------------------------------

utc_now() {
  date -u +%Y-%m-%dT%H:%M:%SZ
}

log() {
  printf '%s %s\n' "$(utc_now)" "$*"
}

die() {
  printf '%s ERROR: %s\n' "$(utc_now)" "$*" >&2
  exit 1
}

# truthy VALUE - 1 for the spellings a dashboard checkbox can produce, else 0.
truthy() {
  case "${1:-}" in
    1 | true | TRUE | True | yes | YES | Yes | on | ON) printf '1' ;;
    *) printf '0' ;;
  esac
}

# --------------------------------------------------------------------------
# configuration
# --------------------------------------------------------------------------

# resolve_config - set every global from the environment. The env names are the
# ones the Render dashboard sets, passed through verbatim.
resolve_config() {
  RELEASE="${TCPUDP_RELEASE:-v1.1.16}"
  SERVER_PORT="${TCPUDP_SERVER_PORT:-7001}"
  REPO_DIR="${TCPUDP_REPO_DIR:-/app/repo}"
  INFO_DIR="${TCPUDP_INFO_DIR:-github_run}"
  STATE_DIR="${TCPUDP_STATE_DIR:-/run/tcpudp}"
  SERVER_BIN="${TCPUDP_SERVER_BIN:-$REPO_DIR/server}"
  SUPERVISE_INTERVAL="${SUPERVISE_INTERVAL:-5}"
  TUNNEL_START_TIMEOUT="${TUNNEL_START_TIMEOUT:-60}"
  PAT="${GITHUB_PAT:-}"
  GITHUB_REPO="${GITHUB_REPO:-xiguichen/tcpudp}"
  PUSH_BRANCH="${GITHUB_PUSH_BRANCH:-run}"
  PUBLISH_ENABLED=$(truthy "${PUBLISH:-true}")
  PORT="${PORT:-10000}"
  KEEPALIVE_SCRIPT="${KEEPALIVE_SCRIPT:-/usr/local/bin/keepalive.py}"
  RENDER_INFO_URL="${RENDER_INFO_URL:-https://ipinfo.io/json}"

  # Derived paths.
  CLOUDFLARED_LOG="${CLOUDFLARED_LOG:-$STATE_DIR/cloudflared.log}"
  STATE_PIDFILE="${STATE_PIDFILE:-$STATE_DIR/supervisor.pid}"
  SERVER_PIDFILE="${SERVER_PIDFILE:-$STATE_DIR/server.pid}"
  KEEPALIVE_PIDFILE="${KEEPALIVE_PIDFILE:-$STATE_DIR/keepalive.pid}"
  TUNNEL_PIDFILE="${TUNNEL_PIDFILE:-$STATE_DIR/tunnel.pid}"

  # Existing commit-polling logic matches on "Auto-update".
  GIT_COMMIT_SUBJECT="Auto-update cloudflare tunnel info (render)"
}

state_file() {
  printf '%s\n' "$STATE_DIR/tunnel.json"
}

# json_escape TEXT - print TEXT as a JSON string literal, quotes included.
json_escape() {
  local text=${1-}
  text=${text//\\/\\\\}
  text=${text//\"/\\\"}
  text=${text//$'\t'/\\t}
  text=${text//$'\n'/\\n}
  text=${text//$'\r'/\\r}
  printf '"%s"' "$text"
}

# json_field JSON-TEXT KEY - one string field of a JSON object, "" on any
# problem. python3 does the parsing; jq is not assumed to exist.
json_field() {
  python3 -c '
import json, sys
try:
    data = json.loads(sys.argv[1])
except Exception:
    data = {}
value = data.get(sys.argv[2], "") if isinstance(data, dict) else ""
if not isinstance(value, str):
    value = str(value)
sys.stdout.write(value)
' "$1" "$2" 2>/dev/null
}

# --------------------------------------------------------------------------
# state file (Contract A)
# --------------------------------------------------------------------------

# write_state HOSTNAME PUBLISHED-0|1 - atomically replace the state file.
# An empty hostname is JSON null, not the string "null": that null is the
# signal the Mac side polls for while the tunnel is still coming up.
write_state() {
  local hostname=${1:-} published=${2:-0}
  local file host_json published_json
  file=$(state_file)
  mkdir -p "$STATE_DIR" || return 1
  if [ -n "$hostname" ]; then
    host_json=$(json_escape "$hostname")
  else
    host_json='null'
  fi
  if [ "$published" = 1 ]; then
    published_json='true'
  else
    published_json='false'
  fi
  printf '{"hostname":%s,"port":%s,"published":%s,"source":"render","updated":"%s"}\n' \
    "$host_json" "$SERVER_PORT" "$published_json" "$(utc_now)" >"$file.tmp" || return 1
  mv "$file.tmp" "$file" || return 1
  return 0
}

# read_state_hostname - the recorded hostname, or nothing when the file is
# missing, unreadable or holds anything but a string.
read_state_hostname() {
  local file
  file=$(state_file)
  [ -f "$file" ] || return 0
  python3 -c '
import json, sys
try:
    with open(sys.argv[1], "r", encoding="utf-8") as handle:
        data = json.load(handle)
except Exception:
    sys.exit(0)
if isinstance(data, dict) and isinstance(data.get("hostname"), str):
    sys.stdout.write(data["hostname"])
' "$file" 2>/dev/null
}

# --------------------------------------------------------------------------
# process helpers
# --------------------------------------------------------------------------

# is_alive PIDFILE - exit 0 when the first field of PIDFILE names a live pid.
is_alive() {
  local pidfile=${1:-} pid=''
  [ -n "$pidfile" ] || return 1
  [ -f "$pidfile" ] || return 1
  pid=$(head -n 1 "$pidfile" 2>/dev/null | tr -cd '0-9')
  [ -n "$pid" ] || return 1
  kill -0 "$pid" 2>/dev/null
}

# kill_pidfile PIDFILE LABEL - TERM the recorded pid, then KILL if it lingers.
kill_pidfile() {
  local pidfile=${1:-} label=${2:-child} pid='' attempt
  [ -f "$pidfile" ] || return 0
  pid=$(head -n 1 "$pidfile" 2>/dev/null | tr -cd '0-9')
  [ -n "$pid" ] || return 0
  if ! kill -0 "$pid" 2>/dev/null; then
    return 0
  fi
  log "stopping $label (pid $pid)"
  kill -TERM "$pid" 2>/dev/null || true
  for ((attempt = 0; attempt < 20; attempt++)); do
    kill -0 "$pid" 2>/dev/null || return 0
    sleep 0.25
  done
  log "$label (pid $pid) ignored SIGTERM; sending SIGKILL"
  kill -KILL "$pid" 2>/dev/null || true
  return 0
}

# port_listening - is anything accepting on 127.0.0.1:$SERVER_PORT?
port_listening() {
  if command -v ss >/dev/null 2>&1; then
    local listing=''
    listing=$(ss -ltn 2>/dev/null) || listing=''
    case "$listing" in
      *":$SERVER_PORT "* | *":$SERVER_PORT"$'\n'*) return 0 ;;
    esac
  fi
  (exec 3<>"/dev/tcp/127.0.0.1/$SERVER_PORT") 2>/dev/null && return 0
  return 1
}

# --------------------------------------------------------------------------
# repository
# --------------------------------------------------------------------------

remote_url() {
  printf '%s\n' "${GITHUB_REMOTE_URL:-https://github.com/$GITHUB_REPO.git}"
}

# push_url - the token URL. Only ever handed straight to `git push`: never
# stored in .git/config, never logged, never echoed.
push_url() {
  printf '%s\n' "${GITHUB_PUSH_URL:-https://x-access-token:$PAT@github.com/$GITHUB_REPO.git}"
}

# ensure_git_identity - a Render container has no global git identity, so
# without this every publish fails on a fresh container while passing on a
# developer machine. Configure it in the clone only.
ensure_git_identity() {
  git -C "$REPO_DIR" config user.name 'render-supervisor' 2>/dev/null || true
  git -C "$REPO_DIR" config user.email 'render-supervisor@users.noreply.github.com' 2>/dev/null || true
}

# ensure_repo - clone the push branch if the checkout is not there yet. A
# Render container starts with an empty filesystem, so this is the normal path.
ensure_repo() {
  if [ -d "$REPO_DIR/.git" ]; then
    ensure_git_identity
    return 0
  fi
  mkdir -p "$(dirname "$REPO_DIR")" || return 1
  log "cloning $(remote_url) branch $PUSH_BRANCH into $REPO_DIR"
  if ! git clone --branch "$PUSH_BRANCH" --depth 50 "$(remote_url)" "$REPO_DIR"; then
    log "ERROR: git clone of $(remote_url) failed"
    return 1
  fi
  ensure_git_identity
  log "cloned into $REPO_DIR"
  return 0
}

# fetch_server - download the release tarball and install the server binary.
fetch_server() {
  local url tarball extracted
  url="https://github.com/$GITHUB_REPO/releases/download/$RELEASE/tcpudp-ubuntu-latest.tar.gz"
  tarball="$REPO_DIR/tcpudp-ubuntu-latest.tar.gz"
  extracted="$REPO_DIR/tcpudp-ubuntu-latest"
  log "downloading $url"
  if ! curl -fsSL --retry 3 --retry-delay 2 -o "$tarball" "$url"; then
    log "ERROR: could not download $url"
    return 1
  fi
  if ! tar -xzf "$tarball" -C "$REPO_DIR"; then
    log "ERROR: could not unpack $tarball"
    return 1
  fi
  if [ ! -f "$extracted/server" ]; then
    log "ERROR: $extracted/server is not in the release tarball"
    return 1
  fi
  if ! mv "$extracted/server" "$SERVER_BIN"; then
    log "ERROR: could not install $SERVER_BIN"
    return 1
  fi
  if ! chmod +x "$SERVER_BIN"; then
    log "ERROR: could not make $SERVER_BIN executable"
    return 1
  fi
  rm -rf "$extracted" "$tarball"
  log "installed $SERVER_BIN from $RELEASE"
  return 0
}

# --------------------------------------------------------------------------
# children
# --------------------------------------------------------------------------

# start_server - launch the server binary and wait for it to listen.
start_server() {
  local pid attempt
  log "starting $SERVER_BIN"
  mkdir -p "$REPO_DIR" || return 1
  nohup "$SERVER_BIN" >>"$REPO_DIR/server.log" 2>&1 </dev/null &
  pid=$!
  printf '%s\n' "$pid" >"$SERVER_PIDFILE"
  for ((attempt = 1; attempt <= 10; attempt++)); do
    if port_listening; then
      log "server is accepting on 127.0.0.1:$SERVER_PORT (pid $pid)"
      return 0
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      log "ERROR: $SERVER_BIN exited immediately; see $REPO_DIR/server.log"
      return 1
    fi
    sleep 1
  done
  log "WARN: nothing is listening on 127.0.0.1:$SERVER_PORT after 10s; supervise_loop will retry"
  return 1
}

# start_keepalive - launch the health endpoint. keepalive.py reads $PORT,
# $TCPUDP_STATE_DIR and $KEEPALIVE_BIND from the environment it inherits.
start_keepalive() {
  local pid
  # KEEPALIVE_BIND is passed through when set: tests pin it to 127.0.0.1 so a
  # run does not trip macOS's "accept incoming network connections" prompt.
  # Production leaves it unset, so keepalive.py binds 0.0.0.0 and Render's
  # health check can reach it.
  if [ -n "${KEEPALIVE_BIND:-}" ]; then
    export KEEPALIVE_BIND
  fi
  log "starting keepalive.py from $KEEPALIVE_SCRIPT on port $PORT"
  nohup python3 "$KEEPALIVE_SCRIPT" >>"$STATE_DIR/keepalive.log" 2>&1 </dev/null &
  pid=$!
  printf '%s\n' "$pid" >"$KEEPALIVE_PIDFILE"
  log "keepalive.py started (pid $pid)"
  return 0
}

# tunnel_hostname_from_log - the first quick-tunnel URL in the log, bare.
tunnel_hostname_from_log() {
  [ -f "$CLOUDFLARED_LOG" ] || return 0
  local match=''
  match=$(grep -oE 'https://[a-z0-9-]+\.trycloudflare\.com' "$CLOUDFLARED_LOG" 2>/dev/null |
    head -n 1) || return 0
  match=${match#https://}
  [ -n "$match" ] || return 0
  printf '%s\n' "$match"
}

# start_tunnel - relaunch a quick tunnel and poll its log for the hostname.
# Echoes the bare hostname, or nothing. Progress logs go to stderr so this
# function's stdout carries only the hostname and `$(start_tunnel)` is safe.
start_tunnel() {
  local pid waited host
  log "starting a cloudflared quick tunnel to 127.0.0.1:$SERVER_PORT" >&2
  kill_pidfile "$TUNNEL_PIDFILE" cloudflared
  # Anchored, so this can never match the supervisor's own command line.
  pkill -f '^cloudflared tunnel --url tcp://127.0.0.1:' 2>/dev/null || true
  mkdir -p "$(dirname "$CLOUDFLARED_LOG")" 2>/dev/null || true
  : >"$CLOUDFLARED_LOG" 2>/dev/null || true
  nohup cloudflared tunnel --url "tcp://127.0.0.1:$SERVER_PORT" --no-autoupdate \
    --logfile "$CLOUDFLARED_LOG" >>"$CLOUDFLARED_LOG" 2>&1 </dev/null &
  pid=$!
  printf '%s\n' "$pid" >"$TUNNEL_PIDFILE"
  log "cloudflared started (pid $pid), waiting up to ${TUNNEL_START_TIMEOUT}s" >&2

  waited=0
  while [ "$waited" -lt "$TUNNEL_START_TIMEOUT" ]; do
    host=$(tunnel_hostname_from_log)
    if [ -n "$host" ]; then
      log "tunnel hostname is $host" >&2
      # Record it as not-yet-published: publish flips published to true.
      write_state "$host" 0 || true
      printf '%s\n' "$host"
      return 0
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      log "ERROR: cloudflared exited before publishing a hostname; see $CLOUDFLARED_LOG" >&2
      return 1
    fi
    sleep 1
    waited=$((waited + 1))
  done
  log "WARN: no tunnel hostname after ${TUNNEL_START_TIMEOUT}s" >&2
  return 1
}

# --------------------------------------------------------------------------
# publishing
# --------------------------------------------------------------------------

# published_hostname - the hostname already recorded in the checkout.
published_hostname() {
  local file
  file="$REPO_DIR/$INFO_DIR/run_info.json"
  [ -f "$file" ] || return 0
  python3 -c '
import json, sys
try:
    with open(sys.argv[1], "r", encoding="utf-8") as handle:
        data = json.load(handle)
except Exception:
    sys.exit(0)
if not isinstance(data, dict):
    sys.exit(0)
host = data.get("hostname") or ""
if not isinstance(host, str):
    sys.exit(0)
sys.stdout.write(host[8:] if host.startswith("https://") else host)
' "$file" 2>/dev/null
}

# write_run_info PATH HOSTNAME - run.yml-compatible run_info.json.
write_run_info() {
  local path=$1 hostname=${2:-} url=''
  if [ -n "$hostname" ]; then
    url="https://$hostname"
  fi
  printf '{"hostname":%s,"timestamp":"%s","port":%s,"source":"render"}\n' \
    "$(json_escape "$url")" "$(utc_now)" "$SERVER_PORT" >"$path" || return 1
  return 0
}

# write_render_info PATH - deliberately not named region.json, so the Render
# path cannot be mistaken for trigger_run.sh's GitHub-runner region check.
# Best effort: any failure yields empty fields and a still-valid document.
write_render_info() {
  local path=$1 payload='' region='' city='' country=''
  if [ -n "$RENDER_INFO_URL" ] && command -v curl >/dev/null 2>&1; then
    payload=$(curl -fsS --max-time 5 "$RENDER_INFO_URL" 2>/dev/null) || payload=''
  fi
  if [ -n "$payload" ]; then
    region=$(json_field "$payload" region)
    city=$(json_field "$payload" city)
    country=$(json_field "$payload" country)
  fi
  printf '{"region":%s,"city":%s,"country":%s,"url":%s,"instance":%s}\n' \
    "$(json_escape "$region")" "$(json_escape "$city")" "$(json_escape "$country")" \
    "$(json_escape "${RENDER_SERVICE_URL:-}")" "$(json_escape "${RENDER_INSTANCE_ID:-}")" \
    >"$path" || return 1
  return 0
}

# write_info_files HOSTNAME - the six files run_github.sh reads, in the exact
# format run.yml produces: a bare command plus one trailing newline, never a
# bare hostname, because run_github.sh sources that file as a shell command.
write_info_files() {
  local hostname=${1:-} line dir
  dir="$REPO_DIR/$INFO_DIR"
  mkdir -p "$dir" || return 1
  line="cloudflared access tcp --url tcp://localhost:$SERVER_PORT --hostname $hostname"
  printf '%s\n' "$line" >"$dir/cloudflare.sh" || return 1
  cp "$dir/cloudflare.sh" "$dir/cloudflare.bat" || return 1
  printf '%s\n' "$line" >"$REPO_DIR/cloudflare.sh" || return 1
  cp "$REPO_DIR/cloudflare.sh" "$REPO_DIR/cloudflare.bat" || return 1
  write_run_info "$dir/run_info.json" "$hostname" || return 1
  write_render_info "$dir/render_info.json" || return 1
  return 0
}

# publish HOSTNAME FORCE-0|1 - write the info files, then add, commit,
# pull --rebase, push. Always returns 0: a git failure must never take the
# tunnel or /healthz down. FORCE 1 publishes unconditionally, which is what a
# fresh container needs after a spindown wiped the filesystem.
publish() {
  local hostname=${1:-} force=${2:-0}
  if [ "$PUBLISH_ENABLED" != 1 ]; then
    log "PUBLISH is off; writing nothing to the repository"
    return 0
  fi
  if [ "$force" != 1 ] && [ -n "$hostname" ]; then
    local recorded
    recorded=$(published_hostname)
    if [ -n "$recorded" ] && [ "$recorded" = "$hostname" ]; then
      log "published hostname is already $hostname; nothing to commit"
      return 0
    fi
  fi
  if ! write_info_files "$hostname"; then
    log "WARN: could not write the info files; skipping publish for $hostname"
    write_state "$hostname" 0 || true
    return 0
  fi
  if [ -z "$PAT" ]; then
    log "WARN: GITHUB_PAT is not set; wrote the info files but skipped commit and push"
    write_state "$hostname" 0 || true
    return 0
  fi
  ensure_git_identity

  if ! git -C "$REPO_DIR" add -- "$INFO_DIR/cloudflare.sh" "$INFO_DIR/cloudflare.bat" \
    'cloudflare.sh' 'cloudflare.bat' "$INFO_DIR/run_info.json" "$INFO_DIR/render_info.json"; then
    log "WARN: git add failed; leaving the info files uncommitted"
    write_state "$hostname" 0 || true
    return 0
  fi
  # Reached on a forced publish whose content matches what is already on the
  # branch: a spindown can bring the checkout back with the files already
  # correct. Committing that would be an empty commit.
  if git -C "$REPO_DIR" diff --cached --quiet; then
    log "the info files already match what is published; nothing to commit"
    write_state "$hostname" 1 || true
    return 0
  fi
  if ! git -C "$REPO_DIR" commit --quiet -m "$GIT_COMMIT_SUBJECT" -- \
    "$INFO_DIR/cloudflare.sh" "$INFO_DIR/cloudflare.bat" \
    'cloudflare.sh' 'cloudflare.bat' "$INFO_DIR/run_info.json" "$INFO_DIR/render_info.json"; then
    log "WARN: git commit failed; leaving the info files uncommitted"
    write_state "$hostname" 0 || true
    return 0
  fi
  if ! git -C "$REPO_DIR" pull --rebase --quiet origin "$PUSH_BRANCH"; then
    log "WARN: git pull --rebase failed; the commit stays local and the next tick retries"
    write_state "$hostname" 0 || true
    return 0
  fi
  if ! git -C "$REPO_DIR" push --quiet "$(push_url)" "HEAD:$PUSH_BRANCH"; then
    log "WARN: git push to $PUSH_BRANCH failed; the next tick retries"
    write_state "$hostname" 0 || true
    return 0
  fi
  log "published $hostname to $GITHUB_REPO@$PUSH_BRANCH"
  write_state "$hostname" 1 || true
  return 0
}

# --------------------------------------------------------------------------
# lifecycle
# --------------------------------------------------------------------------

# preflight - assert what the service cannot run without, and warn about what
# it can degrade past.
preflight() {
  local missing=0 tool
  for tool in cloudflared python3 git; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      log "ERROR: required command not found: $tool"
      missing=1
    fi
  done
  if ! command -v curl >/dev/null 2>&1; then
    log "WARN: curl not found; $INFO_DIR/render_info.json will carry no geo data"
  fi
  if [ ! -x "$SERVER_BIN" ]; then
    log "note: $SERVER_BIN is not executable yet; fetch_server will download it"
  fi
  if [ -z "$PAT" ]; then
    log "WARN: GITHUB_PAT is not set; the tunnel and /healthz still work, but the hostname will not reach git"
  fi
  if [ "$PUBLISH_ENABLED" != 1 ]; then
    log "WARN: PUBLISH is off; nothing will be written to the repository"
  fi
  [ "$missing" = 0 ] || die "preflight failed"
  return 0
}

# supervise_loop - never returns. Restart whatever died; publish whenever the
# live hostname differs from the published one.
supervise_loop() {
  log "supervising every ${SUPERVISE_INTERVAL}s"
  while :; do
    if ! is_alive "$SERVER_PIDFILE"; then
      log "server is not running; restarting it"
      start_server || true
    fi

    local host=''
    if is_alive "$TUNNEL_PIDFILE"; then
      host=$(tunnel_hostname_from_log)
    else
      log "cloudflared is not running; restarting the tunnel"
      host=$(start_tunnel) || true
    fi
    if [ -n "$host" ] && [ "$host" != "$(published_hostname)" ]; then
      publish "$host" 0 || true
    fi

    sleep "$SUPERVISE_INTERVAL"
  done
}

# cleanup - SIGTERM/SIGINT handler. Reap the children, then leave cleanly so
# Render's shutdown does not escalate to SIGKILL.
cleanup() {
  trap - TERM INT
  log "SIGTERM/SIGINT received; stopping children"
  kill_pidfile "$SERVER_PIDFILE" server
  kill_pidfile "$TUNNEL_PIDFILE" cloudflared
  kill_pidfile "$KEEPALIVE_PIDFILE" keepalive
  pkill -f '^cloudflared tunnel --url tcp://127.0.0.1:' 2>/dev/null || true
  log "shutdown complete"
  exit 0
}

main() {
  local host=''
  resolve_config
  mkdir -p "$STATE_DIR" || die "cannot create $STATE_DIR"
  printf '%s\n' "$$" >"$STATE_PIDFILE" 2>/dev/null || true
  trap cleanup TERM INT
  log "render_supervisor.sh starting as pid $$"
  log "repo=$REPO_DIR branch=$PUSH_BRANCH release=$RELEASE server_port=$SERVER_PORT health_port=$PORT"

  preflight
  ensure_repo || die "no usable repository checkout at $REPO_DIR"
  if [ ! -x "$SERVER_BIN" ]; then
    fetch_server || die "could not install $SERVER_BIN from $RELEASE"
  fi

  start_server || log "WARN: the server did not come up; supervise_loop keeps trying"
  start_keepalive
  host=$(start_tunnel) || true
  if [ -n "$host" ]; then
    # published stays false until the git push actually lands.
    write_state "$host" 0 || true
    publish "$host" 1 || true
  else
    write_state '' 0 || true
    log "no tunnel hostname yet; /healthz reports null until one appears"
  fi
  supervise_loop
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then main "$@"; fi
