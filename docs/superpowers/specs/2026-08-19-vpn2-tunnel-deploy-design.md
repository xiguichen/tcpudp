# vpn2 Cloudflare Tunnel Deployment — Design

Date: 2026-08-19

## Goal

Run the tcpudp server on the dedicated vpn2 server and expose it through a

Cloudflare Quick Tunnel, mirroring the GitHub Actions flow (`run.yml`) but

without GitHub/git as the transport for tunnel metadata. All operations go

directly to vpn2 over SSH.

The deliverable is one self-contained, idempotent Mac-side script,

`trigger_vpn2.sh`, which sets up the entire Cloudflare tunnel path against

vpn2 and hands off to the user to run the client.

## How the GitHub flow works today (reference)

`run.yml` (on a push to the `run` branch or workflow_dispatch) does:

1. Download the release tarball

   `tcpudp-ubuntu-latest.tar.gz` from the latest release tag.

2. Run the tcpudp server with no arguments (defaults to listening on TCP

   7001; UDP target port defaults to the same port).

3. Start `cloudflared tunnel --url tcp://localhost:7001` (ingress Quick

   Tunnel) and extract the `https://<host>.trycloudflare.com` hostname from

   its log.

4. Write the client-side connect command to `cloudflare.sh` / `cloudflare.bat`

   (`cloudflared access tcp --url tcp://localhost:7001 --hostname <host>`),

   plus `run_info.json` and `region.json`, and commit/push them to the `run`

   branch.

5. Keep a monitor loop alive that restarts server/cloudflared and re-pushes

   the hostname when the quick-tunnel hostname churns.

The Mac-side `trigger_run.sh` polls git for the "Auto-update" commit, checks

the region, pins the tunnel hostname to a low-RTT Cloudflare edge IP in

`/etc/hosts`, and then `run_github.sh` starts the client-side `cloudflared

access` tunnel and the `udp_client`.

## Design for vpn2

### Overview

`trigger_vpn2.sh` (single Mac-side script, no cross-script dependencies):

1. Remote ensure (via SSH to `vpn2`) — server + Cloudflare Quick Tunnel.

2. Fetch tunnel info back (SCP).

3. Pin DNS to the best Cloudflare edge IP on the Mac.

4. Start the client-side `cloudflared access` tunnel in the background.

5. Write the client config.

6. Hand off: print "run `./run/udp_client`".

### Step-by-step

**Step 1 — Remote ensure (executed on vpn2 via SSH)**

- Assert `cloudflared` is installed (`command -v cloudflared`).

- Server binary: if the binary at the configured path does not exist,

  download the release tarball

  (`https://github.com/xiguichen/tcpudp/releases/download/<tag>/tcpudp-ubuntu-latest.tar.gz`),

  extract it, and use its `server` binary. The release tag is a script

  parameter (`--release <tag>`; default: `v1.1.16`, matching `run.yml`).

  The binary path is a script parameter (`--server <path>`); default:

  `~/tcpudp/server`.

- Server process: if the server is not running (no process for the binary and

  nothing listening on TCP 7001), start it with

  `nohup <server> --log-level=INFO` logging to `~/tcpudp/server.log`. If it

  is already running, leave it alone.

- Cloudflare tunnel: if a `cloudflared tunnel --url tcp://localhost:7001`

  process is running, extract its hostname from the log and verify the

  hostname still resolves (via `dig`) and accepts TCP connections (short

  connect probe on port 443). If the process is dead or the hostname is

  stale, kill any old cloudflared and start a fresh

  `nohup cloudflared tunnel --url tcp://localhost:7001 --logfile ~/tcpudp/cloudflared.log`.

  Poll the log every 5 seconds until a new `https://<host>.trycloudflare.com`

  appears (with an overall timeout of ~60s).

- Write results on vpn2 under `~/tcpudp/`:

  - `cloudflare.sh` / `cloudflare.bat` — the client-side command

    `cloudflared access tcp --url tcp://localhost:7001 --hostname <host>`.

  - `run_info.json` — `{"hostname": ..., "timestamp": ..., "port": 7001}`.

- Print the hostname (and the contents of `cloudflare.sh`) to stdout so the

  caller sees the result.

**Step 2 — Fetch back**

- SCP `cloudflare.sh`, `cloudflare.bat`, and `run_info.json` from vpn2 into

  the local `github_run/` directory, and copy `cloudflare.sh`/`.bat` to the

  repo root (mirroring the layout the GH flow produces).

**Step 3 — DNS pin (Mac)**

- Resolve the tunnel hostname and pin it in `/etc/hosts` to the reachable

  Cloudflare edge IP with the lowest average RTT, probing candidate IPs in

  parallel (same approach as `trigger_run.sh`). Remove any previous pinned

  entry for the hostname first.

**Step 4 — Client access tunnel (Mac)**

- Kill any stale `cloudflared access tcp` process from a previous session.

- Start `cloudflared access tcp --url tcp://localhost:7001 --hostname <host>`

  in the background so it stays alive after the script exits.

**Step 5 — Client config (Mac)**

- Write `run/config.json`:

  `{"localHostUdpPort": 5003, "peerTcpPort": 7001, "peerAddress": "127.0.0.1", "clientId": 4}`

  (UDP 5003 avoids colliding with the direct vpn2 mode's 5002).

**Step 6 — Handoff**

- Print the tunnel hostname and: "Tunnel ready. Run `./run/udp_client`."

### Decisions and explicit non-goals

- No region checks / region.json — vpn2 is a dedicated server; the region is

  fixed and GitHub-runner-style retry logic does not apply.

- No persistent monitor loop on vpn2 (unlike `run.yml`'s "Keep Services

  Alive"). Each run of the script re-checks and repairs state.

- Idempotent by construction: re-running reuses running server/cloudflared

  where healthy.

- No code sharing with `run_github.sh` / `trigger_run.sh`; the script is

  self-contained.

- No git operations are used (or required) for the tunnel metadata path.

### Verification

- Run `./trigger_vpn2.sh --server ~/tcpudp/server`.

- Expect: vpn2 has exactly one server process and one cloudflared process

  (not duplicates on re-run), `~/tcpudp/cloudflare.sh` exists on vpn2 and

  `github_run/cloudflare.sh` exists locally with a matching hostname.

- `./run/udp_client` connects (tunnel hostname reachable from the Mac).

- Re-run the script — it should detect the running instances and not start

  duplicates.

- Kill cloudflared on vpn2, re-run — the script restarts it and picks up the

  new hostname.