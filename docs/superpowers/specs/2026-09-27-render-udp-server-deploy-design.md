# Render UDP Server Deployment — Design

Date: 2026-09-27

## Goal

Run the same `tcpudp` C++ server on [Render](https://dashboard.render.com) that
`.github/workflows/run.yml` runs on GitHub Actions, and make it reachable from the
Mac client through the same Cloudflare Quick Tunnel mechanism — so the `run`
branch gains a third interchangeable way to obtain a working tunnel, alongside
the existing GitHub Actions path and the vpn2 path.

Success looks like: after one `./render/trigger_render.sh`, `./run_github.sh`
connects and `python3 src/test/client.py` completes a UDP round trip — with no
changes to `run_github.sh`, `run/config.json`, or `udp_client`.

## Everything lives in one self-contained folder

All new code goes under `render/`, and nothing outside it is touched. The folder
is a deployable unit that can be lifted into a different Git repository without
edits beyond the environment variables.

```
render/
  render.yaml            # Render Blueprint
  Dockerfile             # runtime image
  render_supervisor.sh   # container PID 1: owns server + keepalive + tunnel
  keepalive.py           # HTTP endpoint reporting live tunnel state
  net.sh                 # sourced: Cloudflare edge probing + /etc/hosts pinning
  trigger_render.sh      # Mac-side: wake Render, reconcile, hand off
```

This is enforced by Render itself: the service sets `rootDir: render`, and
Render documents that *"files outside your service's root directory are not
available to the service at build time or at runtime."* The `Dockerfile` build
context is therefore just this folder, and the supervisor must fetch everything
else it needs at runtime.

## Verified platform constraints

Checked against Render's current documentation, not assumed.

1. **No public raw TCP or UDP ingress.** Render forwards inbound traffic to a
   single HTTP port per web service (`$PORT`, default `10000`). The docs are
   explicit that the port "is not directly reachable via the public internet".
   A `7001` listener is therefore unreachable from the internet and *must* be
   reached via an outbound tunnel.
2. **Stable hostname, no static IP.** Every web service gets a permanent
   `https://<service-name>.onrender.com` subdomain that survives restarts,
   spin-downs and redeploys. There is no dedicated IP; the edge is Cloudflare
   and anycast, and addresses rotate.
3. **Free instances sleep after ~15 minutes** without inbound traffic, spin back
   up on the next inbound HTTP request (taking roughly a minute), and **lose
   local filesystem changes** when they spin down.
4. **Background workers have no public URL and cannot receive private-network
   traffic**, so a free background worker has no way to be woken from outside.
   `RENDER_EXTERNAL_HOSTNAME` / `RENDER_EXTERNAL_URL` are populated only for web
   services and static sites.
5. **Regions:** Oregon (default), Ohio, Virginia, Frankfurt, Singapore.
   `run.yml` blacklists Virginia, so this design uses **Oregon**. The region
   cannot be changed after service creation.
6. **Blueprint auto-deploy defaults to `commit`** (i.e. on) for a new service,
   and the modern field is `autoDeployTrigger: 'off'` — the older `autoDeploy:
   false` is deprecated but equivalent.
7. The repository `xiguichen/tcpudp` is **public**, so clone/pull need no
   credentials; only push does.

## Non-goals

- No change to the C++ `server`, `udp_client`, or the protocol.
- No change to `run.yml`, `trigger_run.sh`, `trigger_vpn2.sh`, or
  `run_github.sh`. The GitHub Actions and vpn2 paths keep working untouched.
- No public UDP exposure directly from Render. It is architecturally impossible
  there; the tunnel is the answer.
- No static IP, custom domain, or named Cloudflare Tunnel.
- No new top-level `lib/` directory — the shared helper lives in `render/net.sh`
  to keep the folder self-contained.
- No migration of the existing DNS-pinning code in `trigger_run.sh` /
  `trigger_vpn2.sh` to `render/net.sh` (see *Judgment calls*).

## Architecture

```
                    ┌──────────────── Render container ────────────────┐
                    │                                                  │
 Mac                │  render_supervisor.sh  (PID 1, loop)              │
 ┌────────┐         │    ├── server            127.0.0.1:7001  ◄──┐    │
 │ render │         │    ├── keepalive.py     0.0.0.0:$PORT     │    │
 │ /trigge│         │    └── cloudflared  ────┘ (tunnel --url     │    │
 │ r_ren  │         │                            tcp://127.0.0.1 │    │
 │ der.sh │         │                                 :7001)     │    │
 └───┬────┘         │  writes /run/tcpudp/tunnel.json ───────────┼─┐  │
     │              └──────────────────────────────────────────┼─┼──┘
     │ git pull                                                  │ │
     ▼                                                         ▼ ▼
 ┌──────────┐   push (PAT)   ┌──────────────┐            Cloudflare edge
 │ run      │◄───────────────│ run branch   │
 │ branch   │                │ github_run/  │
 └──────────┘                └──────────────┘
     │
     │  run_github.sh
     ▼
 cloudflared access ──► Cloudflare edge ──► tunnel ──► server:7001
     │
     ▼
 udp_client (UDP 5003)
```

### Components

Each unit has one job and a defined interface.

| Unit | Responsibility | Interface |
|---|---|---|
| `render.yaml` | Declares the Render service declaratively | Blueprint consumed by Render |
| `Dockerfile` | Produces the runtime image (OS deps + `cloudflared` + the two container scripts) | Image; `CMD` launches the supervisor |
| `keepalive.py` | Serves supervisor state over HTTP | `GET /healthz` → JSON; reads `/run/tcpudp/tunnel.json` |
| `render_supervisor.sh` | Owns the lifecycle of server + keepalive + tunnel, and publishes the hostname | Reads env; writes `tunnel.json` and the `github_run/` files; pushes to git |
| `trigger_render.sh` | Wakes the Render instance and reconciles the local checkout with the live tunnel | `--health-url`, `--timeout`; ends by handing off to `run_github.sh` |
| `net.sh` | Cloudflare edge-IP probing and `/etc/hosts` pinning | `probe_best_edge_ip`, `pin_tunnel_hostname` |

### The central design decision

**`GET /healthz` reports the live tunnel hostname.**

This is what makes the free tier workable. After a spin-down the committed `run`
branch may be stale, and on a resumed instance the supervisor may never re-run
its startup sequence — so waiting for a new git commit can hang forever. Asking
Render directly for the current hostname sidesteps that entirely.

The `.onrender.com` hostname therefore does two jobs, and neither requires a
static IP:

1. **Wake mechanism** — the free instance spins back up on the next inbound
   HTTP request.
2. **Live source of truth** for the current tunnel hostname.

Git push-back to the `run` branch is retained as the durable record, and
`run_github.sh` continues to work unchanged because it only reads
`github_run/cloudflare.sh`.

## Configuration surface

Everything environment-specific is an env var with a working default, so the
folder can move to another repo by changing configuration only.

| Variable | Default | Used by | Purpose |
|---|---|---|---|
| `GITHUB_REPO` | `xiguichen/tcpudp` | supervisor, `trigger_render.sh` | `owner/name` to clone and push to |
| `GITHUB_PUSH_BRANCH` | `run` | supervisor | Branch the tunnel info is pushed to |
| `GITHUB_PAT` | *(unset)* | supervisor | Token with `contents: write`. Absent ⇒ publish is skipped, service still works |
| `TCPUDP_RELEASE` | `v1.1.16` | supervisor | Release tag to download `server` from |
| `TCPUDP_SERVER_PORT` | `7001` | supervisor | Port the server and tunnel use |
| `TCPUDP_REPO_DIR` | `/app/repo` | supervisor | Runtime clone of `GITHUB_REPO` |
| `TCPUDP_SERVER_BIN` | `$TCPUDP_REPO_DIR/server` | supervisor | Path to the `server` binary; override to dry-run against a local build |
| `TCPUDP_INFO_DIR` | `github_run` | supervisor, `trigger_render.sh` | Where `cloudflare.sh` / `run_info.json` are written |
| `SUPERVISE_INTERVAL` | `5` | supervisor | Seconds between liveness checks |
| `TUNNEL_START_TIMEOUT` | `60` | supervisor | Seconds to wait for a tunnel hostname |
| `PUBLISH` | `true` | supervisor | `false` disables all git writes (local testing) |
| `RENDER_HEALTH_URL` | `https://tcpudp-render.onrender.com/healthz` | `trigger_render.sh` | Where to wake and query the service |

Additionally, these exist as **test seams** and all have working defaults, so
production never sets them: `TCPUDP_STATE_DIR` (`/run/tcpudp`),
`GITHUB_REMOTE_URL`, `GITHUB_PUSH_URL`, `KEEPALIVE_READY_FILE`,
`CF_EDGE_IPS`, `PING_COUNT` (5), and `HOSTS_FILE` (`/etc/hosts`).

## Detailed design

### `render/render.yaml`

```yaml
services:
  - type: web
    name: tcpudp-render
    runtime: docker
    rootDir: render              # service root == this folder; makes the folder portable
    dockerfilePath: ./Dockerfile # relative to rootDir
    dockerContext: .             # relative to rootDir
    plan: free                   # 0.1 CPU / 512 MB
    region: oregon
    branch: run
    autoDeployTrigger: 'off'     # CRITICAL — see Gotchas
    healthCheckPath: /healthz
    envVars:
      - key: TCPUDP_RELEASE
        value: v1.1.16
      - key: TCPUDP_SERVER_PORT
        value: "7001"
      - key: GITHUB_REPO
        value: xiguichen/tcpudp
      - key: GITHUB_PUSH_BRANCH
        value: run
      - key: SUPERVISE_INTERVAL
        value: "5"
      - key: GITHUB_PAT
        sync: false              # set in the dashboard, never committed
```

The blueprint path is set when the Blueprint is created in the dashboard
(`render/render.yaml`). `rootDir: render` means the build context is this folder
and nothing outside it is visible to the service.

### `render/Dockerfile`

```dockerfile
FROM ubuntu:24.04

ARG CLOUDFLARED_VERSION=2026.7.1

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl git jq python3 iproute2 \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL -o /tmp/cloudflared.deb \
        "https://github.com/cloudflare/cloudflared/releases/download/${CLOUDFLARED_VERSION}/cloudflared-linux-amd64.deb" \
 && apt-get update && apt-get install -y /tmp/cloudflared.deb \
 && rm -f /tmp/cloudflared.deb \
 && cloudflared --version

COPY render_supervisor.sh /usr/local/bin/render_supervisor.sh
COPY keepalive.py         /usr/local/bin/keepalive.py
RUN chmod +x /usr/local/bin/render_supervisor.sh

EXPOSE 10000
CMD ["/usr/local/bin/render_supervisor.sh"]
```

No CMake: the `server` binary is fetched from the GitHub release tarball at
**runtime**, not baked into the image. Moving to a new release is then an
env-var change plus a redeploy, with no image rebuild — and the binary is
byte-identical to the one `run.yml` uses.

`net.sh` is deliberately **not** copied into the image. Edge-IP probing and
`/etc/hosts` pinning are a client-side concern; the supervisor has no use for
it, and shipping it would put an unused file in the runtime image. Only
`render/trigger_render.sh` sources it, from the checkout on the Mac.

### Runtime repository checkout

`rootDir` means the repo is *not* available to the running container, so the
supervisor clones it itself. The repo is public, so this needs no credentials:

```
git clone --branch "$GITHUB_PUSH_BRANCH" --depth 50 \
    "https://github.com/$GITHUB_REPO.git" "$TCPUDP_REPO_DIR"
```

### `render/keepalive.py`

Stdlib-only HTTP server. Single responsibility: report supervisor state.

- Binds `0.0.0.0:$PORT` (default `10000`).
- `GET /healthz` and `GET /` → `200` with:

  ```json
  {
    "status": "ok",
    "hostname": "xxxx-yyyy.trycloudflare.com",
    "port": 7001,
    "published": true,
    "source": "render",
    "updated": "2026-09-27T12:34:56Z"
  }
  ```

- `hostname` is `null` until the tunnel is up — this is the signal the Mac side
  polls on.
- Any other path → `404`.
- Reads `/run/tcpudp/tunnel.json`; a missing or unparseable file yields
  `hostname: null` rather than an error, so the endpoint always answers 200 and
  Render's health check never flaps.

The supervisor writes `tunnel.json`; the keepalive only reads it. Neither shares
state with the other in-process.

### `render/render_supervisor.sh`

PID 1. One function per job:

| Function | Job |
|---|---|
| `log` / `die` | Timestamped logging to stdout (Render captures it) |
| `preflight` | Assert `server`, `cloudflared`, `python3` exist. **Warn, do not die**, if `GITHUB_PAT` is unset |
| `ensure_repo` | Clone `$TCPUDP_REPO_DIR` if `.git` is absent |
| `fetch_server` | Download and extract the release tarball if the binary is missing |
| `start_server` | `nohup server`; record PID; wait for `127.0.0.1:$TCPUDP_SERVER_PORT` to listen |
| `start_keepalive` | `nohup python3 keepalive.py`; record PID |
| `start_tunnel` | Kill any prior `cloudflared`, truncate its log, relaunch `tunnel --url tcp://127.0.0.1:$TCPUDP_SERVER_PORT`, poll up to `TUNNEL_START_TIMEOUT` for `https://<host>.trycloudflare.com`, write `tunnel.json` |
| `publish` | Write the info files, then `commit` → `pull --rebase` → `push` |
| `supervise_loop` | Every `SUPERVISE_INTERVAL` seconds: restart a dead `server`, restart a dead `cloudflared`, re-publish if the hostname changed |

Files written by `publish`, in the **exact formats `run.yml` uses** so that
`run_github.sh` and `trigger_run.sh` are unaffected:

- `$TCPUDP_INFO_DIR/cloudflare.sh` and `.bat` —
  `cloudflared access tcp --url tcp://localhost:7001 --hostname <host>`
- `cloudflare.sh` / `cloudflare.bat` at the clone root — same content
- `$TCPUDP_INFO_DIR/run_info.json` — `{"hostname","timestamp","port","source":"render"}`
- `$TCPUDP_INFO_DIR/render_info.json` — `{"region","city","country","url","instance"}`,
  best-effort from `curl ipinfo.io`, non-fatal on failure. Deliberately *not*
  named `region.json`, so the Render path cannot confuse `trigger_run.sh`'s
  GitHub-runner region check.

**Publishing rules**

- Commit only when the hostname differs from what is already published, so the
  branch is not spammed.
- On a fresh container, publish **unconditionally** — a wiped filesystem means we
  cannot know what is already published.
- Commit message contains `Auto-update` so that commit-polling logic recognises
  it: `Auto-update cloudflare tunnel info (render)`.
- Push uses an inline token URL and never `git remote add`, so the PAT is not
  written into `.git/config`:
  `git push "https://x-access-token:${GITHUB_PAT}@github.com/${GITHUB_REPO}.git" "HEAD:$GITHUB_PUSH_BRANCH"`
- If `GITHUB_PAT` is unset, write the info files but skip the commit and push,
  log a warning once, set `published: false`, and keep serving. The tunnel and
  `/healthz` still work; the Mac side compensates. (`PUBLISH=false` is
  different: it skips the file writes too, and exists only for local testing.)
- The PAT is never echoed to logs.
- `SIGTERM`/`SIGINT` trap kills the child processes and exits 0, so Render's
  shutdown stays clean.

### `render/trigger_render.sh` (Mac)

The Render analogue of `trigger_run.sh`. Run as `./render/trigger_render.sh`.
Steps:

1. **Preflight** — `curl` present; ensure the checkout is on the push branch;
   `git pull origin "$GITHUB_PUSH_BRANCH"`.
2. **Wake** — poll `GET $RENDER_HEALTH_URL` every 5s until it returns JSON with a
   non-null `hostname`, or `--timeout` (default 300s, covering the ~60s spin-up
   plus the ~60s tunnel start). Print a dot per attempt.
3. **Reconcile** — compare the live hostname against `$TCPUDP_INFO_DIR/cloudflare.sh`.
   If they differ, poll `git fetch origin "$GITHUB_PUSH_BRANCH"` for up to 120s
   waiting for the supervisor's commit.
4. **Fall back** — if the branch is *still* stale, write the live hostname into
   the local (uncommitted) `cloudflare.sh` and warn. This keeps `run_github.sh`
   working even when the PAT is missing or the push failed.
5. **Pin DNS** — `pin_tunnel_hostname` from `net.sh`.
6. **Handoff** — print the hostname and `Next step: ./run_github.sh`.

Options: `--health-url URL`, `--timeout SECONDS`, `--no-wait`.

### `render/net.sh`

Sourced, not executed. Exports:

- `probe_best_edge_ip` — pings the candidate Cloudflare edge IPs in parallel,
  echoes `<ip> <avg_rtt_ms>` for the fastest reachable one.
- `pin_tunnel_hostname <bare-host>` — idempotently pins the host in
  `/etc/hosts`, printing what it did.

## Gotchas this design exists to handle

1. **Auto-deploy loop.** The service pushes to `run`. If Render auto-deploys on
   push to `run`, you get push → redeploy → new tunnel → push, forever. The
   Blueprint default is `autoDeployTrigger: commit`, so `'off'` must be explicit.
2. **Spindown wipes the filesystem.** The supervisor's git checkout is gone
   after a sleep, so it must clone at startup and publish unconditionally on a
   fresh container.
3. **`rootDir` hides the rest of the repo.** By design, but it means the
   supervisor cannot read `render.yaml`, `net.sh`, or anything else at runtime —
   everything it needs must be either in the image or cloned.
4. **Last writer wins.** If the GitHub Actions tunnel and the Render tunnel are
   both running, they both write `$TCPUDP_INFO_DIR/cloudflare.sh`. The `source`
   field in `run_info.json` identifies which published last. Do not run both at
   once.
5. **Reserved ports.** `18012`, `18013` and `19099` are reserved by Render;
   `7001` is not, so it is safe as a private listener.
6. **`x-access-token`.** GitHub's basic-auth username for a PAT is literally
   `x-access-token`, not the account name.
7. **Region is immutable.** Render does not allow changing `region` after
   creation, so pick Oregon deliberately.

## Judgment calls

- **One self-contained folder.** All six files live under `render/` and touch
  nothing outside it, so the folder can be moved to a different repository and
  re-pointed via env vars alone. `net.sh` is inside the folder rather than in a
  new top-level `lib/`, which would have been the tidier-looking choice in this
  repo but would have broken the "lift the folder out" property.
- **Extract DNS pinning to `net.sh` for the new script only.**
  `trigger_run.sh` and `trigger_vpn2.sh` each contain a byte-identical ~40-line
  copy of the edge-IP probing and `/etc/hosts` pinning routine, and the new
  script would be the third copy. I extract it and use it from
  `render/trigger_render.sh`, but deliberately leave the two working scripts
  alone rather than risk breaking working code for an unrelated cleanup.
  Migrating them is an optional follow-up.
- **Download the release tarball rather than building from `src/`.** Matches
  `run.yml`, keeps the image free of a C++ toolchain, keeps the build context
  tiny, and makes builds fast. The trade-off is that a new release requires a
  Render redeploy.
- **Web Service rather than Background Worker.** A background worker is
  semantically tidier, but on the free tier it sleeps with no public URL to
  poke, leaving only *Manual Deploy* in the dashboard. A web service's public
  URL is what makes the free tier usable at all.

## Testing

**Static**
- `bash -n` on `render_supervisor.sh`, `trigger_render.sh`, `net.sh`.
- `python3 -m py_compile render/keepalive.py`.
- `shellcheck` on all three shell files (advisory; fix real findings).
- Validate `render.yaml` against `https://render.com/schema/render.yaml.json`
  (or `render blueprints validate render.yaml` with the Render CLI).

**Local, no Render**
- `keepalive.py` with a hand-written `tunnel.json`: assert 200 + correct JSON
  for `/healthz` and `/`, 404 for `/nope`, and `hostname: null` when the file is
  missing.
- `render_supervisor.sh` in dry-run mode (`PUBLISH=false`, `TCPUDP_SERVER_BIN`
  pointed at a locally built `server`, `TCPUDP_REPO_DIR` at a scratch clone).
  Verify: `server` listens on 7001, `/healthz` reports a hostname, killing
  `server` or `cloudflared` causes a restart within one supervise interval.
- `docker build -t tcpudp-render render/` and run it with `PUBLISH=false`;
  confirm `/healthz` over HTTP and that the tunnel comes up.

**Live, on Render**
1. Confirm region is Oregon and `autoDeployTrigger` is `off`.
2. `curl -fsS https://tcpudp-render.onrender.com/healthz` → hostname non-null.
3. `git log --oneline origin/run` shows an `Auto-update ... (render)` commit;
   `github_run/cloudflare.sh` on the branch matches the live hostname.
4. `nc -z -G 5 127.0.0.1 7001` after `run_github.sh` starts → `CONNECT_OK`.
5. `python3 src/test/client.py` → UDP round trips complete on port 5003.

## Risks

| Risk | Mitigation |
|---|---|
| Quick-tunnel hostname changes on every restart | `trigger_render.sh` re-reads `/healthz`; re-run it per session |
| Free instance sleeps after ~15 min idle | Wake via `/healthz`; budget ~60s at session start |
| Render's Cloudflare edge slow or unreachable from some networks | `/healthz` is low-bandwidth and retry-tolerant; the UDP path goes through the tunnel and the existing `/etc/hosts` pinning |
| 512 MB / 0.1 CPU free plan | Fine for a single test client; the server is thread-per-connection, so not for load |
| PAT mis-scoped | `contents: write` on `xiguichen/tcpudp`; supervisor degrades gracefully and the Mac side falls back to the live hostname |
| Both tunnels running at once | Documented as last-writer-wins; `source` field disambiguates |

## Rollout

1. Commit and push this spec, then the plan and implementation, to `run`.
2. Render dashboard → **New → Blueprint** → select `xiguichen/tcpudp`, set the
   blueprint path to `render/render.yaml`, branch `run` → apply.
3. Add the `GITHUB_PAT` environment variable in the dashboard (fine-grained
   token, `contents: write` on `xiguichen/tcpudp`).
4. Verify region is Oregon and `autoDeployTrigger` is **off**.
5. `curl -fsS https://tcpudp-render.onrender.com/healthz`
6. `./render/trigger_render.sh && ./run_github.sh`

## Moving to another repository

1. Copy the `render/` folder into the target repo.
2. Point the Render Blueprint at `<target-repo>/render/render.yaml`.
3. Set `GITHUB_REPO`, `GITHUB_PUSH_BRANCH`, `RENDER_HEALTH_URL`, and
   `GITHUB_PAT` to match the target repo.
4. Nothing else changes — the supervisor and the Mac script read all of these
   from the environment.

If the target repo's tunnel-info convention differs, only `publish()` in
`render_supervisor.sh` and step 3 of `trigger_render.sh` need editing; the
supervisor's process management and the keepalive endpoint are independent of
it.

## Alternatives considered

- **Background Worker** — rejected; see *Judgment calls*.
- **Named Cloudflare Tunnel with a fixed hostname** — stable across restarts and
  no git push, but needs a Cloudflare account, a real domain, and a tunnel
  token. Worth revisiting if hostname churn becomes irritating.
- **A Render VPS** — would give a static IP and real UDP, at higher cost.
- **Keep the tunnel in GitHub Actions and only add Render as a second host** —
  does not match the request.
