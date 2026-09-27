#!/usr/bin/env bash
#
# Tests for render_supervisor.sh.
#
#   bash render/test_scripts.sh
#
# Plain bash, no test framework, no network. Every git interaction goes to a
# throwaway bare repo created by new_sandbox, and JSON is parsed with python3
# because jq is not installed on the developer's machine.
#
# Deliberately `set -uo pipefail` without `-e`, so a failing assertion is
# reported and the test carries on to its next assertion instead of aborting
# the whole run.

set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
SUPERVISOR="$HERE/render_supervisor.sh"
KEEPALIVE_SCRIPT_PATH="$HERE/keepalive.py"
DOCKERFILE="$HERE/Dockerfile"

# Keep git away from the developer's own identity: the suite asserts that
# render_supervisor.sh configures one inside the clone, and that assertion is
# only meaningful if no ambient identity could satisfy the commit.
export GIT_CONFIG_GLOBAL=/dev/null
export GIT_CONFIG_SYSTEM=/dev/null
export GIT_TERMINAL_PROMPT=0

BRANCH=run
COMMIT_SUBJECT="Auto-update cloudflare tunnel info (render)"
PAT_SENTINEL=ghp_SENTINELSENTINELNOTAREALTOKEN
UPDATED_RE='^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$'

tests_run=0
tests_failed=0
current_test=''
failures=()
sandboxes=()
sandbox=''
origin=''
clone=''
publish_log=''
publish_rc=0
child_pids=()

if [ ! -f "$SUPERVISOR" ]; then
  printf 'ERROR: %s not found - nothing to test\n' "$SUPERVISOR" >&2
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
  if [ "$1" = "$2" ]; then return 0; fi
  fail "$3" "expected [$1], got [$2]"
  return 1
}

assert_contains() { # haystack needle label
  case "$1" in
    *"$2"*) return 0 ;;
  esac
  fail "$3" "expected [$1] to contain [$2]"
  return 1
}

assert_not_contains() { # haystack needle label
  case "$1" in
    *"$2"*)
      fail "$3" "expected [$1] not to contain [$2]"
      return 1
      ;;
  esac
  return 0
}

assert_matches() { # string ere label
  if printf '%s' "$1" | grep -Eq "$2"; then return 0; fi
  fail "$3" "expected [$1] to match /$2/"
  return 1
}

assert_empty() { # actual label
  if [ -z "$1" ]; then return 0; fi
  fail "$2" "expected nothing, got [$1]"
  return 1
}

assert_not_empty() { # actual label
  if [ -n "$1" ]; then return 0; fi
  fail "$2" 'expected a value, got nothing'
  return 1
}

assert_file_bytes() { # expected-without-newline file label
  if printf '%s\n' "$1" | cmp -s - "$2"; then return 0; fi
  fail "$3" "file $2 does not match expected bytes"
  return 1
}

assert_no_file() { # path label
  if [ -e "$1" ]; then
    fail "$2" "$1 exists but should not"
    return 1
  fi
  return 0
}

assert_parses() { # file label
  case "$(json_eval "$1" 'sorted(d.keys())')" in
    PARSE_ERROR* | NOT_AN_OBJECT*)
      fail "$2" "$1 is not a JSON object"
      return 1
      ;;
  esac
  return 0
}

# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------

# json_eval FILE EXPR - print repr() of EXPR evaluated against the parsed
# object `d`, or PARSE_ERROR/... when the file is not a JSON object. repr() so a
# test can tell None, False and "" apart from a real string.
json_eval() {
  python3 -c '
import json, sys
try:
    with open(sys.argv[1], "r", encoding="utf-8") as handle:
        d = json.load(handle)
except (OSError, ValueError) as exc:
    print("PARSE_ERROR:" + type(exc).__name__)
    sys.exit(0)
if not isinstance(d, dict):
    print("NOT_AN_OBJECT")
    sys.exit(0)
print(repr(eval(sys.argv[2])))
' "$1" "$2" 2>/dev/null
}

# json_text FILE EXPR - the string value of EXPR, unquoted, so it can be
# matched against a regex.
json_text() {
  local value
  value=$(json_eval "$1" "$2")
  case "$value" in
    \'*\') value=${value#\'}; value=${value%\'} ;;
  esac
  printf '%s' "$value"
}

free_port() {
  python3 -c '
import socket
sock = socket.socket()
sock.bind(("127.0.0.1", 0))
print(sock.getsockname()[1])
sock.close()
'
}

# pids_matching SUBSTRING - the pids of live processes whose command line
# contains SUBSTRING. pgrep never matches itself; the ps fallback drops its
# own grep.
pids_matching() {
  if command -v pgrep >/dev/null 2>&1; then
    pgrep -f "$1" 2>/dev/null
    return 0
  fi
  ps -Ao pid=,command= 2>/dev/null |
    grep -F "$1" | grep -v 'grep -F' | awk '{print $1}'
}

# --------------------------------------------------------------------------
# fixtures
# --------------------------------------------------------------------------

# new_sandbox - private tmpdir holding a bare `origin` seeded with one commit
# on the push branch, plus a clone of it. Exports the two test seams and the
# clone path, then loads the supervisor against it.
new_sandbox() {
  # The stub knobs are read by wg-quick/ip at call time, so they have to be
  # exported rather than prefixed onto a single command. Clearing them here
  # keeps tests independent of each other's leftovers.
  unset STUB_WG_FAIL STUB_WG_ROUTED STUB_WG_LEAK STUB_WG_KEY STUB_WG_ADDR
  unset STUB_SERVER_SPEAK STUB_SERVER_LINES STUB_SERVER_ARGV_LOG
  sandbox=$(mktemp -d "${TMPDIR:-/tmp}/tcpudp-supervisor-test.XXXXXX")
  # Physical path: ps reports resolved paths, so a relative TMPDIR would not
  # match the stubs' command lines.
  sandbox=$(cd "$sandbox" && pwd -P)
  sandboxes+=("$sandbox")
  origin="$sandbox/origin.git"
  clone="$sandbox/repo"
  mkdir -p "$sandbox/state" "$sandbox/bin" "$sandbox/seed" || return 1

  git init --quiet --bare --initial-branch="$BRANCH" "$origin" || return 1
  (
    cd "$sandbox/seed" || exit 1
    git init --quiet --initial-branch="$BRANCH" . || exit 1
    git config user.name sandbox
    git config user.email sandbox@example.invalid
    printf 'sandbox seed\n' >README.md
    git add README.md
    git commit --quiet -m seed
    git remote add origin "$origin"
    git push --quiet origin "$BRANCH"
  ) >/dev/null 2>&1 || return 1
  git clone --quiet --branch "$BRANCH" "$origin" "$clone" || return 1

  export GITHUB_REMOTE_URL="$origin"
  export GITHUB_PUSH_URL="$origin"
  # The push URL seam points at the bare repo, so the token is never needed -
  # but it must be *set*, otherwise publish takes the no-token path and skips
  # the commit entirely.
  export GITHUB_PAT="$PAT_SENTINEL"
  export TCPUDP_REPO_DIR="$clone"
  export TCPUDP_STATE_DIR="$sandbox/state"
  # Ruling 2: the tests point KEEPALIVE_SCRIPT at the checkout, not at the
  # image path.
  export KEEPALIVE_SCRIPT="$KEEPALIVE_SCRIPT_PATH"
  load_supervisor
}

# load_supervisor - source the script (which must start nothing) and resolve
# the globals against the current sandbox.
load_supervisor() {
  # shellcheck source=/dev/null
  TCPUDP_STATE_DIR="$sandbox/state" \
    TCPUDP_REPO_DIR="$clone" \
    SUPERVISE_INTERVAL=1 \
    TUNNEL_START_TIMEOUT=2 \
    source "$SUPERVISOR" || return 1
  resolve_config
}

