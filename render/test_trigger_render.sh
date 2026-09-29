#!/usr/bin/env bash
#
# Tests for net.sh and trigger_render.sh (the Mac-side entry point).
#
#   bash render/test_trigger_render.sh
#
# Plain bash, no test framework, no real network:
#   - git talks to a throwaway bare repo, never to github.com
#   - /healthz is served by a local python server on 127.0.0.1
#   - `ping`, `cloudflared` and `pkill` are stubbed on PATH, so no ICMP leaves
#     the machine and no real tunnel daemon is ever started
#   - /etc/hosts is redirected to a sandbox file via HOSTS_FILE, so no sudo
#
# Deliberately `set -uo pipefail` without `-e`, so a failing assertion is
# reported and the test carries on instead of aborting the run.

set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
TRIGGER="$HERE/trigger_render.sh"
NET="$HERE/net.sh"
BLUEPRINT="$HERE/render.yaml"

export GIT_CONFIG_GLOBAL=/dev/null
export GIT_CONFIG_SYSTEM=/dev/null
export GIT_TERMINAL_PROMPT=0

BRANCH=run
FAST_IP=162.159.38.209
SLOW_IP=104.17.213.97
# Fixed non-default port for every trigger test, so the stub daemons never
# collide with anything a developer has running on 8080.
TEST_PORT=18080

tests_run=0
tests_failed=0
current_test=''
failures=()
sandboxes=()
health_pids=()
sandbox=''
origin=''
repo=''
hosts=''
health_port=''
trigger_out=''
trigger_rc=0

if [ ! -f "$TRIGGER" ] || [ ! -f "$NET" ]; then
  printf 'ERROR: %s and %s must both exist - nothing to test\n' "$TRIGGER" "$NET" >&2
  exit 1
fi

# --------------------------------------------------------------------------
# assertions
# --------------------------------------------------------------------------

fail() {
  failures+=("$current_test: $1${2:+ -- $2}")
  printf '  FAIL %s\n' "$1"
  [ -n "${2:-}" ] && printf '       %s\n' "$2"
  return 0
}

assert_eq() { # expected actual label
  [ "$1" = "$2" ] && return 0
  fail "$3" "expected [$1], got [$2]"
  return 1
}

assert_contains() { # haystack needle label
  case "$1" in *"$2"*) return 0 ;; esac
  fail "$3" "expected output to contain [$2]"
  return 1
}

assert_not_contains() { # haystack needle label
  case "$1" in
    *"$2"*)
      fail "$3" "expected output NOT to contain [$2]"
      return 1
      ;;
  esac
  return 0
}

assert_empty() { # actual label
  [ -z "$1" ] && return 0
  fail "$2" "expected nothing, got [$1]"
  return 1
}

assert_not_empty() { # actual label
  [ -n "$1" ] && return 0
  fail "$2" 'expected a value, got nothing'
  return 1
}

# assert_host_line_count HOST IP LABEL - exactly one hosts line is "<ip> <host>"
assert_host_line_count() { # host ip label
  local count
  count=$(grep -cE "^[[:space:]]*$2[[:space:]]+$1\$" "$hosts" 2>/dev/null)
  [ -n "$count" ] || count=0
  assert_eq 1 "$count" "$3"
}

run_test() { # fn name
  current_test=$2
  tests_run=$((tests_run + 1))
  local before=${#failures[@]}
  "$1" || true
  # One tunnel at a time, like production: stop whatever daemons this test
  # (and any earlier failed one) left running, so the next test gets a free
  # port. Without this the suite's first spawned fake cloudflared keeps
  # holding $TEST_PORT for the whole run and every later daemon dies with
  # EADDRINUSE - which flakes any assertion that needs live traffic.
  kill_sandbox_daemons
  if [ "${#failures[@]}" -gt "$before" ]; then
    tests_failed=$((tests_failed + 1))
    printf 'FAIL  %s\n' "$2"
  else
    printf 'ok    %s\n' "$2"
  fi
  return 0
}

# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------

# A ping stub. PING_MODE=fast makes FAST_IP the winner; PING_MODE=none makes
# every candidate unreachable.
make_ping_stub() { # dir mode
  cat >"$1/ping" <<'STUB'
#!/usr/bin/env bash
# Stub for ping(1). The last argument is the address.
ip=${@: -1}
emit() { printf 'round-trip min/avg/max/stddev = 1.0/%s/3.0/0.5 bytes\n' "$1"; }
case "${PING_MODE:-fast}" in
  none) exit 1 ;;
  fast)
    case "$ip" in
      162.159.38.209) emit 5.0; exit 0 ;;
      104.17.213.97) emit 50.0; exit 0 ;;
      *) exit 1 ;;
    esac
    ;;
