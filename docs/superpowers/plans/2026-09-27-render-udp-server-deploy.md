# Render UDP Server Deployment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a self-contained `render/` folder that runs the `tcpudp` server on Render behind a Cloudflare Quick Tunnel and publishes the tunnel hostname to the `run` branch, so `./render/trigger_render.sh && ./run_github.sh` yields a working client connection.

**Architecture:** A Render *web* service (free plan, `rootDir: render`) runs `render_supervisor.sh` as PID 1. The supervisor owns three children — the `server` binary, a stdlib `keepalive.py` HTTP endpoint on `$PORT`, and `cloudflared` — and publishes the discovered `trycloudflare.com` hostname by committing to the `run` branch. `GET /healthz` reports the *live* hostname, which is what lets the free tier survive spin-downs. A Mac-side `trigger_render.sh` wakes the instance, reconciles the local checkout against the live hostname, and hands off to the existing, unmodified `run_github.sh`.

**Tech Stack:** bash 5, Python 3 stdlib only (`unittest`, `http.server`, `json`), `git`, `curl`, `cloudflared`, Render Blueprint YAML, Docker (`ubuntu:24.04`).

**Spec:** `docs/superpowers/specs/2026-09-27-render-udp-server-deploy-design.md` — the plan argues from the spec, so the spec travels with it; executors read both.

## Global Constraints

- **Nothing outside `render/` is created, modified, or deleted.** `run.yml`, `trigger_run.sh`, `trigger_vpn2.sh`, `run_github.sh`, `run/config.json` and `udp_client` must be byte-identical after this work.
- Every repo-specific value is an env var with a working default. Defaults, copied verbatim from the spec's configuration table:
  `GITHUB_REPO=xiguichen/tcpudp`, `GITHUB_PUSH_BRANCH=run`, `GITHUB_PAT=` (unset),
  `TCPUDP_RELEASE=v1.1.16`, `TCPUDP_SERVER_PORT=7001`, `TCPUDP_REPO_DIR=/app/repo`,
  `TCPUDP_INFO_DIR=github_run`, `SUPERVISE_INTERVAL=5`, `TUNNEL_START_TIMEOUT=60`,
  `PUBLISH=true`, `RENDER_HEALTH_URL=https://tcpudp-render.onrender.com/healthz`.
- Two extra env vars exist purely as test seams and default to the GitHub URLs: `GITHUB_REMOTE_URL` (default `https://github.com/$GITHUB_REPO.git`) and `GITHUB_PUSH_URL` (default `https://x-access-token:$GITHUB_PAT@github.com/$GITHUB_REPO.git`).
- Python is **stdlib only**. No `pip install`, no `PyYAML`, no `requests`, no `pytest`. Tests use `unittest`.
- Shell is `bash`, and every script must pass `bash -n`. Scripts are `source`-able without executing `main`, via `if [ "${BASH_SOURCE[0]}" = "$0" ]; then main "$@"; fi` as the last line.
- The `GITHUB_PAT` is never written to a file, never passed to `git remote add`, and never echoed. All git pushes use an inline URL passed straight to `git push`.
- `render.yaml` sets `autoDeployTrigger: 'off'`, `rootDir: render`, `region: oregon`. These three are load-bearing; see the spec's *Gotchas*.
- Commits created by the supervisor must have `Auto-update` in the subject so existing commit-polling logic recognises them: exactly `Auto-update cloudflare tunnel info (render)`.
- Commit after every task.

## File Structure

| File | Responsibility |
|---|---|
| `render/keepalive.py` | HTTP server on `$PORT`. Sole job: report the contents of the state file as JSON. No other logic. |
| `render/render_supervisor.sh` | PID 1. Owns `server`, `keepalive.py`, `cloudflared`; writes the state file; publishes to git. |
| `render/net.sh` | Sourced library. `probe_best_edge_ip`, `pin_tunnel_hostname`. |
| `render/trigger_render.sh` | Mac entry point. Wake → reconcile → pin DNS → hand off. |
| `render/render.yaml` | Render Blueprint. |
| `render/Dockerfile` | Runtime image. `CMD` = supervisor. |
| `render/README.md` | How to deploy, and how to move the folder to another repo. |
| `render/run_tests.sh` | Single entry point that runs all three test files. |
| `render/test_keepalive.py` | `unittest` for the `/healthz` contract. |
| `render/test_scripts.sh` | bash tests for the supervisor's pure functions and `net.sh`. |
| `render/test_trigger_render.sh` | bash integration test for wake/reconcile against a fake health server. |

### Cross-task contracts

These names and shapes are fixed. A mismatch is a bug, not a style choice.

