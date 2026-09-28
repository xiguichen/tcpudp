#!/usr/bin/env bash
#
# render_supervisor.sh - PID 1 inside the Render container.
#
# Owns three children - tinyproxy on $TCPUDP_PROXY_PORT, keepalive.py on $PORT,
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
  # The port the proxy listens on. This is also the port the Mac's
  # `cloudflared access tcp` forwards to, so it has to be the one Render
  # publishes - see start_tunnel, which reads it back out of the log.
  PROXY_PORT="${TCPUDP_PROXY_PORT:-7001}"
  # 0.0.0.0, not loopback: the tunnel reaches this container from outside, and
  # Render only routes to a port bound on all interfaces.
  PROXY_BIND="${TCPUDP_PROXY_BIND:-0.0.0.0}"
  # Run unprivileged. This proxy is reachable by anyone who learns the tunnel
  # hostname, so a tinyproxy bug should not hand out root.
  PROXY_USER="${TCPUDP_PROXY_USER:-tinyproxy}"
  REPO_DIR="${TCPUDP_REPO_DIR:-/app/repo}"
  INFO_DIR="${TCPUDP_INFO_DIR:-github_run}"
  STATE_DIR="${TCPUDP_STATE_DIR:-/run/tcpudp}"
  SUPERVISE_INTERVAL="${SUPERVISE_INTERVAL:-5}"
  TUNNEL_START_TIMEOUT="${TUNNEL_START_TIMEOUT:-60}"
  PAT="${GITHUB_PAT:-}"
  GITHUB_REPO="${GITHUB_REPO:-xiguichen/tcpudp}"
  PUSH_BRANCH="${GITHUB_PUSH_BRANCH:-run}"
  PUBLISH_ENABLED=$(truthy "${PUBLISH:-true}")
  PORT="${PORT:-10000}"
  KEEPALIVE_SCRIPT="${KEEPALIVE_SCRIPT:-/usr/local/bin/keepalive.py}"
  RENDER_INFO_URL="${RENDER_INFO_URL:-https://ipinfo.io/json}"
  # Surfaced through /healthz. "ready" means the port is answering, not just
  # that the process was spawned.
  PROXY_STATUS='starting'

  # Derived paths. Keyed off the env-var name, not the previous global: this
  # function can run more than once in one process (the test suite sources the
  # supervisor repeatedly against fresh sandboxes), and ${VAR:-default} against
  # the old global would freeze the first run's path forever.
  CLOUDFLARED_LOG="${TCPUDP_CLOUDFLARED_LOG:-$STATE_DIR/cloudflared.log}"
  STATE_PIDFILE="${TCPUDP_STATE_PIDFILE:-$STATE_DIR/supervisor.pid}"
  PROXY_PIDFILE="${TCPUDP_PROXY_PIDFILE:-$STATE_DIR/proxy.pid}"
  PROXY_CONF="${TCPUDP_PROXY_CONF:-$STATE_DIR/tinyproxy.conf}"
  KEEPALIVE_PIDFILE="${TCPUDP_KEEPALIVE_PIDFILE:-$STATE_DIR/keepalive.pid}"
  TUNNEL_PIDFILE="${TCPUDP_TUNNEL_PIDFILE:-$STATE_DIR/tunnel.pid}"

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
  # Coerced to a known token: this is interpolated into JSON, and a stray
  # value from the environment must not be able to break the document.
  local proxy_json
  case "$PROXY_STATUS" in
    ready | starting | down) proxy_json=$PROXY_STATUS ;;
    *) proxy_json='unknown' ;;
  esac
  printf '{"hostname":%s,"port":%s,"published":%s,"source":"render","proxy":"%s","updated":"%s"}\n' \
    "$host_json" "$PROXY_PORT" "$published_json" "$proxy_json" "$(utc_now)" >"$file.tmp" || return 1
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