origin_commit_count() {
  git -C "$origin" rev-list --count "$BRANCH"
}

origin_subject() {
  git -C "$origin" log --format=%s -1 "$BRANCH"
}

origin_file() { # path-in-repo
  git -C "$origin" show "$BRANCH:$1" 2>/dev/null
}

# history_contains NEEDLE LABEL - the branch's full history mentions NEEDLE.
# Compared with `case` rather than a grep pipeline: `grep -q` under pipefail can
# report failure when the producer takes a SIGPIPE after an early match.
history_contains() {
  local haystack
  haystack=$(git -C "$origin" log -p --format=%s "$BRANCH")
  case "$haystack" in
    *"$1"*) return 0 ;;
  esac
  fail "$2" "the branch history never mentions [$1]"
}

# publish_quiet HOSTNAME FORCE - run publish, keeping its stdout (the
# supervisor logs there by design) in publish_log so the report stays readable.
# Sets publish_rc; dumps publish_log only if the test fails.
publish_quiet() {
  publish_log=$(publish "$1" "$2" 2>&1)
  publish_rc=$?
  return 0
}

# install_stubs - `server` and `cloudflared` executables in the sandbox bin
# dir. Both idle with their own script path in the command line, so the test
# can prove the supervisor reaped them; the cloudflared stub also writes a
# quick-tunnel URL into --logfile, which is what start_tunnel polls for.
install_stubs() {
  cat >"$sandbox/bin/server" <<'STUB'
#!/bin/bash
# Stands in for the release's server binary: holds 127.0.0.1:$TCPUDP_SERVER_PORT
# open so the supervisor's readiness poll succeeds, then idles.
[ -n "${STUB_ORDER_LOG:-}" ] && printf 'server\n' >>"$STUB_ORDER_LOG"
# STUB_SERVER_ARGV_LOG records how the server was actually invoked, so a test can
# assert on the flags the supervisor passes rather than on the source text.
[ -n "${STUB_SERVER_ARGV_LOG:-}" ] && printf '%s\n' "$*" >>"$STUB_SERVER_ARGV_LOG"
# STUB_SERVER_SPEAK makes the stub emit the lines the log-visibility tests look
# for, on both streams, and STUB_SERVER_LINES of them in a burst. Nothing else
# prints, so every other test is unaffected.
if [ -n "${STUB_SERVER_SPEAK:-}" ]; then
  i=1
  while [ "$i" -le "${STUB_SERVER_LINES:-1}" ]; do
    printf 'STUB_SERVER_STDOUT_MARKER_%s\n' "$i"
    printf 'STUB_SERVER_STDERR_MARKER_%s\n' "$i" >&2
    i=$((i + 1))
  done
  # Keep speaking slowly so a long-lived child is judged on a stream that
  # stays open, not just on the initial burst. Backgrounded so the stub still
  # falls through to binding its port below.
  ( while :; do sleep 1; printf 'STUB_SERVER_STDOUT_TICK\n'; done ) &
fi
python3 - "${TCPUDP_SERVER_PORT:-7001}" <<'PY' &
import socket, sys, time
sock = socket.socket()
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(("127.0.0.1", int(sys.argv[1])))
sock.listen(8)
time.sleep(3600)
PY
child=$!
trap 'kill "$child" 2>/dev/null; exit 0' TERM INT
while :; do sleep 1; done
STUB
  cat >"$sandbox/bin/cloudflared" <<'STUB'
#!/bin/bash
# Stands in for cloudflared: accepts `tunnel --url ... --logfile <path>`, logs a
# quick-tunnel URL the way the real binary does, then idles.
log=''
while [ $# -gt 0 ]; do
  case "$1" in
    --logfile) log=${2:-}; shift 2 ;;
    *) shift ;;
  esac
done
[ -n "$log" ] || exit 1
printf '%s INF |  https://%s.trycloudflare.com  |\n' \
  "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
  "${STUB_TUNNEL_HOST:-render-stub.trycloudflare.com}" >>"$log"
while :; do sleep 1; done
STUB
  cat >"$sandbox/bin/wg-quick" <<'STUB'
#!/bin/bash
# Stands in for wg-quick. `up` fails when STUB_WG_FAIL=1, using the real tool's
# wording. STUB_WG_LEAK=1 makes it print a PrivateKey line: a well-behaved
# wg-quick never would, so this is what actually exercises the redaction.
[ -n "${STUB_ORDER_LOG:-}" ] && printf 'wireguard\n' >>"$STUB_ORDER_LOG"
action=${1:-}
cfg=${2:-wg0}
if [ "$action" = up ] && [ "${STUB_WG_FAIL:-0}" = 1 ]; then
  printf 'RTNETLINK answers: Operation not permitted\n' >&2
  exit 1
fi
if [ "$action" = up ]; then
  if [ "${STUB_WG_LEAK:-0}" = 1 ]; then
    printf '[Interface]\nPrivateKey = %s\n' "${STUB_WG_KEY:?}"
  fi
  printf '# %s\n' "$cfg"
fi
exit 0
STUB
  cat >"$sandbox/bin/ip" <<'STUB'
#!/bin/bash
# Stands in for iproute2's `ip`, for the two queries the supervisor makes.
case "${1:-}" in
  -4)
    printf '    inet %s scopeid global\n' "${STUB_WG_ADDR:-10.7.0.2/32}"
    ;;
  route)
    # STUB_WG_ROUTED decides whether the interface carries the default route.
    # This is the distinction that matters: an interface that is up but not in
    # the default route tunnels nothing.
    if [ "${STUB_WG_ROUTED:-0}" = 1 ]; then
      printf 'default via 10.7.0.1 dev wg0 proto static\n'
    else
      printf 'default via 172.17.0.1 dev eth0\n'
    fi
    ;;
esac
exit 0
STUB
  chmod +x "$sandbox/bin/server" "$sandbox/bin/cloudflared" \
    "$sandbox/bin/wg-quick" "$sandbox/bin/ip"
}

# --------------------------------------------------------------------------
# tests
# --------------------------------------------------------------------------

test_resolve_config_defaults() {
  # The brief's own loading snippet, so this pins the defaults rather than the
  # sandbox's overrides.
  unset TCPUDP_RELEASE TCPUDP_SERVER_PORT TCPUDP_REPO_DIR TCPUDP_INFO_DIR \
    TCPUDP_STATE_DIR TCPUDP_SERVER_BIN SUPERVISE_INTERVAL \
    TUNNEL_START_TIMEOUT GITHUB_PAT GITHUB_REPO GITHUB_PUSH_BRANCH PUBLISH \
    PORT KEEPALIVE_SCRIPT GITHUB_REMOTE_URL GITHUB_PUSH_URL 2>/dev/null || true
  # shellcheck source=/dev/null
  SUPERVISE_INTERVAL=1 TUNNEL_START_TIMEOUT=2 source "$SUPERVISOR"
  resolve_config

  assert_eq 7001 "${SERVER_PORT:-}" SERVER_PORT
  assert_eq v1.1.16 "${RELEASE:-}" RELEASE
  assert_eq github_run "${INFO_DIR:-}" INFO_DIR
  assert_eq run "${PUSH_BRANCH:-}" PUSH_BRANCH
  assert_eq 1 "${PUBLISH_ENABLED:-}" PUBLISH_ENABLED
  # The rest of the env -> global mapping, so a Render dashboard setting can
  # never be silently ignored.
  assert_eq xiguichen/tcpudp "${GITHUB_REPO:-}" GITHUB_REPO
  assert_eq /run/tcpudp "${STATE_DIR:-}" STATE_DIR
  assert_eq 5 "${SUPERVISE_INTERVAL:-}" SUPERVISE_INTERVAL
  assert_eq 60 "${TUNNEL_START_TIMEOUT:-}" TUNNEL_START_TIMEOUT
  assert_eq 10000 "${PORT:-}" PORT
  assert_eq /usr/local/bin/keepalive.py "${KEEPALIVE_SCRIPT:-}" KEEPALIVE_SCRIPT
  assert_eq '' "${PAT:-}" PAT
  assert_eq '/app/repo/server' "${SERVER_BIN:-}" SERVER_BIN
}