esac
exit 1
STUB
  chmod +x "$1/ping"
}

# A cloudflared(1) stub. Records its full command line to CF_STUB_LOG (append),
# then binds the port from --url and stays alive, standing in for the real
# daemon so the script's "listening on" probe really runs. Treated like the real
# thing afterwards: killed with TERM.
make_cloudflared_stub() { # dir
  cat >"$1/cloudflared" <<'STUB'
#!/usr/bin/env bash
[ -n "${CF_STUB_LOG:-}" ] || exit 1
# $* omits argv[0]; a real ps-shaped command line includes the binary name.
printf 'cloudflared %s\n' "$*" >>"$CF_STUB_LOG"
want=0
url=''
for arg in "$@"; do
  if [ "$want" = 1 ]; then url=$arg; want=0; fi
  [ "$arg" = --url ] && want=1
done
port=${url##*:}
case "$port" in
  '' | *[!0-9]*) exit 1 ;;
esac
exec python3 -c '
import socket, sys, time
port = int(sys.argv[1])
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", port))
s.listen(16)
while True:
    time.sleep(60)
' "$port"
STUB
  chmod +x "$1/cloudflared"
}

# A pkill(1) stub: records its invocation so tests can assert the always-restart
# cleanup ran, then does nothing - stopping the fake daemon is the pidfile's job.
make_pkill_stub() { # dir
  cat >"$1/pkill" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"${CF_PKILL_LOG:-/dev/null}"
exit 0
STUB
  chmod +x "$1/pkill"
}

# A local /healthz that walks a list of canned responses, one per request, and
# then repeats the last one forever. Lets a test change what the caller sees
# between polls without racing it.
make_health_server() { # docroot port
  cat >"$1/server.py" <<'PY'
import http.server, json, os, sys

docroot, port = sys.argv[1], int(sys.argv[2])
state = {"n": 0}


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        try:
            # Every request lands here - the keepalive tests use this to
            # observe background traffic without touching the network.
            with open(os.path.join(docroot, "hits.log"), "a") as fh:
                fh.write("hit\n")
        except Exception:
            pass
        try:
            with open(os.path.join(docroot, "responses.json")) as fh:
                responses = json.load(fh)
        except Exception:
            responses = [{"status": 200, "body": "not json"}]
        i = min(state["n"], len(responses) - 1)
        state["n"] += 1
        r = responses[i]
        body = r.get("body", "").encode()
        self.send_response(r.get("status", 200))
        self.send_header("Content-Type", r.get("type", "application/json"))
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


http.server.HTTPServer(("127.0.0.1", port), Handler).serve_forever()
PY
}

free_port() {
  python3 -c 'import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()'
}

# new_sandbox [ping_mode] - bare origin + a repo whose render/ holds the scripts
new_sandbox() {
  local mode=${1:-fast}
  sandbox=$(mktemp -d "${TMPDIR:-/tmp}/tcpudp-render-test.XXXXXX")
  # Physical path: ps reports resolved paths, so a relative TMPDIR would not
  # match the stubs' command lines.
  sandbox=$(cd "$sandbox" && pwd -P)
  sandboxes+=("$sandbox")
  origin="$sandbox/origin.git"
  repo="$sandbox/repo"
  hosts="$sandbox/hosts"
  mkdir -p "$sandbox/seed" "$sandbox/bin" "$sandbox/health" || return 1
  : >"$hosts"

  make_ping_stub "$sandbox/bin" "$mode"
  make_cloudflared_stub "$sandbox/bin"
  make_pkill_stub "$sandbox/bin"
  # The stub has to be on PATH for the net.sh library tests too, not only for
  # run_trigger: probe_best_edge_ip calls a bare `ping`, so without this the
  # suite would send real ICMP and measure the user's real network.
  PATH="$sandbox/bin:$PATH"
  export PATH
  export PING_MODE=$mode

  git init --quiet --bare --initial-branch="$BRANCH" "$origin" || return 1
  (
    cd "$sandbox/seed" || exit 1
    git init --quiet --initial-branch="$BRANCH" . || exit 1
    git config user.name sandbox
    git config user.email sandbox@example.invalid
    mkdir -p github_run
    printf 'cloudflared access tcp --url tcp://0.0.0.0:%s --hostname seed.trycloudflare.com\n' "$TEST_PORT" \
      >github_run/cloudflare.sh
    git add README.md 2>/dev/null || true
    printf 'seed\n' >README.md
    git add -A
    git commit --quiet -m seed
    git remote add origin "$origin"
    git push --quiet origin "$BRANCH"
  ) >/dev/null 2>&1 || return 1
  git clone --quiet --branch "$BRANCH" "$origin" "$repo" || return 1
  mkdir -p "$repo/render"
  cp "$TRIGGER" "$NET" "$repo/render/" || return 1
  chmod +x "$repo/render/trigger_render.sh"
  return 0
}