**Contract A — state file**, written by the supervisor (Task 2), read by `keepalive.py` (Task 1).
Path: `$TCPUDP_STATE_DIR/tunnel.json`, default `TCPUDP_STATE_DIR=/run/tcpudp`.
Exactly these five keys:

```json
{"hostname": "abc-def.trycloudflare.com", "port": 7001, "published": true, "source": "render", "updated": "2026-09-27T12:34:56Z"}
```

`hostname` is `null` (JSON null, not the string `"null"`) until the tunnel is up. `updated` is UTC `%Y-%m-%dT%H:%M:%SZ`. Writes are atomic: write to `"$file.tmp"`, then `mv`.

**Contract B — `/healthz` response**, served by `keepalive.py` (Task 1), parsed by `trigger_render.sh` (Task 3).
`GET /healthz` and `GET /` both return `200` with the state file's contents verbatim, plus `"status": "ok"`. Every other path returns `404`. **The endpoint always returns 200**, including when the state file is missing or unparseable — in that case `hostname` is `null` and the other four keys take their defaults.

**Contract C — `net.sh` exports.** `probe_best_edge_ip` echoes `"<ip> <avg_rtt_ms>"` for the fastest reachable candidate, and echoes nothing (exit 1) if none respond. `pin_tunnel_hostname <bare-host>` appends a `/etc/hosts` line only when one is absent, and echoes one of `already-pinned`, `pinned <ip>`, or `fallback <ip>`.

---

### Task 1: `keepalive.py` and its `/healthz` contract

**Files:**
- Create: `render/keepalive.py`
- Create: `render/test_keepalive.py`

**Interfaces:**
- Consumes: Contract A — reads `$TCPUDP_STATE_DIR/tunnel.json` (default `/run/tcpudp/tunnel.json`).
- Produces: Contract B. Task 2 writes the state file; Task 3 parses the response.
- `keepalive.py` reads `PORT` (default `10000`) and `TCPUDP_STATE_DIR` (default `/run/tcpudp`) from the environment. It takes no CLI arguments.
- Test hook: `keepalive.py` reads a further optional env var `KEEPALIVE_READY_FILE`; if set, it touches that path once the socket is listening, and re-touches it after every request. This exists so tests can wait on readiness without sleeping; production never sets it.

- [ ] **Step 1: Write the failing tests in `render/test_keepalive.py`**

`unittest`, stdlib only. Helper `start_server(state_dir, port)` uses `subprocess.Popen` to launch `keepalive.py`, waits for `KEEPALIVE_READY_FILE` to appear (timeout 10s), and registers cleanup. Helper `get(path, port)` uses `urllib.request` and returns `(status, parsed_json_or_None)`.

Tests, each named for the behaviour it pins:

- `test_healthz_returns_state_file_contents` — write a state file with all five keys filled in; `GET /healthz` → status 200 and `body["hostname"] == "abc-def.trycloudflare.com"`, `body["published"] is True`, `body["source"] == "render"`.
- `test_root_path_serves_same_payload` — `GET /` → 200, and its JSON equals `GET /healthz`'s JSON.
- `test_status_ok_always_present` — `GET /healthz` → `body["status"] == "ok"`.
- `test_unknown_path_returns_404` — `GET /nope` → status 404.
- `test_missing_state_file_returns_200_with_null_hostname` — no state file at all; `GET /healthz` → status 200 and `body["hostname"] is None`. This is the post-wake, pre-tunnel case and must not 500.
- `test_corrupt_state_file_returns_200_with_null_hostname` — write `{"hostname": ` (truncated); `GET /healthz` → status 200, `body["hostname"] is None`.
- `test_null_hostname_in_state_file_is_preserved` — state file with `"hostname": null`; `GET /healthz` → 200 and `body["hostname"] is None`.
- `test_defaults_applied_for_missing_keys` — state file containing only `hostname`; `GET /healthz` → 200 and `body["port"] == 7001`, `body["source"] == "render"`, `body["published"] is False`, and `updated` is present as a string.
- `test_state_file_is_reread_per_request` — `GET /healthz`, then overwrite the state file with a different hostname, then `GET /healthz` again → the second response has the new hostname. The server must not cache.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cd render && python3 -m unittest test_keepalive -v
```
Expected: `ERROR` — `ModuleNotFoundError: No module named 'keepalive'` (or an `FileNotFoundError` for the script), so all 9 tests fail.

- [ ] **Step 3: Implement `render/keepalive.py`**

Structure, in this order:

- `DEFAULTS` dict — `port: 7001`, `published: False`, `source: "render"`, `updated: ""`, `hostname: None`. These are the exact values pinned by `test_defaults_applied_for_missing_keys`.
- `state_path() -> str` — `os.path.join(os.environ.get("TCPUDP_STATE_DIR", "/run/tcpudp"), "tunnel.json")`.
- `read_state() -> dict` — read the file; on missing file, `json.JSONDecodeError`, or a non-dict result, return `DEFAULTS` merged with `{"hostname": None, "status": "ok"}`. Otherwise return `DEFAULTS` updated by the parsed dict, then force `status` to `"ok"`. Never raises.
- `class Handler(BaseHTTPRequestHandler)` with `do_GET`. Route on `self.path` split on `?`, ignoring the query string: `"/healthz"` and `"/"` → 200 + `json.dumps(read_state())`; anything else → 404 + a short body.
- `log_message` overridden to a no-op, so the container log stays clean.
- `main()` — bind `0.0.0.0` on `int(os.environ.get("PORT", "10000"))` via `ThreadingHTTPServer`, then if `KEEPALIVE_READY_FILE` is set, `open(..., "w").close()` it, then `serve_forever()`. Guarded by `if __name__ == "__main__":`.

The state file is re-read inside `do_GET`, never cached.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cd render && python3 -m unittest test_keepalive -v
```
Expected: `Ran 9 tests ... OK`.