test_write_state_writes_all_six_keys() {
  new_sandbox || return 1
  write_state 'h.trycloudflare.com' 1
  local file
  file=$(state_file)
  assert_parses "$file" state-file-is-an-object
  assert_eq "['hostname', 'port', 'published', 'source', 'updated', 'wireguard']" \
    "$(json_eval "$file" 'sorted(d.keys())')" six-keys
  assert_eq "'h.trycloudflare.com'" "$(json_eval "$file" 'd["hostname"]')" hostname
  assert_eq '7001' "$(json_eval "$file" 'd["port"]')" port
  assert_eq 'True' "$(json_eval "$file" 'd["published"]')" published
  assert_eq "'render'" "$(json_eval "$file" 'd["source"]')" source
  assert_matches "$(json_text "$file" 'd["updated"]')" "$UPDATED_RE" updated
  # Must be a known token. WG_STATUS is interpolated into this JSON, so an
  # unexpected value must not be able to break the document.
  assert_eq "'skipped'" "$(json_eval "$file" 'd["wireguard"]')" wireguard-default
}

test_write_state_empty_hostname_is_json_null() {
  new_sandbox || return 1
  write_state '' 0
  assert_eq 'None' "$(json_eval "$(state_file)" 'd["hostname"]')" hostname-is-null
  assert_eq 'False' "$(json_eval "$(state_file)" 'd["published"]')" published-false
}

test_write_state_is_atomic() {
  new_sandbox || return 1
  write_state 'h.trycloudflare.com' 1
  assert_no_file "$(state_file).tmp" no-temp-file-left-behind
  # A second write must replace, not append.
  write_state 'h2.trycloudflare.com' 1
  assert_eq "'h2.trycloudflare.com'" \
    "$(json_eval "$(state_file)" 'd["hostname"]')" second-write-replaces
}

test_publish_writes_run_yml_compatible_files() {
  new_sandbox || return 1
  publish_quiet 'h.trycloudflare.com' 1
  assert_eq 0 "$publish_rc" publish-returns-0

  local line='cloudflared access tcp --url tcp://localhost:7001 --hostname h.trycloudflare.com'
  assert_file_bytes "$line" "$REPO_DIR/$INFO_DIR/cloudflare.sh" info-dir-sh
  assert_file_bytes "$line" "$clone/cloudflare.sh" clone-root-sh
  if cmp -s "$REPO_DIR/$INFO_DIR/cloudflare.sh" "$REPO_DIR/$INFO_DIR/cloudflare.bat"; then
    :
  else
    fail info-dir-bat-matches-sh 'the .bat is not byte-identical to the .sh'
  fi
  if cmp -s "$clone/cloudflare.sh" "$clone/cloudflare.bat"; then
    :
  else
    fail clone-root-bat-matches-sh 'the clone-root .bat is not byte-identical to the .sh'
  fi

  local info="$REPO_DIR/$INFO_DIR/run_info.json"
  assert_parses "$info" run_info-parses
  assert_eq "'https://h.trycloudflare.com'" \
    "$(json_eval "$info" 'd["hostname"]')" run_info-hostname
  assert_eq '7001' "$(json_eval "$info" 'd["port"]')" run_info-port
  assert_eq "'render'" "$(json_eval "$info" 'd["source"]')" run_info-source
  assert_matches "$(json_text "$info" 'd["timestamp"]')" "$UPDATED_RE" run_info-timestamp

  assert_parses "$REPO_DIR/$INFO_DIR/render_info.json" render_info-parses
}

test_publish_commits_to_the_push_branch() {
  new_sandbox || return 1
  publish_quiet 'h.trycloudflare.com' 1
  assert_eq 0 "$publish_rc" publish-returns-0
  assert_eq "$COMMIT_SUBJECT" \
    "$(git -C "$clone" log --format=%s -1)" clone-commit-subject
  assert_eq "$COMMIT_SUBJECT" "$(origin_subject)" pushed-commit-subject
  # A Render container has no global git identity, so the clone must carry one.
  assert_not_empty "$(git -C "$clone" config user.email 2>/dev/null)" clone-has-identity
  assert_eq 'True' "$(json_eval "$(state_file)" 'd["published"]')" state-published-true
}

test_publish_skips_when_unchanged_and_not_forced() {
  new_sandbox || return 1
  publish_quiet 'h.trycloudflare.com' 1
  local after_first
  after_first=$(origin_commit_count)
  publish_quiet 'h.trycloudflare.com' 0
  assert_eq 0 "$publish_rc" publish-returns-0
  assert_eq "$after_first" "$(origin_commit_count)" no-new-commit
  # Same content must not even be rewritten on disk.
  assert_eq "$COMMIT_SUBJECT" "$(origin_subject)" subject-unchanged
}

test_publish_commits_when_hostname_changes() {
  new_sandbox || return 1
  local base
  base=$(origin_commit_count)
  publish_quiet 'h1.trycloudflare.com' 1
  publish_quiet 'h2.trycloudflare.com' 0
  assert_eq 0 "$publish_rc" publish-returns-0
  assert_eq "$((base + 2))" "$(origin_commit_count)" two-commits
  assert_contains "$(origin_file github_run/cloudflare.sh)" h2.trycloudflare.com \
    origin-has-new-hostname
  history_contains h1.trycloudflare.com first-hostname-still-in-history
}

test_publish_without_pat_degrades_gracefully() {
  new_sandbox || return 1
  PAT=''
  local base
  base=$(origin_commit_count)
  publish_quiet 'h.trycloudflare.com' 1
  assert_eq 0 "$publish_rc" publish-returns-0
  assert_contains "$(cat "$REPO_DIR/$INFO_DIR/cloudflare.sh")" h.trycloudflare.com \
    info-file-still-written
  assert_eq "$base" "$(origin_commit_count)" no-commit-without-pat
  assert_eq 'False' "$(json_eval "$(state_file)" 'd["published"]')" state-published-false
}

test_publish_rebases_when_branch_moved_on() {
  new_sandbox || return 1
  local base
  base=$(origin_commit_count)
  publish_quiet 'h1.trycloudflare.com' 1

  # Somebody else pushes while we hold the clone.
  local other="$sandbox/other"
  git clone --quiet --branch "$BRANCH" "$origin" "$other" || return 1
  git -C "$other" config user.name other
  git -C "$other" config user.email other@example.invalid
  printf 'other\n' >"$other/other.md"
  git -C "$other" add other.md
  git -C "$other" commit --quiet -m 'concurrent commit'
  git -C "$other" push --quiet origin "$BRANCH" || return 1

  publish_quiet 'h2.trycloudflare.com' 1
  assert_eq 0 "$publish_rc" publish-returns-0
  assert_eq "$((base + 3))" "$(origin_commit_count)" three-commits-nothing-lost
  local subjects
  subjects=$(git -C "$origin" log --format=%s "$BRANCH" | tr '\n' '|')
  assert_contains "$subjects" 'concurrent commit' concurrent-commit-kept
  history_contains h1.trycloudflare.com first-hostname-kept
  history_contains h2.trycloudflare.com second-hostname-kept
  assert_eq "$COMMIT_SUBJECT" "$(origin_subject)" tip-is-our-commit
  assert_contains "$(origin_file github_run/cloudflare.sh)" h2.trycloudflare.com \
    tip-has-newest-hostname
}