# start_health RESP_JSON - serve that list of responses on 127.0.0.1
start_health() {
  printf '%s' "$1" >"$sandbox/health/responses.json"
  make_health_server "$sandbox/health"
  health_port=$(free_port)
  python3 "$sandbox/health/server.py" "$sandbox/health" "$health_port" \
    >"$sandbox/health/server.log" 2>&1 &
  health_pids+=("$!")
  local i
  for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    if curl -fsS --max-time 1 "http://127.0.0.1:$health_port/ping" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

# run_trigger [extra args] - invoke the script; sets trigger_out and trigger_rc
run_trigger() {
  trigger_out=$(cd "$repo" && env \
    PATH="$sandbox/bin:$PATH" \
    HOSTS_FILE="$hosts" \
    PING_MODE="${PING_MODE:-fast}" \
    POLL_INTERVAL=1 \
    RECONCILE_TIMEOUT="${RECONCILE_TIMEOUT_OVERRIDE:-2}" \
    GITHUB_PUSH_BRANCH="$BRANCH" \
    GITHUB_REMOTE_URL="$origin" \
    TCPUDP_INFO_DIR=github_run \
    TCPUDP_RUN_DIR="$sandbox/runstate" \
    TCPUDP_PROXY_PORT="$TEST_PORT" \
    TCPUDP_LAN_IP="10.0.0.99" \
    KEEPALIVE_INTERVAL="${KEEPALIVE_INTERVAL_OVERRIDE:-180}" \
    CF_STUB_LOG="$sandbox/cf-argv.log" \
    CF_PKILL_LOG="$sandbox/cf-pkill.log" \
    RENDER_HEALTH_URL="http://127.0.0.1:$health_port/healthz" \
    "$repo/render/trigger_render.sh" --health-url "http://127.0.0.1:$health_port/healthz" \
    "$@" 2>&1)
  trigger_rc=$?
  return 0
}

# Load net.sh into the current shell for the library-level tests.
load_net() {
  # shellcheck disable=SC1090
  . "$NET"
}

# kill_sandbox_daemons - stop fake cloudflared/keepalive that any sandbox left
# running (their pids live in each sandbox's runstate pidfiles). Safe to call
# repeatedly: missing pidfiles and already-dead pids are ignored.
kill_sandbox_daemons() {
  local dir pidfile pid
  for dir in "${sandboxes[@]:-}"; do
    [ -n "$dir" ] || continue
    for pidfile in cloudflared.pid keepalive.pid; do
      pid=$(cat "$dir/runstate/$pidfile" 2>/dev/null || true)
      [ -n "$pid" ] && kill -TERM "$pid" 2>/dev/null || true
    done
  done
  return 0
}

teardown() {
  local pid dir
  for pid in "${health_pids[@]:-}"; do
    [ -n "$pid" ] && kill -TERM "$pid" 2>/dev/null
  done
  kill_sandbox_daemons
  for dir in "${sandboxes[@]:-}"; do
    [ -n "$dir" ] || continue
    rm -rf "$dir"
  done
  return 0
}
trap teardown EXIT

# --------------------------------------------------------------------------
# net.sh
# --------------------------------------------------------------------------

# The fastest reachable candidate wins, reported as "<ip> <avg_rtt_ms>".
test_probe_best_edge_ip_picks_lowest_rtt() {
  new_sandbox fast || return 1
  (
    load_net
    CF_EDGE_IPS="$FAST_IP $SLOW_IP" probe_best_edge_ip
  ) >"$sandbox/probe.out" 2>&1
  assert_eq "$FAST_IP 5.0" "$(cat "$sandbox/probe.out")" probe-returns-fastest-ip
}

# No candidate answers, so the probe fails rather than guessing.
test_probe_best_edge_ip_fails_when_none_respond() {
  new_sandbox none || return 1
  local rc=0
  (
    load_net
    CF_EDGE_IPS="$FAST_IP $SLOW_IP" probe_best_edge_ip
  ) >"$sandbox/probe.out" 2>&1 || rc=$?
  assert_not_empty "$rc" 'probe must exit non-zero when nothing responds'
  assert_empty "$(cat "$sandbox/probe.out")" probe-echoes-nothing-on-failure
}

# A host with no entry gets one, and the caller is told which IP was used.
# A hosts write that fails must not be reported as a success. The whole point of
# this function is to dodge the slow CN edge IP, so silently "succeeding" would
# hand the user exactly the outcome they are trying to avoid - while telling
# them it worked. Simulated with a sudo stub that always fails and a directory
# the test cannot write.
test_pin_tunnel_hostname_reports_a_failed_write() {
  new_sandbox fast || return 1
  local ro="$sandbox/ro"
  mkdir -p "$ro"
  printf '127.0.0.1 localhost\n' >"$ro/hosts"
  printf '#!/bin/sh\nexit 1\n' >"$sandbox/bin/sudo"
  chmod +x "$sandbox/bin/sudo"
  # The file itself must be read-only. Locking the directory would not do:
  # appending to an already-writable file does not need directory write
  # permission, so the write would succeed and the test would prove nothing.
  chmod 444 "$ro/hosts"

  local out rc=0
  # 2>&1 matters: the diagnostic goes to stderr, and $(...) alone would
  # capture only the stdout side, making this test pass for the wrong reason.
  out=$(
    load_net
    HOSTS_FILE="$ro/hosts" CF_EDGE_IPS="$FAST_IP $SLOW_IP" \
      pin_tunnel_hostname 'fresh.trycloudflare.com' 2>&1
  ) || rc=$?
  chmod 644 "$ro/hosts"

  assert_contains "$out" 'could not write' pin-reports-the-failure
  if [ "$rc" -eq 0 ]; then
    fail 'a failed hosts write must return non-zero'
  fi
}

test_pin_tunnel_hostname_appends_when_absent() {
  new_sandbox fast || return 1
  local out
  out=$(
    load_net
    HOSTS_FILE="$hosts" CF_EDGE_IPS="$FAST_IP $SLOW_IP" \
      pin_tunnel_hostname 'fresh.trycloudflare.com'
  )
  assert_contains "$out" 'pinned' pin-reports-pinned
  assert_contains "$out" "$FAST_IP" pin-reports-the-ip
  assert_host_line_count 'fresh.trycloudflare.com' "$FAST_IP" hosts-has-one-line
}

# Re-running must not append a duplicate.
test_pin_tunnel_hostname_is_idempotent() {
  new_sandbox fast || return 1
  local first second
  first=$(
    load_net
    HOSTS_FILE="$hosts" CF_EDGE_IPS="$FAST_IP $SLOW_IP" \
      pin_tunnel_hostname 'twice.trycloudflare.com'
  )
  second=$(
    load_net
    HOSTS_FILE="$hosts" CF_EDGE_IPS="$FAST_IP $SLOW_IP" \
      pin_tunnel_hostname 'twice.trycloudflare.com'
  )
  assert_contains "$first" 'pinned' first-call-pins
  assert_contains "$second" 'already-pinned' second-call-is-a-no-op
  assert_host_line_count 'twice.trycloudflare.com' "$FAST_IP" still-exactly-one-line
}

# A stale entry pointing at the wrong IP is replaced, not duplicated.
test_pin_tunnel_hostname_replaces_stale_entry() {
  new_sandbox fast || return 1
  printf '%s %s\n' "$SLOW_IP" 'stale.trycloudflare.com' >>"$hosts"
  (
    load_net
    HOSTS_FILE="$hosts" CF_EDGE_IPS="$FAST_IP $SLOW_IP" \
      pin_tunnel_hostname 'stale.trycloudflare.com' >/dev/null
  )
  assert_host_line_count 'stale.trycloudflare.com' "$FAST_IP" stale-entry-replaced
  assert_empty "$(grep -E "[[:space:]]$SLOW_IP[[:space:]]+stale\.trycloudflare\.com\$" "$hosts")" \
    old-ip-no-longer-present
}

# --------------------------------------------------------------------------
# trigger_render.sh
# --------------------------------------------------------------------------

# Render serves an HTML "spinning up" page at the edge on a cold start. That page
# never reaches our server, so the caller must keep polling instead of treating
# it as an answer. This is the single most common real occurrence.
test_trigger_render_waits_through_a_non_json_loading_page() {
  new_sandbox fast || return 1
  start_health '[
    {"status":200,"type":"text/html","body":"<html><body>Loading...</body></html>"},
    {"status":200,"type":"text/html","body":"<html><body>Loading...</body></html>"},
    {"status":200,"type":"text/html","body":"<html><body>Loading...</body></html>"},
    {"status":200,"type":"application/json","body":"{\"status\":\"ok\",\"hostname\":\"woke.trycloudflare.com\",\"port\":8080,\"published\":false,\"source\":\"render\",\"updated\":\"2026-09-27T00:00:00Z\"}"}
  ]' || return 1
  run_trigger --timeout 30
  assert_eq 0 "$trigger_rc" exits-0-after-the-loading-page
  assert_contains "$trigger_out" 'woke.trycloudflare.com' reports-the-live-hostname
}