# port_listening - is the proxy accepting on $PROXY_PORT?
#
# Probed over loopback even when the proxy binds 0.0.0.0: a wildcard bind
# answers on 127.0.0.1 too, so this works for both, and it does not depend on
# the container having a route to its own external address.
port_listening() {
  if command -v ss >/dev/null 2>&1; then
    local listing=''
    listing=$(ss -ltn 2>/dev/null) || listing=''
    case "$listing" in
      *":$PROXY_PORT "* | *":$PROXY_PORT"$'\n'*) return 0 ;;
    esac
  fi
  (exec 3<>"/dev/tcp/127.0.0.1/$PROXY_PORT") 2>/dev/null && return 0
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

# --------------------------------------------------------------------------
# proxy
# --------------------------------------------------------------------------

# write_proxy_conf - render the tinyproxy config.
#
# Generated rather than baked into the image so the port and bind address stay
# configurable, and so the file that decides what this container exposes is
# readable in one place.
write_proxy_conf() {
  cat >"$PROXY_CONF" <<CONF || return 1
## Generated by render_supervisor.sh. Edits are lost on restart.
User $PROXY_USER
Group $PROXY_USER
Listen $PROXY_BIND
Port $PROXY_PORT
Timeout 600

## A browser opens several connections per page, and a session opens several
## pages, so this is sized for a real session rather than a single request.
StartServers 10
MinSpareServers 5
MaxSpareServers 20
MaxClients 200

## Web only. Without this the instance relays whatever anyone asks it to fetch,
## to anyone who learns the tunnel hostname.
ConnectPort 80
ConnectPort 443

## Do not advertise the proxy in the Via header.
DisableViaHeader Yes

## Log to the container's stdout, so connections are visible in Render's log.
## Render discards the container filesystem on every free-tier sleep, so a log
## written to a file is a log nobody can read after the fact.
Syslog Off
LogFile "/dev/stdout"
LogLevel Info
CONF
  return 0
}

# _proxy_log_pump - read tinyproxy's output one line at a time, append each to
# $STATE_DIR/proxy.log and re-emit it on the supervisor's own stdout with a
# "proxy: " prefix.
#
# This mirrors the fix for the server's invisible log: Render discards the
# container filesystem on every free-tier sleep and offers no shell to read it,
# so a log written only to a file is a log nobody can read after the fact.
# Process substitution rather than a pipe, so $! stays tinyproxy's own pid and
# cleanup keeps pointing at the right process.
_proxy_log_pump() {
  local line
  while IFS= read -r line; do
    printf '%s\n' "$line" >>"$STATE_DIR/proxy.log"
    log "proxy: $line"
  done
}