test_publish_respects_publish_disabled() {
  new_sandbox || return 1
  PUBLISH_ENABLED=0
  local base
  base=$(origin_commit_count)
  publish_quiet 'h.trycloudflare.com' 1
  assert_eq 0 "$publish_rc" publish-returns-0
  assert_eq "$base" "$(origin_commit_count)" no-commit
  assert_no_file "$REPO_DIR/$INFO_DIR/cloudflare.sh" no-info-files
  assert_no_file "$clone/cloudflare.sh" no-clone-root-files
  assert_no_file "$REPO_DIR/$INFO_DIR/run_info.json" no-run-info
  assert_no_file "$REPO_DIR/$INFO_DIR/render_info.json" no-render-info
}

test_pat_never_appears_in_state_or_repo() {
  new_sandbox || return 1
  RENDER_INFO_URL="file://$sandbox/ipinfo.json"
  printf '{"city":"Testville","region":"Testregion","country":"TS"}\n' \
    >"$sandbox/ipinfo.json"
  publish_quiet 'h.trycloudflare.com' 1
  assert_eq 0 "$publish_rc" publish-returns-0

  assert_empty "$(git -C "$clone" log -p | grep -F "$PAT_SENTINEL")" pat-not-in-history
  assert_empty "$(grep -rlaF "$PAT_SENTINEL" "$clone")" pat-not-in-working-tree
  assert_empty "$(grep -F "$PAT_SENTINEL" "$(state_file)" 2>/dev/null)" pat-not-in-state-file
  assert_empty "$(grep -F "$PAT_SENTINEL" "$REPO_DIR/$INFO_DIR/render_info.json" 2>/dev/null)" \
    pat-not-in-render-info

  # The token only ever appears in the inline URL handed to `git push`: never a
  # remote, never a file. Compared without echoing the value.
  local saved=${GITHUB_PUSH_URL:-}
  unset GITHUB_PUSH_URL
  local url
  url=$(push_url)
  [ -n "$saved" ] && export GITHUB_PUSH_URL="$saved"
  if [ "$url" = "https://x-access-token:$PAT_SENTINEL@github.com/xiguichen/tcpudp.git" ]; then
    :
  else
    fail push-url-is-inline-token 'push_url is not the inline x-access-token URL'
  fi
  assert_empty "$(git -C "$clone" remote -v | grep -F "$PAT_SENTINEL")" pat-not-in-a-remote
}

test_is_alive_detects_dead_and_live_pids() {
  new_sandbox || return 1
  local pidfile="$sandbox/pids" pid

  sleep 30 &
  pid=$!
  printf '%s\n' "$pid" >"$pidfile"
  is_alive "$pidfile"
  assert_eq 0 $? live-pid-is-alive
  kill "$pid" 2>/dev/null
  wait "$pid" 2>/dev/null

  # A reaped pid is guaranteed to be gone.
  sleep 0 &
  pid=$!
  wait "$pid" 2>/dev/null
  printf '%s\n' "$pid" >"$pidfile"
  if is_alive "$pidfile"; then
    fail dead-pid-is-not-alive "pid $pid reported alive"
  else
    :
  fi

  : >"$pidfile"
  if is_alive "$pidfile"; then
    fail empty-pidfile-is-not-alive 'an empty pidfile reported alive'
  else
    :
  fi
  if is_alive "$sandbox/absent.pid"; then
    fail missing-pidfile-is-not-alive 'a missing pidfile reported alive'
  else
    :
  fi
}

test_sigterm_reaps_children_and_exits_zero() {
  new_sandbox || return 1
  install_stubs
  local server_port health_port sup_pid sup_log
  server_port=$(free_port)
  health_port=$(free_port)
  sup_log="$sandbox/supervisor.log"

  TCPUDP_STATE_DIR="$sandbox/state" \
    TCPUDP_REPO_DIR="$clone" \
    TCPUDP_SERVER_BIN="$sandbox/bin/server" \
    TCPUDP_SERVER_PORT="$server_port" \
    TCPUDP_INFO_DIR=github_run \
    TCPUDP_RELEASE=v1.1.16 \
    SUPERVISE_INTERVAL=1 \
    TUNNEL_START_TIMEOUT=5 \
    PUBLISH=false \
    PORT="$health_port" \
    KEEPALIVE_SCRIPT="$KEEPALIVE_SCRIPT_PATH" \
    KEEPALIVE_BIND=127.0.0.1 \
    GITHUB_REMOTE_URL="$origin" \
    GITHUB_PUSH_URL="$origin" \
    GITHUB_PUSH_BRANCH="$BRANCH" \
    STUB_TUNNEL_HOST=render-stub.trycloudflare.com \
    PATH="$sandbox/bin:$PATH" \
    bash "$SUPERVISOR" >"$sup_log" 2>&1 &
  sup_pid=$!
  child_pids+=("$sup_pid")

  local state="$sandbox/state/tunnel.json" tries=0 ready=''
  while [ "$tries" -lt 60 ]; do
    if [ -f "$state" ]; then
      ready=$(json_text "$state" 'd["hostname"]')
      [ -n "$ready" ] && break
    fi
    if ! kill -0 "$sup_pid" 2>/dev/null; then break; fi
    sleep 0.5
    tries=$((tries + 1))
  done
  if [ "$ready" != 'render-stub.trycloudflare.com' ]; then
    fail supervisor-reached-a-tunnel-hostname \
      "state=$(cat "$state" 2>/dev/null) log=$(tail -n 5 "$sup_log" 2>/dev/null | tr '\n' '|')"
    kill -TERM "$sup_pid" 2>/dev/null
    wait "$sup_pid" 2>/dev/null
    return 1
  fi

  local server_pid tunnel_pid keepalive_pid
  server_pid=$(cat "$sandbox/state/server.pid" 2>/dev/null)
  tunnel_pid=$(cat "$sandbox/state/tunnel.pid" 2>/dev/null)
  keepalive_pid=$(cat "$sandbox/state/keepalive.pid" 2>/dev/null)
  assert_not_empty "$server_pid" server-pidfile-written
  assert_not_empty "$tunnel_pid" tunnel-pidfile-written
  assert_not_empty "$keepalive_pid" keepalive-pidfile-written

  kill -TERM "$sup_pid"
  wait "$sup_pid"
  assert_eq 0 $? sigterm-exit-status-is-zero

  local pid
  for pid in "$server_pid" "$tunnel_pid" "$keepalive_pid"; do
    local gone=0 tries2=0
    while [ "$tries2" -lt 20 ]; do
      if ! kill -0 "$pid" 2>/dev/null; then
        gone=1
        break
      fi
      sleep 0.25
      tries2=$((tries2 + 1))
    done
    [ "$gone" = 1 ] || fail child-reaped "pid $pid still running after SIGTERM"
  done

  for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    [ -z "$(pids_matching "$sandbox/bin/server")" ] &&
      [ -z "$(pids_matching "$sandbox/bin/cloudflared")" ] && break
    sleep 0.25
  done
  assert_empty "$(pids_matching "$sandbox/bin/server")" no-stub-server-process
  assert_empty "$(pids_matching "$sandbox/bin/cloudflared")" no-stub-cloudflared-process
  assert_empty "$(pids_matching "$KEEPALIVE_SCRIPT_PATH")" no-keepalive-process
}