# Our own server answers 200 with hostname:null while the tunnel is still
# registering. That means "still waking", not "broken".
test_trigger_render_waits_for_null_hostname_then_succeeds() {
  new_sandbox fast || return 1
  start_health '[
    {"status":200,"type":"application/json","body":"{\"status\":\"ok\",\"hostname\":null,\"port\":8080,\"published\":false,\"source\":\"render\",\"updated\":\"2026-09-27T00:00:00Z\"}"},
    {"status":200,"type":"application/json","body":"{\"status\":\"ok\",\"hostname\":null,\"port\":8080,\"published\":false,\"source\":\"render\",\"updated\":\"2026-09-27T00:00:00Z\"}"},
    {"status":200,"type":"application/json","body":"{\"status\":\"ok\",\"hostname\":null,\"port\":8080,\"published\":false,\"source\":\"render\",\"updated\":\"2026-09-27T00:00:00Z\"}"},
    {"status":200,"type":"application/json","body":"{\"status\":\"ok\",\"hostname\":\"up.trycloudflare.com\",\"port\":8080,\"published\":false,\"source\":\"render\",\"updated\":\"2026-09-27T00:00:00Z\"}"}
  ]' || return 1
  run_trigger --timeout 30
  assert_eq 0 "$trigger_rc" exits-0-once-the-hostname-appears
  assert_contains "$trigger_out" 'up.trycloudflare.com' reports-the-live-hostname
}