- [ ] **Step 5: Syntax-check and commit**

```bash
python3 -m py_compile render/keepalive.py && bash -n /dev/null
cd render && python3 -m unittest test_keepalive 2>&1 | tail -2
cd .. && git add render/keepalive.py render/test_keepalive.py \
  && git commit -m "feat(render): add keepalive health endpoint reporting live tunnel state"
```

---

### Task 2: `render_supervisor.sh`

**Files:**
- Create: `render/render_supervisor.sh`
- Create: `render/test_scripts.sh`

**Interfaces:**
- Consumes: Contract A (writes it), `PORT`, and the global-constraints env vars.
- Produces: `$TCPUDP_INFO_DIR/cloudflare.sh`, `.bat`, `run_info.json`, `render_info.json` in the clone at `$TCPUDP_REPO_DIR`; a commit on `$GITHUB_PUSH_BRANCH`; the `tunnel.json` that Task 1 serves.
- Function signatures, fixed:

```
log <msg...>                              # "<ts> <msg>" to stdout
die <msg...>                              # log to stderr, exit 1
resolve_config                            # sets every global below from env + defaults
state_file                                # echoes $TCPUDP_STATE_DIR/tunnel.json
write_state <hostname> <published 0|1>    # atomic; hostname "" means JSON null
read_state_hostname                       # echoes hostname, or nothing if unreadable
is_alive <pidfile>                        # exit 0 if the pid in the file is running
remote_url                                # echoes $GITHUB_REMOTE_URL
push_url                                  # echoes $GITHUB_PUSH_URL
ensure_repo                               # clone $GITHUB_REMOTE_URL --branch $GITHUB_PUSH_BRANCH --depth 50 into $TCPUDP_REPO_DIR if $TCPUDP_REPO_DIR/.git is absent
fetch_server                              # download + untar the release tarball; extract server to $TCPUDP_SERVER_BIN
start_server                              # nohup $TCPUDP_SERVER_BIN; wait for 127.0.0.1:$TCPUDP_SERVER_PORT to listen
start_keepalive                           # nohup python3 keepalive.py
start_tunnel                              # kill prior cloudflared, relaunch, poll log; echoes bare hostname or nothing
publish <hostname> <force 0|1>            # 0 on success
supervise_loop                            # never returns
cleanup                                   # kill children, exit 0
main
```

- Globals set by `resolve_config`: `RELEASE, SERVER_PORT, REPO_DIR, INFO_DIR, STATE_DIR, SERVER_BIN, SUPERVISE_INTERVAL, TUNNEL_START_TIMEOUT, PAT, GITHUB_REPO, PUSH_BRANCH, PUBLISH_ENABLED, PORT, KEEPALIVE_SCRIPT, CLOUDFLARED_LOG, STATE_PIDFILE, SERVER_PIDFILE, KEEPALIVE_PIDFILE, TUNNEL_PIDFILE`.
- `publish` is called by `main` with force `1` and by `supervise_loop` with force `0`. When `force` is `0` and `hostname` equals the hostname already recorded in `$REPO_DIR/$INFO_DIR/run_info.json`, it must return 0 without staging or committing anything.

- [ ] **Step 1: Write the failing tests in `render/test_scripts.sh`**