# --------------------------------------------------------------------------
# runner
# --------------------------------------------------------------------------

run_test() {
  current_test=$1
  tests_run=$((tests_run + 1))
  local before=${#failures[@]}
  publish_log=''
  "$2" || true
  if [ "${#failures[@]}" -gt "$before" ]; then
    tests_failed=$((tests_failed + 1))
    printf 'FAIL  %s\n' "$1"
    if [ -n "$publish_log" ]; then
      printf '       publish said: %s\n' "$(printf '%s' "$publish_log" | tr '\n' '|')"
    fi
  else
    printf 'ok    %s\n' "$1"
  fi
}

# teardown - named teardown, not cleanup: sourcing render_supervisor.sh
# deliberately defines a cleanup() of its own, and an EXIT trap pointing at the
# wrong one would call exit 0 and swallow the suite's exit status.
teardown() {
  local dir pid
  for pid in "${child_pids[@]:-}"; do
    [ -n "$pid" ] && kill -TERM "$pid" 2>/dev/null
  done
  for dir in "${sandboxes[@]:-}"; do
    [ -n "$dir" ] || continue
    pkill -f "$dir/bin/server" 2>/dev/null
    pkill -f "$dir/bin/cloudflared" 2>/dev/null
    rm -rf "$dir"
  done
  return 0
}

# The Dockerfile and preflight are edited in different places, and they drift
# apart silently. A tool that preflight treats as fatal but the image does not
# install kills the container on the first boot with nothing but
# "preflight failed" - which is exactly how the missing git package showed up
# in production. Read the fatal list out of preflight rather than restating it
# here, so this test cannot itself go stale.
# A wg0.conf with a recognisable private key, so the leak tests have something
# to look for. The value is syntactically a WireGuard key (44 chars, base64)
# but it is not a real key and grants nothing.
WG_KEY='qJ0vQ1J8kZ3xW7pL2mN9cR4tY6uB8eH0dF1gI3jK5lM7nO9pQ2rS4tU6vW8xY0zA='
WG_SAMPLE_CONFIG="[Interface]
PrivateKey = $WG_KEY
Address = 10.7.0.2/32

[Peer]
PublicKey = TruncatedForTestsOnly0000000000000000000000=
Endpoint = vpn.example.invalid:51820
AllowedIPs = 0.0.0.0/0"

# wg_quiet - run start_wireguard in the current shell (so WG_STATUS survives)
# with its log captured to $sandbox/wg.log. Sets wg_rc.
wg_quiet() {
  wg_rc=0
  PATH="$sandbox/bin:$PATH" start_wireguard >"$sandbox/wg.log" 2>&1 || wg_rc=$?
  return 0
}

# No config: skip cleanly, report skipped, and never invoke wg-quick.
test_wireguard_is_skipped_when_config_is_unset() {
  new_sandbox || return 1
  install_stubs
  WIREGUARD_CONFIG='' resolve_config
  wg_quiet
  assert_eq 0 "$wg_rc" skip-returns-0
  assert_eq 'skipped' "$WG_STATUS" status-is-skipped
  assert_contains "$(cat "$sandbox/wg.log")" 'not set' logs-that-it-skipped
  if grep -q 'wg-quick' "$sandbox/wg.log" 2>/dev/null; then
    fail 'wg-quick-must-not-run' 'wg-quick ran even though no config was set'
  fi
}

# A failed wg-quick must not take the service down by default: /healthz has to
# stay reachable for the failure to be diagnosable from outside.
test_wireguard_failure_is_not_fatal_by_default() {
  new_sandbox || return 1
  install_stubs
  export STUB_WG_FAIL=1
  WIREGUARD_CONFIG="$WG_SAMPLE_CONFIG" \
    WIREGUARD_CONFIG_PATH="$sandbox/wg0.conf" resolve_config
  wg_quiet
  assert_eq 1 "$wg_rc" failure-returns-1
  assert_eq 'failed' "$WG_STATUS" status-is-failed
  local log
  log=$(cat "$sandbox/wg.log")
  assert_contains "$log" 'RTNETLINK' surfaces-the-underlying-error
  assert_contains "$log" 'NOT tunnelled' warns-that-egress-is-not-tunnelled
}

# ...unless the operator explicitly asked for it to be mandatory.
test_wireguard_failure_is_fatal_when_required() {
  new_sandbox || return 1
  install_stubs
  (
    export STUB_WG_FAIL=1
    WIREGUARD_CONFIG="$WG_SAMPLE_CONFIG" \
      WIREGUARD_CONFIG_PATH="$sandbox/wg0.conf" WIREGUARD_REQUIRED=true \
      PATH="$sandbox/bin:$PATH" \
      bash -c 'source "$1"; resolve_config; start_wireguard' _ "$SUPERVISOR"
  ) >"$sandbox/wg.log" 2>&1
  local rc=$?
  local log
  log=$(cat "$sandbox/wg.log")

  # The exit code alone cannot tell die() apart from a plain `return 1` as the
  # last statement of bash -c, so assert the message only die() produces, and
  # assert the continue-anyway warning is absent. Together they pin the behaviour.
  assert_contains "$log" 'WireGuard is required but wg-quick failed' explains-why-it-stopped
  if printf '%s' "$log" | grep -q 'continuing without WireGuard'; then
    fail required-stops-dont-continue \
      'it both announced it was required and carried on anyway'
  fi
  if [ "$rc" -ne 0 ]; then
    :
  else
    fail 'required-failure-must-exit-nonzero' \
      'WIREGUARD_REQUIRED=true did not stop the supervisor'
  fi
}

# The happy path, and the case that actually matters: an interface that came up
# but is not in the default route has tunnelled nothing.
test_wireguard_up_and_carrying_the_default_route() {
  new_sandbox || return 1
  install_stubs
  export STUB_WG_ROUTED=1
  WIREGUARD_CONFIG="$WG_SAMPLE_CONFIG" \
    WIREGUARD_CONFIG_PATH="$sandbox/wg0.conf" resolve_config
  wg_quiet
  assert_eq 0 "$wg_rc" success-returns-0
  assert_eq 'up' "$WG_STATUS" status-is-up
  local log
  log=$(cat "$sandbox/wg.log")
  assert_contains "$log" 'egress IS tunnelled' confirms-the-route
  assert_contains "$log" '10.7.0.2/32' reports-the-address
}

test_wireguard_up_but_not_routed_is_flagged() {
  new_sandbox || return 1
  install_stubs
  export STUB_WG_ROUTED=0
  WIREGUARD_CONFIG="$WG_SAMPLE_CONFIG" \
    WIREGUARD_CONFIG_PATH="$sandbox/wg0.conf" resolve_config
  wg_quiet
  assert_eq 0 "$wg_rc" interface-still-came-up
  # The distinct status is the point: /healthz must distinguish "tunnelled" from
  # "interface exists but carries no traffic".
  assert_eq 'up-not-routed' "$WG_STATUS" status-distinguishes-routing
  local log
  log=$(cat "$sandbox/wg.log")
  assert_contains "$log" 'does NOT traverse' warns-about-the-route
  assert_contains "$log" 'AllowedIPs' explains-the-likely-cause
}

# A private key must not reach the log, the state file, or the repo that
# publish() pushes - even when the tool handling it prints one.
test_wireguard_private_key_never_leaks() {
  new_sandbox || return 1
  install_stubs
  export STUB_WG_ROUTED=1 STUB_WG_LEAK=1 STUB_WG_KEY="$WG_KEY"
  WIREGUARD_CONFIG="$WG_SAMPLE_CONFIG" \
    WIREGUARD_CONFIG_PATH="$sandbox/wg0.conf" resolve_config
  wg_quiet
  write_state 'h.trycloudflare.com' 1

  local log
  log=$(cat "$sandbox/wg.log")
  if printf '%s' "$log" | grep -qF "$WG_KEY"; then
    fail 'key-not-in-log' 'the private key appeared in the supervisor log'
  fi
  assert_contains "$log" '<redacted>' output-is-redacted
  if grep -rqF "$WG_KEY" "$STATE_DIR" 2>/dev/null; then
    fail 'key-not-in-state' 'the private key appeared in the state directory'
  fi
  if grep -rqF "$WG_KEY" "$clone" 2>/dev/null; then
    fail 'key-not-in-repo' 'the private key appeared inside the pushed repo'
  fi
  # It must still have been written, 0600, or nothing was achieved.
  assert_file_contains "$sandbox/wg0.conf" "$WG_KEY" config-was-written
  local mode
  mode=$(ls -l "$sandbox/wg0.conf" | cut -c1-10)
  assert_eq '-rw-------' "$mode" config-is-0600
}

# WireGuard must come up before the server and the tunnel. This is not a
# stylistic preference: wg-quick installs routing as it brings the interface
# up, so anything already running has already sent its traffic on the
# container's ordinary egress. The result would look like success in every log
# line except the one that matters.
test_wireguard_comes_up_before_the_server() {
  new_sandbox || return 1
  install_stubs
  local server_port health_port sup_pid sup_log order
  server_port=$(free_port)
  health_port=$(free_port)
  sup_log="$sandbox/supervisor.log"
  order="$sandbox/order.log"
  : >"$order"

  TCPUDP_STATE_DIR="$sandbox/state" \
    TCPUDP_REPO_DIR="$clone" \
    TCPUDP_SERVER_BIN="$sandbox/bin/server" \
    TCPUDP_SERVER_PORT="$server_port" \
    TCPUDP_INFO_DIR=github_run \
    TCPUDP_RELEASE=v1.1.16 \
    SUPERVISE_INTERVAL=1 \
    TUNNEL_START_TIMEOUT=5 \
    PUBLISH=false \
    PORT="$health_port" \
    KEEPALIVE_SCRIPT="$KEEPALIVE_SCRIPT_PATH" \
    KEEPALIVE_BIND=127.0.0.1 \
    GITHUB_REMOTE_URL="$origin" \
    GITHUB_PUSH_URL="$origin" \
    GITHUB_PUSH_BRANCH="$BRANCH" \
    STUB_TUNNEL_HOST=render-stub.trycloudflare.com \
    STUB_WG_ROUTED=1 \
    STUB_ORDER_LOG="$order" \
    WIREGUARD_CONFIG="$WG_SAMPLE_CONFIG" \
    WIREGUARD_CONFIG_PATH="$sandbox/wg0.conf" \
    PATH="$sandbox/bin:$PATH" \
    bash "$SUPERVISOR" >"$sup_log" 2>&1 &
  sup_pid=$!
  child_pids+=("$sup_pid")

  # Wait for both markers instead of sleeping a fixed amount: supervise_loop
  # restarts the server, so a fixed wait would read later lines.
  local tries=0
  while [ "$tries" -lt 60 ]; do
    if grep -q '^wireguard$' "$order" 2>/dev/null &&
      grep -q '^server$' "$order" 2>/dev/null; then
      break
    fi
    kill -0 "$sup_pid" 2>/dev/null || break
    sleep 0.5
    tries=$((tries + 1))
  done

  kill -TERM "$sup_pid" 2>/dev/null
  wait "$sup_pid" 2>/dev/null

  local wg_line server_line
  wg_line=$(grep -n '^wireguard$' "$order" 2>/dev/null | head -1 | cut -d: -f1)
  server_line=$(grep -n '^server$' "$order" 2>/dev/null | head -1 | cut -d: -f1)
  assert_not_empty "$wg_line" wireguard-was-invoked
  assert_not_empty "$server_line" server-was-started
  if [ -z "$wg_line" ] || [ -z "$server_line" ]; then
    fail ordering-could-be-checked \
      "order.log=[$(tr '\n' '|' <"$order" 2>/dev/null)] log=[$(tail -n 5 "$sup_log" 2>/dev/null | tr '\n' '|')]"
    return 1
  fi
  if [ "$wg_line" -lt "$server_line" ]; then
    :
  else
    fail wireguard-precedes-server \
      "wg-quick ran at line $wg_line but the server started at line $server_line"
  fi
}

# The capability check is the one diagnostic that predicts the outcome, so it
# gets tested against known capability sets rather than left to whatever host
# the suite happens to run on. 0x00000000200425fb is Docker's *default* set.
test_wireguard_reports_capabilities_honestly() {
  new_sandbox || return 1
  install_stubs
  local proc status i
  for i in 1 2; do
    case "$i" in
      # Docker default: CAP_NET_ADMIN (bit 12) is NOT set.
      1) capeff='00000000200425fb' expect='missing' ;;
      # The same set with bit 12 added.
      2) capeff='00000000200435fb' expect='held' ;;
    esac
    proc="$sandbox/status.$i"
    printf 'Name:\tsupervisor\nCapEff:\t%s\n' "$capeff" >"$proc"
    WIREGUARD_PROC_STATUS="$proc" WIREGUARD_CONFIG="$WG_SAMPLE_CONFIG" \
      WIREGUARD_CONFIG_PATH="$sandbox/wg0.conf" resolve_config
    wg_quiet
    local log
    log=$(cat "$sandbox/wg.log")
    assert_contains "$log" "CAP_NET_ADMIN=$expect" "capability-$expect-reported"

    # Only the blocked case should predict the failure; claiming a block that
    # does not exist would send the operator chasing the wrong cause.
    if [ "$expect" = missing ]; then
      assert_contains "$log" 'a TUN device cannot be created' blocked-case-explains-why
    elif printf '%s' "$log" | grep -q 'a TUN device cannot be created'; then
      fail unblocked-case-makes-no-block-claim \
        'told the operator the host is blocked when it holds CAP_NET_ADMIN'
    fi
  done

  # No /proc at all: report 'unknown' rather than claiming a capability we
  # never measured.
  WIREGUARD_CONFIG="$WG_SAMPLE_CONFIG" \
    WIREGUARD_CONFIG_PATH="$sandbox/wg0.conf" \
    WIREGUARD_PROC_STATUS="$sandbox/absent" resolve_config
  wg_quiet
  local log
  log=$(cat "$sandbox/wg.log")
  assert_contains "$log" 'CAP_NET_ADMIN=unknown' unreadable-proc-reports-unknown
  if printf '%s' "$log" | grep -q 'a TUN device cannot be created'; then
    fail unknown-must-not-claim-a-block \
      'made a blocking claim without having measured the capability'
  fi
}