# A service that never reports a hostname must fail loudly, not hang forever.
test_trigger_render_times_out_with_clear_error() {
  new_sandbox fast || return 1
  start_health '[{"status":200,"type":"application/json","body":"{\"status\":\"ok\",\"hostname\":null,\"port\":8080,\"published\":false,\"source\":\"render\",\"updated\":\"2026-09-27T00:00:00Z\"}"}]' \
    || return 1
  run_trigger --timeout 6
  assert_not_empty "$trigger_rc" 'times-out-with-non-zero-status'
  assert_contains "$trigger_out" 'timed out' 'says-it-timed-out'
}

# The normal case after a free-tier sleep: git still holds the previous tunnel's
# hostname. The live one must win locally so run_github.sh keeps working.
test_trigger_render_uses_live_hostname_when_branch_is_stale() {
  new_sandbox fast || return 1
  start_health '[{"status":200,"type":"application/json","body":"{\"status\":\"ok\",\"hostname\":\"new.trycloudflare.com\",\"port\":8080,\"published\":false,\"source\":\"render\",\"updated\":\"2026-09-27T00:00:00Z\"}"}]' \
    || return 1
  run_trigger --timeout 30
  assert_eq 0 "$trigger_rc" exits-0-even-when-git-is-stale
  assert_contains "$trigger_out" 'stale' warns-that-git-is-behind
  # Ruling 1: the local file must keep the exact one-line command format that
  # run_github.sh sources, never a bare hostname.
  assert_eq \
    'cloudflared access tcp --url tcp://0.0.0.0:18080 --hostname new.trycloudflare.com' \
    "$(cat "$repo/github_run/cloudflare.sh")" local-file-holds-the-live-hostname
}

# When git already agrees, there is nothing to warn about.
test_trigger_render_accepts_matching_branch() {
  new_sandbox fast || return 1
  printf 'cloudflared access tcp --url tcp://0.0.0.0:18080 --hostname match.trycloudflare.com\n' \
    >"$repo/github_run/cloudflare.sh"
  # Must reach origin: the script checks origin/$BRANCH, so a purely local
  # commit would look stale and the assertion below would pass for the wrong
  # reason.
  ( cd "$repo" && git add -A && git commit --quiet -m 'matching hostname' &&
    git push --quiet origin "$BRANCH" ) >/dev/null 2>&1
  start_health '[{"status":200,"type":"application/json","body":"{\"status\":\"ok\",\"hostname\":\"match.trycloudflare.com\",\"port\":8080,\"published\":false,\"source\":\"render\",\"updated\":\"2026-09-27T00:00:00Z\"}"}]' \
    || return 1
  run_trigger --timeout 30
  assert_eq 0 "$trigger_rc" exits-0-when-git-already-matches
  assert_not_contains "$trigger_out" 'stale' no-stale-warning-when-in-sync
}