Plain bash, no framework. `set -uo pipefail` (deliberately not `-e`, so a failing assertion reports rather than aborts). Helpers: `pass`/`fail` counters, `assert_eq <expected> <actual> <label>`, `assert_contains <haystack> <needle> <label>`, and `new_sandbox` which makes a temp dir containing a bare `origin` repo plus a clone of it, exporting `GITHUB_REMOTE_URL` and `GITHUB_PUSH_URL` at the bare repo and `TCPUDP_REPO_DIR` at the clone.

The script under test is loaded with:

```bash
TCPUDP_STATE_DIR="$sandbox/state" SUPERVISE_INTERVAL=1 TUNNEL_START_TIMEOUT=2 \
  source ./render_supervisor.sh
```

which must not start anything, because of the `BASH_SOURCE` guard.

Tests, each named for the behaviour it pins:

- `test_resolve_config_defaults` — after `resolve_config`, `SERVER_PORT` is `7001`, `RELEASE` is `v1.1.16`, `INFO_DIR` is `github_run`, `PUSH_BRANCH` is `run`, `PUBLISH_ENABLED` is `1`.
- `test_write_state_writes_all_five_keys` — `write_state "h.trycloudflare.com" 1`; the file parses as JSON with exactly the keys `hostname, port, published, source, updated`, `hostname == "h.trycloudflare.com"`, `published is True`, `source == "render"`, and `updated` matches `^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$`.
- `test_write_state_empty_hostname_is_json_null` — `write_state "" 0`; parsed `hostname is None`.
- `test_write_state_is_atomic` — after a write, no `"$state.tmp"` file remains.
- `test_publish_writes_run_yml_compatible_files` — `publish "h.trycloudflare.com" 1`; assert `$REPO_DIR/github_run/cloudflare.sh` is exactly `cloudflared access tcp --url tcp://localhost:7001 --hostname h.trycloudflare.com` followed by one newline; `.bat` is byte-identical to `.sh`; the clone-root `cloudflare.sh` matches; `run_info.json` has `hostname == "https://h.trycloudflare.com"`, `port == 7001`, `source == "render"`, and a `timestamp` matching the same regex; `render_info.json` exists and parses.
- `test_publish_commits_to_the_push_branch` — after the publish above, `git -C "$TCPUDP_REPO_DIR" log --format=%s -1` is exactly `Auto-update cloudflare tunnel info (render)`, and `git -C "$origin" log --format=%s -1` (the bare repo) is the same — proving the push landed.
- `test_publish_skips_when_unchanged_and_not_forced` — publish twice with the same hostname, the second with force `0`; the origin's commit count is unchanged after the second call.
- `test_publish_commits_when_hostname_changes` — publish `h1` forced, then `h2` with force `0`; the origin's `cloudflare.sh` contains `h2` and the origin has 2 commits.
- `test_publish_without_pat_degrades_gracefully` — unset `PAT`; `publish "h.trycloudflare.com" 1` returns 0, `$REPO_DIR/github_run/cloudflare.sh` is still written, no commit was created, and the state file records `published: false`.
- `test_publish_rebases_when_branch_moved_on` — publish and push; then create a commit on the bare repo's branch from a second clone; then `publish "h2.trycloudflare.com" 1`; the origin log must have 3 commits, contain both hostnames, and no commit may be lost.
- `test_publish_respects_publish_disabled` — `PUBLISH_ENABLED=0`; `publish "h" 1` returns 0, no commit, no files written.
- `test_pat_never_appears_in_state_or_repo` — with `PAT` set to a sentinel like `ghp_SENTINEL`, run a publish and `git -C "$REPO_DIR" log -p` plus `grep -r` over `$REPO_DIR` and the state file; the sentinel must not appear anywhere.
- `test_is_alive_detects_dead_and_live_pids` — write a real `sleep 30` pid to a pidfile → `is_alive` exits 0; write a definitely-dead pid → exits non-zero.
- `test_sigterm_reaps_children_and_exits_zero` — launch the supervisor as a real `subprocess` with stub `server` and stub `cloudflared` on `PATH`, `PUBLISH=false`, `TCPUDP_SERVER_BIN` pointed at the stub; wait for the state file to have a non-null hostname; send `SIGTERM`; assert exit status 0 and that no `stub_server` or `stub_cloudflared` process remains.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
bash render/test_scripts.sh
```
Expected: non-zero exit with the failure count ≥ 1 and an error naming `render_supervisor.sh` as missing.

- [ ] **Step 3: Implement `render/render_supervisor.sh`**

Behaviour to pin, function by function:

- `resolve_config` — `${VAR:-default}` for every global in the Interfaces list, so an unset var never trips `set -u`. `PUBLISH_ENABLED` becomes `1`/`0`. The last line is the `BASH_SOURCE` guard calling `main "$@"`.
- `write_state` — build the JSON with `printf`, not string concatenation of shell-escaped values; hostname is `null` when the argument is empty; write `"$state_file.tmp"` then `mv`.
- `is_alive` — read the first field of the pidfile, return 1 if empty, else `kill -0 "$pid"`.
- `ensure_repo` / `fetch_server` — both `log` and return non-zero on failure; `fetch_server` downloads `https://github.com/xiguichen/tcpudp/releases/download/$RELEASE/tcpudp-ubuntu-latest.tar.gz` into `$REPO_DIR`, untars, moves `tcpudp-ubuntu-latest/server` to `$TCPUDP_SERVER_BIN`, and `chmod +x`.
- `start_server` — `nohup "$TCPUDP_SERVER_BIN" > "$REPO_DIR/server.log" 2>&1 &`, record the pid, then poll `ss -ltn` (falling back to a `/dev/tcp` connect) for `:$SERVER_PORT` up to 10 times at 1s.
- `start_tunnel` — `pkill -f '^cloudflared tunnel --url tcp://127.0.0.1:'` (anchored so it never matches the supervisor's own command line), truncate `$CLOUDFLARED_LOG`, launch with `--logfile`, then poll the log for `https://[a-z0-9-]*\.trycloudflare\.com` up to `TUNNEL_START_TIMEOUT` at 1s. Echoes the bare hostname, or nothing on timeout.
- `publish` — order matters: write the four info files, then `git -C "$REPO_DIR" add` them, then `commit`, then `pull --rebase origin "$PUSH_BRANCH"`, then `git push "$push_url" "HEAD:$PUSH_BRANCH"`. Any git failure logs a warning, calls `write_state "$hostname" 0`, and returns 0 — it must never take the service down. When `PAT` is empty or `PUBLISH_ENABLED` is `0`, return 0 immediately after logging once, without writing anything.
- `supervise_loop` — each tick: `is_alive` the server pidfile else `start_server`; `is_alive` the tunnel pidfile else `start_tunnel`; if the tunnel came back with a hostname, `publish "$hostname" 0`; sleep `SUPERVISE_INTERVAL`.
- `cleanup` — `trap`-registered for `TERM` and `INT`; kills the three children by pidfile, then `exit 0`.
- `main` — `resolve_config`; create `$STATE_DIR`; `preflight` (assert `server`, `cloudflared`, `python3`; **warn** not die if `PAT` is empty); `ensure_repo`; `fetch_server` if `$SERVER_BIN` is not executable; `start_server`; `start_keepalive`; `start_tunnel`; `write_state "$hostname" 1`; `publish "$hostname" 1`; `supervise_loop`.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
bash render/test_scripts.sh
```
Expected: exit 0, failure count 0.

- [ ] **Step 5: Syntax-check and commit**

```bash
bash -n render/render_supervisor.sh && bash render/test_scripts.sh
git add render/render_supervisor.sh render/test_scripts.sh \
  && git commit -m "feat(render): add supervisor owning server, keepalive, tunnel and git publishing"
```

---

### Task 3: `trigger_render.sh` and `net.sh`

**Files:**
- Create: `render/net.sh`
- Create: `render/trigger_render.sh`
- Create: `render/test_trigger_render.sh`

**Interfaces:**
- Consumes: Contract B (parses `/healthz` with `python3 -c`, since `jq` is not on the Mac), and `$TCPUDP_INFO_DIR/cloudflare.sh` in the local checkout.
- Produces: `pin_tunnel_hostname` and `probe_best_edge_ip` for reuse; exits 0 after printing the handoff, having left `github_run/cloudflare.sh` holding the live hostname.
- `trigger_render.sh` options: `--health-url URL`, `--timeout SECONDS` (default 300), `--no-wait`. It reads `RENDER_HEALTH_URL`, `GITHUB_PUSH_BRANCH` and `TCPUDP_INFO_DIR` from the environment, resolving the repo root as the parent of the script's own directory.
- `net.sh` honours `CF_EDGE_IPS` (space-separated, default `162.159.38.209 104.17.213.97`), `PING_COUNT` (default 5) and `HOSTS_FILE` (default `/etc/hosts`) — `HOSTS_FILE` exists so the pinning logic is testable without `sudo`.

- [ ] **Step 1: Write the failing tests in `render/test_trigger_render.sh`**

Plain bash, same helpers as Task 2. A `fake_health` helper starts `python3 -m http.server` in a temp dir whose handler serves a mutable `body` file, so a test can change what `/healthz` returns between polls.

Tests, each named for the behaviour it pins:

- `test_probe_best_edge_ip_picks_lowest_rtt` — stub `ping` on `PATH` that prints canned output keyed by IP, with `h1.test` fastest; `probe_best_edge_ip` echoes `<h1_ip> <rtt>`.
- `test_probe_best_edge_ip_fails_when_none_respond` — stub `ping` that always exits non-zero; `probe_best_edge_ip` exits non-zero and echoes nothing.
- `test_pin_tunnel_hostname_appends_when_absent` — `HOSTS_FILE` in a sandbox; `pin_tunnel_hostname "h.trycloudflare.com"` echoes `pinned <ip>` and the file has exactly one line ending in that host.
- `test_pin_tunnel_hostname_is_idempotent` — call twice; echoes `already-pinned` the second time and the file still has exactly one matching line.
- `test_pin_tunnel_hostname_replaces_stale_entry` — pre-seed a line for the host with a different IP; after the call exactly one line remains for that host.
- `test_trigger_render_waits_through_a_non_json_loading_page` — fake health returns `<html>Loading...</html>` for the first 3 polls, then valid JSON; the script still exits 0 and reports the hostname. This is the free-tier spin-up page and is the single most common real occurrence.
- `test_trigger_render_waits_for_null_hostname_then_succeeds` — fake health returns `hostname: null` for the first 3 polls, then a real hostname; exit 0.
- `test_trigger_render_times_out_with_clear_error` — fake health always returns `hostname: null`; with `--timeout 6` the script exits non-zero and its output contains `timed out`.
- `test_trigger_render_uses_live_hostname_when_branch_is_stale` — local checkout's `cloudflare.sh` holds `old.trycloudflare.com`, fake health reports `new.trycloudflare.com`, and `git fetch` never yields a matching commit; the script exits 0, prints a warning containing `stale`, and the local `cloudflare.sh` now contains `new.trycloudflare.com`.
- `test_trigger_render_accepts_matching_branch` — local `cloudflare.sh` already holds the live hostname; exit 0, and no `stale` warning is printed.
- `test_trigger_render_prints_handoff` — output contains `./run_github.sh`.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
bash render/test_trigger_render.sh
```
Expected: non-zero exit, failures naming missing `render/net.sh` and `render/trigger_render.sh`.

- [ ] **Step 3: Implement `render/net.sh`**

- `probe_best_edge_ip` — ping every IP in `$CF_EDGE_IPS` in parallel into per-IP temp files, `wait`, then pick the smallest average from the `round-trip` lines, treating an unreachable IP as `999999`. Echoes `"<ip> <rtt>"`, or exits 1 if every IP was unreachable. This is the same algorithm `trigger_run.sh` uses, minus the printout of every candidate's RTT.
- `pin_tunnel_hostname <host>` — if `$HOSTS_FILE` already has a line ending in ` <host>`, remove it; resolve the best edge IP; if none respond, fall back to the first entry of `$CF_EDGE_IPS`; append `"<ip> <host>"` using `tee -a` (not `sudo tee` — the caller supplies privilege, so tests need none). Echo `pinned <ip>`, `fallback <ip>`, or `already-pinned` when the correct line was already present and nothing changed.

- [ ] **Step 4: Implement `render/trigger_render.sh`**

- `health_hostname <url>` — `curl -fsS --max-time 10 "$url"`, pipe through `python3 -c` that reads stdin and prints `body["hostname"] or ""`, exiting 0. Non-JSON input, a non-200, or a missing key must all yield an empty string and exit 0, never a traceback.
- `main` — resolve the repo root as the script's parent directory; `cd` there; `git checkout "$GITHUB_PUSH_BRANCH"` if not already on it; `git pull origin "$GITHUB_PUSH_BRANCH"`. Then the wake loop: poll `health_hostname` every 5s until non-empty, printing `.` per attempt, failing with a message containing `timed out` after `--timeout` seconds. Then reconcile: compare the live hostname against `$TCPUDP_INFO_DIR/cloudflare.sh`; on mismatch, poll `git fetch origin "$GITHUB_PUSH_BRANCH"` every 5s for up to 120s for a matching commit; if it is still mismatched, write the live hostname into the local `cloudflare.sh` and warn with a message containing `stale`. Then `source`-relative `net.sh` and `pin_tunnel_hostname`. Then print the hostname and `Next step:  ./run_github.sh`.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
bash render/test_trigger_render.sh
```
Expected: exit 0, failure count 0.

- [ ] **Step 6: Syntax-check and commit**

```bash
bash -n render/net.sh && bash -n render/trigger_render.sh && bash render/test_trigger_render.sh
chmod +x render/trigger_render.sh
git add render/net.sh render/trigger_render.sh render/test_trigger_render.sh \
  && git commit -m "feat(render): add Mac-side trigger script and shared edge-pinning library"
```

---

### Task 4: `render.yaml`, `Dockerfile`, `run_tests.sh`, `README.md`

**Files:**
- Create: `render/render.yaml`
- Create: `render/Dockerfile`
- Create: `render/run_tests.sh`
- Create: `render/README.md`

**Interfaces:**
- Consumes: every env var name from the Global Constraints, and `/usr/local/bin/render_supervisor.sh` + `/usr/local/bin/keepalive.py` as the image's `CMD` target.
- Produces: a Blueprint that Render can apply, and one command to run the whole suite.

- [ ] **Step 1: Write `render/render.yaml`**

Exactly the service block from the spec's *`render/render.yaml`* section: `type: web`, `name: tcpudp-render`, `runtime: docker`, `rootDir: render`, `dockerfilePath: ./Dockerfile`, `dockerContext: .`, `plan: free`, `region: oregon`, `branch: run`, `autoDeployTrigger: 'off'`, `healthCheckPath: /healthz`, and the `envVars` list with `TCPUDP_RELEASE`, `TCPUDP_SERVER_PORT`, `GITHUB_REPO`, `GITHUB_PUSH_BRANCH`, `SUPERVISE_INTERVAL`, and `GITHUB_PAT` with `sync: false`.

- [ ] **Step 2: Write `render/Dockerfile`**

Exactly the Dockerfile from the spec, with `COPY render_supervisor.sh` and `COPY keepalive.py` (no `net.sh` — the supervisor does not use it), `ARG CLOUDFLARED_VERSION=2026.7.1`, `EXPOSE 10000`, and `CMD ["/usr/local/bin/render_supervisor.sh"]`. Paths are relative to the build context, which `rootDir: render` sets to this folder.

- [ ] **Step 3: Write `render/run_tests.sh`**

Runs, in order, and exits non-zero if any fail: `python3 -m unittest test_keepalive -v` (from inside `render/`), then `bash render/test_scripts.sh`, then `bash render/test_trigger_render.sh`. Prints a one-line summary per file. `chmod +x`.

- [ ] **Step 4: Write `render/README.md`**

Cover, in this order: what the folder is; the two commands to run on the Mac (`./render/trigger_render.sh` then `./run_github.sh`); the one-time Render setup (New → Blueprint, blueprint path `render/render.yaml`, branch `run`, add `GITHUB_PAT`, confirm `region: oregon` and `autoDeployTrigger: 'off'`); how to bump the release (`TCPUDP_RELEASE` + redeploy); how to run the tests; the full env-var table; and the *Moving to another repository* procedure from the spec.

- [ ] **Step 5: Verify the suite passes end to end**

```bash
./render/run_tests.sh
```
Expected: all three files report zero failures and the script exits 0.

- [ ] **Step 6: Verify the blueprint's load-bearing fields are present**

```bash
cd render && grep -nE "autoDeployTrigger|rootDir|region:|healthCheckPath|runtime:|plan:" render.yaml
```
Expected: exactly one match each, with the values `autoDeployTrigger: 'off'`, `rootDir: render`, `region: oregon`, `healthCheckPath: /healthz`, `runtime: docker`, `plan: free`.

Note: `shellcheck`, `docker`, `jq` and `PyYAML` are not installed on this machine, so those checks cannot run locally. The authoritative validation of `render.yaml` is applying the Blueprint in the Render dashboard, which is the next step.

- [ ] **Step 7: Verify nothing outside `render/` changed**

```bash
git diff --stat main...HEAD -- . ':(exclude)render' ':(exclude)docs'
```
Expected: only `docs/superpowers/**` appears. Any other path means the Global Constraints were broken.

- [ ] **Step 8: Commit**

```bash
git add render/render.yaml render/Dockerfile render/run_tests.sh render/README.md \
  && git commit -m "feat(render): add Blueprint, Dockerfile, test runner and README"
```

---

### Task 5: Live rollout on Render (manual, requires credentials)

No code is written in this task. It cannot be automated from here because it
needs a Render account and a GitHub PAT, and it is the only way to prove the
deploy works.

- [ ] **Step 1: Push the branch**

```bash
git push origin run
```
Note: `run.yml` triggers only on `trigger/*` changes, so this push does not start a GitHub Actions tunnel.

- [ ] **Step 2: Create the service in the dashboard**

Render dashboard → **New → Blueprint** → repo `xiguichen/tcpudp` → blueprint path `render/render.yaml` → branch `run` → apply. Then add the `GITHUB_PAT` environment variable (fine-grained token, `contents: write` on `xiguichen/tcpudp`).

- [ ] **Step 3: Confirm the load-bearing settings took**

Service → Settings: region **Oregon**, auto-deploy **off**. If either is wrong, fix in the dashboard — the region cannot be changed after creation.

- [ ] **Step 4: Confirm the service is healthy**

```bash
curl -fsS https://tcpudp-render.onrender.com/healthz
```
Expected: JSON with a non-null `hostname` and `"published": true`. The first call after a cold start may take up to ~2 minutes while the free instance spins up and the tunnel registers; retry.

- [ ] **Step 5: Confirm the hostname reached the branch**

```bash
git fetch origin run && git log --oneline -3 origin/run
```
Expected: an `Auto-update cloudflare tunnel info (render)` commit, and `github_run/cloudflare.sh` on that commit naming the same hostname `/healthz` reported.

- [ ] **Step 6: Confirm the client path works**

```bash
./render/trigger_render.sh
./run_github.sh
```
Expected: the trigger prints the hostname and `Next step: ./run_github.sh`; the client connects.

- [ ] **Step 7: Confirm a UDP round trip**

In a second terminal, with the client running:
```bash
python3 src/test/client.py
```
Expected: the echo responses print and no `An error occurred` line appears.

---

## Review Focus

The five conditions most likely to bite someone using this, none of which the
happy path exercises. Each has a test in the task that owns the code.

1. **Render serves its spin-up "loading page" instead of JSON.** On the free
   tier this happens on essentially every cold start, and a naive parser either
   crashes or mistakes it for a hostname. Expected: the wake loop keeps polling
   and the user sees dots, not an error. → `test_trigger_render_waits_through_a_non_json_loading_page` (Task 3)
2. **`/healthz` is hit before the tunnel exists.** Expected: `200` with
   `hostname: null` — never a 500, which would fail Render's health check and
   also leave the Mac unable to tell "still waking" from "broken". →
   `test_missing_state_file_returns_200_with_null_hostname`,
   `test_corrupt_state_file_returns_200_with_null_hostname` (Task 1)
3. **The committed `cloudflare.sh` disagrees with the live hostname** — the
   normal case after a free-tier sleep. Expected: the live hostname wins, and
   `run_github.sh` still connects even if the git push never happened. →
   `test_trigger_render_uses_live_hostname_when_branch_is_stale` (Task 3)
4. **`GITHUB_PAT` is unset, mis-scoped, or the push is rejected.** Expected: the
   service keeps serving, the tunnel stays up, `published` reads `false`, and
   the failure is visible in the logs rather than silently looping. →
   `test_publish_without_pat_degrades_gracefully`,
   `test_publish_respects_publish_disabled` (Task 2)
5. **Render sends `SIGTERM` during a deploy.** Expected: children are reaped,
   exit status 0, and no orphaned `server` or `cloudflared` holding port 7001. →
   `test_sigterm_reaps_children_and_exits_zero` (Task 2)

Two further conditions the spec calls out get tests even though they did not make
the top five, because they corrupt shared state rather than failing locally:
`test_publish_rebases_when_branch_moved_on` (the supervisor must never clobber
history on `run`) and `test_pat_never_appears_in_state_or_repo` (a leaked token
in a public repo is unrecoverable once pushed).

## Self-Review

**Spec coverage.** Every spec section maps to a task: platform constraints →
Global Constraints and Task 4's settings checks; the folder layout → File
Structure; `render.yaml`, `Dockerfile`, `keepalive.py`,
`render_supervisor.sh`, `trigger_render.sh`, `net.sh` → Tasks 1–4; the
configuration table → Global Constraints; publishing rules → Task 2's `publish`
tests; the four files written by `publish` in `run.yml`-compatible format →
`test_publish_writes_run_yml_compatible_files`; portability → Task 4's README;
rollout → Task 5; risks and rejected alternatives → design decisions already
locked in Task 2 and Task 3.

**Step scan.** No step says "handle errors" or "add validation" without naming
the behaviour. Script bodies are described function by function with the values
they must use, not transcribed.

**Type consistency.** `TCPUDP_STATE_DIR` (the directory) versus
`state_file` (the file inside it) is the one place a rename would bite; both
Tasks 1 and 2 name `tunnel.json` inside it explicitly. `write_state <hostname>
<published 0|1>` is called with force-`1` from `main` and force-`0` from
`supervise_loop` in Task 2, and `publish <hostname> <force 0|1>` is tested with
both values.

**Review Focus.** Five lines, each with the test that pins it, added to the
owning task. Two extra tests noted beneath the list.

**Proportion.** Six implementation files, four test files, five tasks against a
six-file spec — proportionate for deploy code that has no existing CI coverage.