test_dockerfile_provides_every_required_command() {
  local required providers missing=''

  required=$(sed -n 's/^  for tool in \(.*\); do$/\1/p' "$SUPERVISOR")
  assert_contains "$required" 'git' 'preflight-still-lists-git'

  # Everything the image puts on PATH: the apt package names, plus cloudflared,
  # which is downloaded from GitHub releases rather than installed. The final
  # tr flattens the list to one space-separated line, because the membership
  # test below matches on " word " boundaries.
  providers=$(awk '/apt-get install/,/rm -rf \/var\/lib\/apt/' "$DOCKERFILE" |
    tr -d '\\' | tr ' \t' '\n\n' | grep -E '^[a-z0-9.+-]+$' | tr '\n' ' ')
  providers="$providers cloudflared"

  local tool
  for tool in $required; do
    case " $providers " in
      *" $tool "*) ;;
      *) missing="$missing $tool" ;;
    esac
  done
  assert_eq '' "$missing" 'every-fatal-tool-is-present-in-the-image'

  # wg-quick cannot be joined to the loop above: it is a warn-only preflight
  # check, and the package providing it is named differently from the tool.
  # Checked against $providers (the extracted apt list), never against the file:
  # a comment naming the package would otherwise satisfy this and the image
  # would still lack it - the same class of bug as the missing `git` that killed
  # the container at preflight.
  if grep -q 'wg-quick up' "$SUPERVISOR"; then
    case " $providers " in
      *" wireguard-tools "*) ;;
      *)
        fail 'the-image-ships-wireguard-tools' \
          'the supervisor runs wg-quick but the apt install list has no wireguard-tools'
        ;;
    esac
  fi
}