# The user has to be told what to run next.
# published=false means the service's own git write-back did not land, so no
# push is coming. Polling for one anyway costs the full reconcile timeout on
# every single run and always ends at the same fallback - the exact waste this
# test exists to prevent.
test_trigger_render_does_not_wait_when_no_publish_is_expected() {
  new_sandbox fast || return 1
  start_health '[{"status":200,"type":"application/json","body":"{\"status\":\"ok\",\"hostname\":\"nopub.trycloudflare.com\",\"port\":8080,\"published\":false,\"source\":\"render\",\"updated\":\"2026-09-27T00:00:00Z\"}"}]' \
    || return 1

  # Make the reconcile timeout far larger than the ceiling below. If the wait
  # were not skipped, elapsed would be >= 30s; if it was skipped, elapsed is
  # however long the git/curl work takes. A 2s timeout with a 2s ceiling made
  # the two indistinguishable and the test failed intermittently under load.
  local started elapsed
  started=$(date +%s)
  RECONCILE_TIMEOUT_OVERRIDE=30 run_trigger --timeout 30
  elapsed=$(( $(date +%s) - started ))

  assert_eq 0 "$trigger_rc" exits-0
  assert_not_contains "$trigger_out" 'waiting up to' 'does-not-wait-for-a-publish'
  assert_contains "$trigger_out" 'nothing to wait for' explains-why-it-skipped
  # The behaviour itself, not just the wording.
  if [ "$elapsed" -ge 10 ]; then
    fail 'skips-the-wait-in-practice' "still took ${elapsed}s despite published=false"
  fi
  # It must still do the useful part: write the live hostname locally.
  assert_eq \
    'cloudflared access tcp --url tcp://0.0.0.0:18080 --hostname nopub.trycloudflare.com' \
    "$(cat "$repo/github_run/cloudflare.sh")" still-writes-the-live-hostname
}

# published=true means a push may be in flight, so the wait must stay. Losing
# this would make the script race a push that is about to succeed.
test_trigger_render_still_waits_when_a_publish_is_in_flight() {
  new_sandbox fast || return 1
  start_health '[{"status":200,"type":"application/json","body":"{\"status\":\"ok\",\"hostname\":\"pub.trycloudflare.com\",\"port\":8080,\"published\":true,\"source\":\"render\",\"updated\":\"2026-09-27T00:00:00Z\"}"}]' \
    || return 1
  run_trigger --timeout 30
  assert_eq 0 "$trigger_rc" exits-0
  assert_contains "$trigger_out" 'waiting up to' 'waits-when-a-push-may-be-landing'
  assert_not_contains "$trigger_out" 'nothing to wait for' no-skip-when-published
}