# start_proxy - run tinyproxy and wait for it to listen.
start_proxy() {
  local pid attempt
  if ! command -v tinyproxy >/dev/null 2>&1; then
    log "ERROR: tinyproxy is not installed; the image should provide it"
    return 1
  fi
  write_proxy_conf || return 1
  log "starting tinyproxy on $PROXY_BIND:$PROXY_PORT"
  # -d keeps it in the foreground so $! is tinyproxy's own pid and the pidfile
  # and cleanup keep pointing at the right process. Without it tinyproxy forks
  # and the recorded pid is the parent that has already exited.
  nohup tinyproxy -d -c "$PROXY_CONF" > >(_proxy_log_pump) 2>&1 </dev/null &
  pid=$!
  # Record it only once it is confirmed alive: a proxy that dies (or fails to
  # start, as the stub can) must leave no pidfile for supervise_loop or cleanup
  # to act on, and a stale pidfile would look like a healthy proxy.
  if ! kill -0 "$pid" 2>/dev/null; then
    PROXY_STATUS='down'
    log "ERROR: tinyproxy fell over before its readiness poll; output above and in $STATE_DIR/proxy.log"
    return 1
  fi
  printf '%s\n' "$pid" >"$PROXY_PIDFILE"
  for ((attempt = 1; attempt <= 10; attempt++)); do
    if port_listening; then
      PROXY_STATUS='ready'
      log "proxy is accepting on $PROXY_BIND:$PROXY_PORT (pid $pid)"
      return 0
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      PROXY_STATUS='down'
      # The initial confirmation above can race a process that dies a moment
      # after spawning, so remove the pidfile here too - the pidfile means
      # "alive and being supervised", and a written-but-dead entry reads as
      # healthy to is_alive.
      rm -f "$PROXY_PIDFILE"
      log "ERROR: tinyproxy exited immediately; its output is above and in $STATE_DIR/proxy.log"
      return 1
    fi
    sleep 1
  done
  PROXY_STATUS='down'
  log "WARN: nothing is listening on $PROXY_BIND:$PROXY_PORT after 10s; supervise_loop will retry"
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
  log "starting a cloudflared quick tunnel to 127.0.0.1:$PROXY_PORT" >&2
  kill_pidfile "$TUNNEL_PIDFILE" cloudflared
  # Anchored, so this can never match the supervisor's own command line.
  pkill -f '^cloudflared tunnel --url tcp://127.0.0.1:' 2>/dev/null || true
  mkdir -p "$(dirname "$CLOUDFLARED_LOG")" 2>/dev/null || true
  : >"$CLOUDFLARED_LOG" 2>/dev/null || true
  nohup cloudflared tunnel --url "tcp://127.0.0.1:$PROXY_PORT" --no-autoupdate \
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
    "$(json_escape "$url")" "$(utc_now)" "$PROXY_PORT" >"$path" || return 1
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
  line="cloudflared access tcp --url tcp://localhost:$PROXY_PORT --hostname $hostname"
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
  # Anything fatal here runs before the first request is served, so a missing
  # tool is much cheaper to find now than as a silent failure later.
  for tool in cloudflared python3 git tinyproxy; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      log "ERROR: required command not found: $tool"
      missing=1
    fi
  done
  if ! command -v curl >/dev/null 2>&1; then
    log "WARN: curl not found; $INFO_DIR/render_info.json will carry no geo data"
  fi
  if ! command -v pkill >/dev/null 2>&1; then
    log "WARN: pkill not found; a restarted supervisor may leave orphans behind"
  fi
  # The proxy drops to root if this user is missing, because the config names it
  # and tinyproxy will not start without it. Say so here, where the log is still
  # being read, rather than as a bare "exited immediately" from start_proxy.
  # Only meaningful to a root supervisor - the image's apt package creates the
  # user - and running as root is exactly what the container does, so that is
  # the condition that gates it. A non-root run (local tests) skips it, because
  # there is no tinyproxy user on a developer machine either.
  if [ "$(id -u)" = 0 ] && \
     command -v tinyproxy >/dev/null 2>&1 && ! id "$PROXY_USER" >/dev/null 2>&1; then
    log "ERROR: user '$PROXY_USER' does not exist; tinyproxy will refuse to start"
    missing=1
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
  local reported='' live=''
  log "supervising every ${SUPERVISE_INTERVAL}s"
  while :; do
    if ! is_alive "$PROXY_PIDFILE"; then
      log "proxy is not running; restarting it"
      start_proxy || true
    fi
    # Rewrite the state only when the proxy status actually moved. The file
    # carries "proxy":"ready", and a status that goes stale in either direction
    # is worse than no status at all.
    if [ "$PROXY_STATUS" != "$reported" ]; then
      live=$(tunnel_hostname_from_log)
      if [ -n "$live" ] && [ "$live" = "$(published_hostname)" ]; then
        write_state "$live" 1 || true
      else
        write_state "$live" 0 || true
      fi
      reported=$PROXY_STATUS
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
  kill_pidfile "$PROXY_PIDFILE" proxy
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
  log "repo=$REPO_DIR branch=$PUSH_BRANCH proxy_port=$PROXY_PORT health_port=$PORT"

  preflight
  ensure_repo || die "no usable repository checkout at $REPO_DIR"

  # Before the tunnel, so the proxy is already answering when the tunnel
  # starts pointing at it. The tunnel is useless without a listener behind it.
  start_proxy || log "WARN: the proxy did not come up; supervise_loop keeps trying"
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