trap teardown EXIT

run_test test_resolve_config_defaults test_resolve_config_defaults
run_test test_write_state_writes_all_six_keys test_write_state_writes_all_six_keys
run_test test_write_state_empty_hostname_is_json_null test_write_state_empty_hostname_is_json_null
run_test test_write_state_is_atomic test_write_state_is_atomic
run_test test_publish_writes_run_yml_compatible_files test_publish_writes_run_yml_compatible_files
run_test test_publish_commits_to_the_push_branch test_publish_commits_to_the_push_branch
run_test test_publish_skips_when_unchanged_and_not_forced test_publish_skips_when_unchanged_and_not_forced
run_test test_publish_commits_when_hostname_changes test_publish_commits_when_hostname_changes
run_test test_publish_without_pat_degrades_gracefully test_publish_without_pat_degrades_gracefully
run_test test_publish_rebases_when_branch_moved_on test_publish_rebases_when_branch_moved_on
run_test test_publish_respects_publish_disabled test_publish_respects_publish_disabled
run_test test_pat_never_appears_in_state_or_repo test_pat_never_appears_in_state_or_repo
run_test test_is_alive_detects_dead_and_live_pids test_is_alive_detects_dead_and_live_pids
test_server_output_reaches_the_supervisor_log() {
  new_sandbox || return 1
  install_stubs
  local server_port health_port sup_pid sup_log
  server_port=$(free_port)
  health_port=$(free_port)
  sup_log="$sandbox/supervisor.log"

  STUB_SERVER_SPEAK=1 STUB_SERVER_LINES=3 \
    TCPUDP_STATE_DIR="$sandbox/state" \
    TCPUDP_REPO_DIR="$clone" \
    TCPUDP_SERVER_BIN="$sandbox/bin/server" \
    TCPUDP_SERVER_PORT="$server_port" \
    TCPUDP_INFO_DIR=github_run \
    TCPUDP_RELEASE=v1.1.16 \
    SUPERVISE_INTERVAL=1 \
    TUNNEL_START_TIMEOUT=5 \
    PUBLISH=false \
    PORT="$health_port" \
    KEEPALIVE_SCRIPT="$KEEPALIVE_SCRIPT_PATH" \
    KEEPALIVE_BIND=127.0.0.1 \
    GITHUB_REMOTE_URL="$origin" \
    GITHUB_PUSH_URL="$origin" \
    GITHUB_PUSH_BRANCH="$BRANCH" \
    STUB_TUNNEL_HOST=render-stub.trycloudflare.com \
    PATH="$sandbox/bin:$PATH" \
    bash "$SUPERVISOR" >"$sup_log" 2>&1 &
  sup_pid=$!
  child_pids+=("$sup_pid")

  local state="$sandbox/state/tunnel.json" tries=0
  while [ "$tries" -lt 60 ]; do
    [ -f "$state" ] && break
    if ! kill -0 "$sup_pid" 2>/dev/null; then break; fi
    sleep 0.5
    tries=$((tries + 1))
  done

  kill -TERM "$sup_pid" 2>/dev/null
  wait "$sup_pid" 2>/dev/null

  local whole
  whole=$(cat "$sup_log" 2>/dev/null)

  # Both streams. The server writes its own diagnostics to stdout AND stderr;
  # if either is invisible the whole point of this change is lost.
  assert_contains "$whole" 'server: STUB_SERVER_STDOUT_MARKER_1' \
    server-stdout-reaches-the-render-log
  assert_contains "$whole" 'server: STUB_SERVER_STDERR_MARKER_1' \
    server-stderr-reaches-the-render-log

  # Prefixed, so a server line is never mistaken for a supervisor line and the
  # two sources can be told apart when reading the dashboard.
  assert_matches "$whole" '[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z server: ' \
    server-lines-are-prefixed-and-identifiable

  # server.log is still written, so the existing "exited immediately; see
  # $REPO_DIR/server.log" advice stays true and the file survives for later.
  local onfile
  onfile=$(cat "$clone/server.log" 2>/dev/null)
  assert_contains "$onfile" 'STUB_SERVER_STDOUT_MARKER_1' server-log-file-still-written
  assert_contains "$onfile" 'STUB_SERVER_STDERR_MARKER_1' server-log-file-also-has-stderr
}

test_server_log_burst_is_not_corrupted() {
  new_sandbox || return 1
  install_stubs
  local server_port health_port sup_pid sup_log
  server_port=$(free_port)
  health_port=$(free_port)
  sup_log="$sandbox/supervisor.log"

  STUB_SERVER_SPEAK=1 STUB_SERVER_LINES=40 \
    TCPUDP_STATE_DIR="$sandbox/state" \
    TCPUDP_REPO_DIR="$clone" \
    TCPUDP_SERVER_BIN="$sandbox/bin/server" \
    TCPUDP_SERVER_PORT="$server_port" \
    TCPUDP_INFO_DIR=github_run \
    TCPUDP_RELEASE=v1.1.16 \
    SUPERVISE_INTERVAL=1 \
    TUNNEL_START_TIMEOUT=5 \
    PUBLISH=false \
    PORT="$health_port" \
    KEEPALIVE_SCRIPT="$KEEPALIVE_SCRIPT_PATH" \
    KEEPALIVE_BIND=127.0.0.1 \
    GITHUB_REMOTE_URL="$origin" \
    GITHUB_PUSH_URL="$origin" \
    GITHUB_PUSH_BRANCH="$BRANCH" \
    STUB_TUNNEL_HOST=render-stub.trycloudflare.com \
    PATH="$sandbox/bin:$PATH" \
    bash "$SUPERVISOR" >"$sup_log" 2>&1 &
  sup_pid=$!
  child_pids+=("$sup_pid")

  local state="$sandbox/state/tunnel.json" tries=0
  while [ "$tries" -lt 60 ]; do
    [ -f "$state" ] && break
    if ! kill -0 "$sup_pid" 2>/dev/null; then break; fi
    sleep 0.5
    tries=$((tries + 1))
  done

  kill -TERM "$sup_pid" 2>/dev/null
  wait "$sup_pid" 2>/dev/null

  # Every line of a 40-line burst must arrive whole. A pump that reads with a
  # short buffer, or that lets the server's stdout and stderr interleave
  # mid-line, would merge or truncate records - and a merged diagnostic is
  # worse than a missing one, because it reads as real content.
  local whole missing=0 i
  whole=$(cat "$sup_log" 2>/dev/null)
  for ((i = 1; i <= 40; i++)); do
    if ! printf '%s' "$whole" | grep -q "server: STUB_SERVER_STDOUT_MARKER_${i}\$"; then
      missing=$((missing + 1))
    fi
  done
  assert_eq 0 "$missing" every-line-of-the-burst-arrived-intact

  local stderr_missing=0
  for ((i = 1; i <= 40; i++)); do
    if ! printf '%s' "$whole" | grep -q "server: STUB_SERVER_STDERR_MARKER_${i}\$"; then
      stderr_missing=$((stderr_missing + 1))
    fi
  done
  assert_eq 0 "$stderr_missing" every-stderr-line-arrived-intact
}