# A published value we cannot interpret must not be trusted to skip the wait.
# Only a real JSON false means "no push is coming"; anything else is unknown,
# and unknown must keep the old behaviour.
test_trigger_render_does_not_trust_a_non_boolean_published() {
  local bad bad_in_body
  for bad in '"false"' '""' '0' 'null' '"nope"'; do
    new_sandbox fast || return 1
    # $bad is a raw JSON fragment that goes *inside* the body string, so its
    # own quotes must be escaped. Unescaped they terminate the string early and
    # the health server silently falls back to "not json" - which looks like a
    # trigger failure rather than a fixture bug.
    bad_in_body=${bad//\"/\\\"}
    start_health "[{\"status\":200,\"type\":\"application/json\",\"body\":\"{\\\"status\\\":\\\"ok\\\",\\\"hostname\\\":\\\"weird.trycloudflare.com\\\",\\\"port\\\":8080,\\\"published\\\":$bad_in_body,\\\"source\\\":\\\"render\\\"}\"}]" \
      || return 1
    run_trigger --timeout 30
    assert_eq 0 "$trigger_rc" "exits-0-for-published=$bad"
    assert_not_contains "$trigger_out" 'nothing to wait for' \
      "does-not-skip-the-wait-on-published=$bad"
  done
}

test_trigger_render_prints_handoff() {
  new_sandbox fast || return 1
  start_health '[{"status":200,"type":"application/json","body":"{\"status\":\"ok\",\"hostname\":\"handoff.trycloudflare.com\",\"port\":18080,\"published\":false,\"source\":\"render\",\"updated\":\"2026-09-27T00:00:00Z\"}"}]' \
    || return 1
  run_trigger --timeout 30
  assert_contains "$trigger_out" 'this Mac:       http://127.0.0.1:18080' \
    prints-the-local-proxy-url
  assert_contains "$trigger_out" 'other machines: http://10.0.0.99:18080' \
    prints-the-lan-proxy-url
  assert_contains "$trigger_out" \
    'cloudflared --no-autoupdate access tcp --url tcp://0.0.0.0:18080 --hostname handoff.trycloudflare.com' \
    prints-the-tunnel-command
}

# The script starts the tunnel itself now: pidfile + log, on 0.0.0.0 so other
# machines can proxy through it, and a real listener probe.
test_trigger_render_starts_the_tunnel_itself() {
  new_sandbox fast || return 1
  start_health '[{"status":200,"type":"application/json","body":"{\"status\":\"ok\",\"hostname\":\"auto.trycloudflare.com\",\"port\":18080,\"published\":false,\"source\":\"render\",\"updated\":\"2026-09-27T00:00:00Z\"}"}]' \
    || return 1
  run_trigger --timeout 30

  assert_eq 0 "$trigger_rc" exits-0-with-a-started-tunnel
  assert_eq \
    'cloudflared --no-autoupdate access tcp --url tcp://0.0.0.0:18080 --hostname auto.trycloudflare.com' \
    "$(cat "$sandbox/cf-argv.log")" spawns-the-0-0-0-0-command
  assert_contains "$trigger_out" 'started pid' reports-the-start
  assert_contains "$trigger_out" 'listening on 0.0.0.0:18080' reports-the-listener
  assert_contains "$(cat "$sandbox/cf-pkill.log")" ':18080' pkill-scoped-to-our-port

  local pid
  pid=$(cat "$sandbox/runstate/cloudflared.pid")
  [ -n "$pid" ] || fail 'has-a-pidfile'
  if ! kill -0 "$pid" 2>/dev/null; then
    fail 'the-spawned-tunnel-is-alive'
  fi
}

# Render sleeps after ~15 idle minutes, and a wake wipes the filesystem and
# mints a new hostname. While the tunnel is up, the script must keep /healthz
# warm from the background - and the keepalive must stop when the tunnel dies,
# so it never points traffic at a dead hostname.
test_trigger_render_keepalive_hits_render_until_tunnel_dies() {
  new_sandbox fast || return 1
  start_health '[{"status":200,"type":"application/json","body":"{\"status\":\"ok\",\"hostname\":\"keep.trycloudflare.com\",\"port\":18080,\"published\":false,\"source\":\"render\",\"updated\":\"2026-09-27T00:00:00Z\"}"}]' \
    || return 1
  KEEPALIVE_INTERVAL_OVERRIDE=1 run_trigger --timeout 30

  local kpid tpid base after
  kpid=$(cat "$sandbox/runstate/keepalive.pid" 2>/dev/null)
  [ -n "$kpid" ] || fail 'keepalive-has-a-pidfile'
  if ! kill -0 "$kpid" 2>/dev/null; then
    fail 'keepalive-is-alive'
  fi
  assert_contains "$trigger_out" 'keepalive' reports-the-keepalive

  # With a 1s interval the keepalive visibly generates traffic of its own.
  base=$(awk 'END{print NR}' "$sandbox/health/hits.log" 2>/dev/null)
  base=${base:-0}
  sleep 3
  after=$(awk 'END{print NR}' "$sandbox/health/hits.log" 2>/dev/null)
  [ "${after:-0}" -gt "$base" ] || fail 'keepalive-generates-traffic' \
    "hits before=$base after=$after"

  # The keepalive dies with its tunnel.
  tpid=$(cat "$sandbox/runstate/cloudflared.pid" 2>/dev/null)
  kill -TERM "$tpid" 2>/dev/null
  sleep 3
  if kill -0 "$kpid" 2>/dev/null; then
    fail 'keepalive-stops-when-the-tunnel-dies'
  fi
}

# Always restart: a second run with a changed hostname must stop the previous
# daemon and spawn a fresh one that binds the new --hostname.
test_trigger_render_restarts_tunnel_when_hostname_changes() {
  new_sandbox fast || return 1
  start_health '[
    {"status":200,"type":"application/json","body":"{\"status\":\"ok\",\"hostname\":\"boot1.trycloudflare.com\",\"port\":18080,\"published\":false,\"source\":\"render\",\"updated\":\"2026-09-27T00:00:00Z\"}"},
    {"status":200,"type":"application/json","body":"{\"status\":\"ok\",\"hostname\":\"boot2.trycloudflare.com\",\"port\":18080,\"published\":false,\"source\":\"render\",\"updated\":\"2026-09-27T00:00:00Z\"}"}
  ]' || return 1

  run_trigger --timeout 30
  local pid1 pid2 n kpid1 kpid2
  pid1=$(cat "$sandbox/runstate/cloudflared.pid")
  kpid1=$(cat "$sandbox/runstate/keepalive.pid" 2>/dev/null)
  n=$(awk 'END{print NR}' "$sandbox/cf-argv.log")
  assert_eq 1 "$n" spawns-once-on-the-first-run

  run_trigger --timeout 30
  pid2=$(cat "$sandbox/runstate/cloudflared.pid")
  kpid2=$(cat "$sandbox/runstate/keepalive.pid" 2>/dev/null)
  n=$(awk 'END{print NR}' "$sandbox/cf-argv.log")
  assert_eq 2 "$n" spawns-again-on-the-second-run

  if [ "$pid1" = "$pid2" ]; then
    fail 'restart-spawns-a-fresh-process' "pid was reused: $pid1"
  fi
  if kill -0 "$pid1" 2>/dev/null; then
    fail 'the-old-tunnel-was-killed'
  fi
  if ! kill -0 "$pid2" 2>/dev/null; then
    fail 'the-new-tunnel-is-alive'
  fi
  # The keepalive follows the same lifecycle as the tunnel it watches.
  if [ -z "$kpid1" ] || [ -z "$kpid2" ] || [ "$kpid1" = "$kpid2" ]; then
    fail 'keepalive-replaced-on-restart' "keepalive pid1=[$kpid1] pid2=[$kpid2]"
  fi
  if kill -0 "$kpid1" 2>/dev/null; then
    fail 'the-old-keepalive-was-killed'
  fi
  if ! kill -0 "$kpid2" 2>/dev/null; then
    fail 'the-new-keepalive-is-alive'
  fi
  assert_contains "$(sed -n '2p' "$sandbox/cf-argv.log")" \
    '--hostname boot2.trycloudflare.com' second-spawn-targets-the-new-hostname
  assert_eq \
    'cloudflared access tcp --url tcp://0.0.0.0:18080 --hostname boot2.trycloudflare.com' \
    "$(cat "$repo/github_run/cloudflare.sh")" local-file-tracks-the-new-hostname
}

run_test test_probe_best_edge_ip_picks_lowest_rtt test_probe_best_edge_ip_picks_lowest_rtt
run_test test_probe_best_edge_ip_fails_when_none_respond test_probe_best_edge_ip_fails_when_none_respond
# The service name lives in two places: `name:` in the Blueprint, and the host
# baked into trigger_render.sh's default RENDER_HEALTH_URL. When those drift,
# the failure is a bare 404 with "x-render-routing: no-server", which reads as a
# dead service rather than a typo - and the default URL is what a user gets
# before they know the env var exists.
test_default_health_url_matches_the_blueprint_service_name() {
  local name host

  name=$(sed -n 's/^    name: *//p' "$BLUEPRINT" | head -1)
  assert_eq tcpudp "$name" 'blueprint-service-name'

  host=$(sed -n 's|.*RENDER_HEALTH_URL:-https://\([^./]*\)\..*|\1|p' "$TRIGGER" | head -1)
  assert_eq "$name" "$host" 'default-health-url-host-matches-service-name'
}

run_test test_default_health_url_matches_the_blueprint_service_name test_default_health_url_matches_the_blueprint_service_name
run_test test_pin_tunnel_hostname_reports_a_failed_write test_pin_tunnel_hostname_reports_a_failed_write
run_test test_pin_tunnel_hostname_appends_when_absent test_pin_tunnel_hostname_appends_when_absent
run_test test_pin_tunnel_hostname_is_idempotent test_pin_tunnel_hostname_is_idempotent
run_test test_pin_tunnel_hostname_replaces_stale_entry test_pin_tunnel_hostname_replaces_stale_entry
run_test test_trigger_render_waits_through_a_non_json_loading_page test_trigger_render_waits_through_a_non_json_loading_page
run_test test_trigger_render_waits_for_null_hostname_then_succeeds test_trigger_render_waits_for_null_hostname_then_succeeds
run_test test_trigger_render_times_out_with_clear_error test_trigger_render_times_out_with_clear_error
run_test test_trigger_render_uses_live_hostname_when_branch_is_stale test_trigger_render_uses_live_hostname_when_branch_is_stale
run_test test_trigger_render_does_not_wait_when_no_publish_is_expected test_trigger_render_does_not_wait_when_no_publish_is_expected
run_test test_trigger_render_still_waits_when_a_publish_is_in_flight test_trigger_render_still_waits_when_a_publish_is_in_flight
run_test test_trigger_render_does_not_trust_a_non_boolean_published test_trigger_render_does_not_trust_a_non_boolean_published
run_test test_trigger_render_accepts_matching_branch test_trigger_render_accepts_matching_branch
run_test test_trigger_render_prints_handoff test_trigger_render_prints_handoff
run_test test_trigger_render_starts_the_tunnel_itself test_trigger_render_starts_the_tunnel_itself
run_test test_trigger_render_keepalive_hits_render_until_tunnel_dies test_trigger_render_keepalive_hits_render_until_tunnel_dies
run_test test_trigger_render_restarts_tunnel_when_hostname_changes test_trigger_render_restarts_tunnel_when_hostname_changes

printf '\n%s tests, %s failed\n' "$tests_run" "$tests_failed"
if [ "$tests_failed" -gt 0 ]; then
  printf 'failures:\n'
  for failure in "${failures[@]}"; do printf '  %s\n' "$failure"; done
  exit 1
fi
exit 0