run_test test_sigterm_reaps_children_and_exits_zero test_sigterm_reaps_children_and_exits_zero
run_test test_wireguard_is_skipped_when_config_is_unset test_wireguard_is_skipped_when_config_is_unset
run_test test_wireguard_failure_is_not_fatal_by_default test_wireguard_failure_is_not_fatal_by_default
run_test test_wireguard_failure_is_fatal_when_required test_wireguard_failure_is_fatal_when_required
run_test test_wireguard_up_and_carrying_the_default_route test_wireguard_up_and_carrying_the_default_route
run_test test_wireguard_up_but_not_routed_is_flagged test_wireguard_up_but_not_routed_is_flagged
run_test test_wireguard_private_key_never_leaks test_wireguard_private_key_never_leaks
run_test test_wireguard_comes_up_before_the_server test_wireguard_comes_up_before_the_server
run_test test_wireguard_reports_capabilities_honestly test_wireguard_reports_capabilities_honestly
run_test test_dockerfile_provides_every_required_command test_dockerfile_provides_every_required_command
test_server_is_aimed_at_the_wireguard_listen_port() {
  new_sandbox || return 1
  install_stubs
  local server_port health_port sup_pid sup_log argv_log
  server_port=$(free_port)
  health_port=$(free_port)
  sup_log="$sandbox/supervisor.log"
  argv_log="$sandbox/server-argv.log"

  # A WireGuard config whose ListenPort is deliberately not 7001, so a default
  # cannot accidentally satisfy the assertion.
  local wgconf
  wgconf=$'[Interface]\nPrivateKey = STUB_WG_KEY\nListenPort = 51899\n\n[Peer]\nPublicKey = peerkey\nAllowedIPs = 10.66.66.3/32\n'

  STUB_SERVER_ARGV_LOG="$argv_log" \
    STUB_WG_KEY=stubkey \
    WIREGUARD_CONFIG="$wgconf" \
    TCPUDP_STATE_DIR="$sandbox/state" \
    TCPUDP_REPO_DIR="$clone" \
    TCPUDP_SERVER_BIN="$sandbox/bin/server" \
    TCPUDP_SERVER_PORT="$server_port" \
    TCPUDP_INFO_DIR=github_run \
    TCPUDP_RELEASE=v1.1.16 \
    SUPERVISE_INTERVAL=1 \
    TUNNEL_START_TIMEOUT=5 \
    PUBLISH=false \
    PORT="$health_port" \
    KEEPALIVE_SCRIPT="$KEEPALIVE_SCRIPT_PATH" \
    KEEPALIVE_BIND=127.0.0.1 \
    GITHUB_REMOTE_URL="$origin" \
    GITHUB_PUSH_URL="$origin" \
    GITHUB_PUSH_BRANCH="$BRANCH" \
    STUB_TUNNEL_HOST=render-stub.trycloudflare.com \
    PATH="$sandbox/bin:$PATH" \
    bash "$SUPERVISOR" >"$sup_log" 2>&1 &
  sup_pid=$!
  child_pids+=("$sup_pid")

  local state="$sandbox/state/tunnel.json" tries=0
  while [ "$tries" -lt 60 ]; do
    [ -f "$state" ] && break
    if ! kill -0 "$sup_pid" 2>/dev/null; then break; fi
    sleep 0.5
    tries=$((tries + 1))
  done
  kill -TERM "$sup_pid" 2>/dev/null
  wait "$sup_pid" 2>/dev/null

  local argv
  argv=$(cat "$argv_log" 2>/dev/null)
  assert_not_empty "$argv" the-server-was-invoked
  # The whole point: the server relays the virtual channel out over UDP, and
  # the only thing listening on that UDP port is the WireGuard interface. Aiming
  # it at the default 7001 sends every packet nowhere.
  assert_contains "$argv" '--udp-target-port=51899' \
    the-server-is-aimed-at-the-wireguard-listen-port
  assert_not_contains "$argv" '--udp-target-port=7001' \
    the-server-is-not-aimed-at-its-own-default
}

test_explicit_udp_target_port_overrides_the_wireguard_config() {
  new_sandbox || return 1
  install_stubs
  local server_port health_port sup_pid sup_log argv_log
  server_port=$(free_port)
  health_port=$(free_port)
  sup_log="$sandbox/supervisor.log"
  argv_log="$sandbox/server-argv.log"

  local wgconf
  wgconf=$'[Interface]\nPrivateKey = STUB_WG_KEY\nListenPort = 51899\n\n[Peer]\nPublicKey = peerkey\n'

  STUB_SERVER_ARGV_LOG="$argv_log" \
    STUB_WG_KEY=stubkey \
    WIREGUARD_CONFIG="$wgconf" \
    TCPUDP_UDP_TARGET_PORT=41777 \
    TCPUDP_STATE_DIR="$sandbox/state" \
    TCPUDP_REPO_DIR="$clone" \
    TCPUDP_SERVER_BIN="$sandbox/bin/server" \
    TCPUDP_SERVER_PORT="$server_port" \
    TCPUDP_INFO_DIR=github_run \
    TCPUDP_RELEASE=v1.1.16 \
    SUPERVISE_INTERVAL=1 \
    TUNNEL_START_TIMEOUT=5 \
    PUBLISH=false \
    PORT="$health_port" \
    KEEPALIVE_SCRIPT="$KEEPALIVE_SCRIPT_PATH" \
    KEEPALIVE_BIND=127.0.0.1 \
    GITHUB_REMOTE_URL="$origin" \
    GITHUB_PUSH_URL="$origin" \
    GITHUB_PUSH_BRANCH="$BRANCH" \
    STUB_TUNNEL_HOST=render-stub.trycloudflare.com \
    PATH="$sandbox/bin:$PATH" \
    bash "$SUPERVISOR" >"$sup_log" 2>&1 &
  sup_pid=$!
  child_pids+=("$sup_pid")

  local state="$sandbox/state/tunnel.json" tries=0
  while [ "$tries" -lt 60 ]; do
    [ -f "$state" ] && break
    if ! kill -0 "$sup_pid" 2>/dev/null; then break; fi
    sleep 0.5
    tries=$((tries + 1))
  done
  kill -TERM "$sup_pid" 2>/dev/null
  wait "$sup_pid" 2>/dev/null

  local argv
  argv=$(cat "$argv_log" 2>/dev/null)
  assert_contains "$argv" '--udp-target-port=41777' \
    an-explicit-udp-target-port-wins-over-the-config
  assert_not_contains "$argv" '--udp-target-port=51899' \
    the-config-listen-port-is-not-also-passed
}

run_test test_server_output_reaches_the_supervisor_log test_server_output_reaches_the_supervisor_log
run_test test_server_log_burst_is_not_corrupted test_server_log_burst_is_not_corrupted
run_test test_server_is_aimed_at_the_wireguard_listen_port test_server_is_aimed_at_the_wireguard_listen_port
run_test test_explicit_udp_target_port_overrides_the_wireguard_config test_explicit_udp_target_port_overrides_the_wireguard_config

printf '\n%s tests, %s failed\n' "$tests_run" "$tests_failed"
if [ "$tests_failed" -gt 0 ]; then
  printf 'failures:\n'
  for failure in "${failures[@]}"; do printf '  %s\n' "$failure"; done
  exit 1
fi
exit 0
