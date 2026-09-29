# render/ — HTTP proxy on Render

Browsing egress from Render's IP, in front of a Cloudflare Quick Tunnel. The
Mac's existing `cloudflared` command opens a local TCP port; it forwards raw
bytes through the tunnel to Render, where a `tinyproxy` instance answers them as
an HTTP proxy. Nothing else sits in the path — no custom protocol, no UDP, no
WireGuard.

Everything this needs is in this folder. Move the whole folder to another repo
and change `GITHUB_REPO`.

## Using it

```bash
./render/trigger_render.sh
```

One command does everything: wake the instance, pin the hostname to a fast
Cloudflare edge IP in `/etc/hosts`, start `cloudflared` in the background
(listening on `0.0.0.0`), and start a keepalive that keeps `/healthz` warm so a
free-tier sleep cannot kill the tunnel mid-browse. It prints the addresses
when it is done:

```
this Mac:       http://127.0.0.1:8080
other machines: http://192.168.1.23:8080
```

Point your browser (or curl) at the HTTP proxy. On this Mac use the loopback
address; on any other machine on the same LAN use this Mac's IP. Requests exit
the internet from Render's IP either way.

The hostname changes on every free-tier sleep, so re-run `trigger_render.sh`
first whenever the instance has been asleep. The keepalive makes that rare,
but a new hostname on a wake is still guaranteed.

## Why `/healthz` exists

Render's free tier sleeps after ~15 minutes idle and **wipes the filesystem**
when it does. Every wake mints a brand-new `trycloudflare.com` hostname, so the
hostname committed to git is stale almost immediately.

`GET /healthz` is therefore the only authority on the current hostname. It is
also what wakes the instance, and it always answers `200` — with
`"hostname": null` and `"proxy": "starting"` while the tunnel is still
registering — so a caller can tell "still waking" from "broken".

```
$ curl -fsS https://tcpudp.onrender.com/healthz
{"hostname": "abc-def.trycloudflare.com", "port": 8080, "published": false, "source": "render", "proxy": "ready", "updated": "2026-09-28T12:34:56Z", "status": "ok"}
```

`proxy` is the verdict on the port, and the distinction matters:

| Value | Meaning |
|---|---|
| `starting` | The supervisor spawned tinyproxy but the readiness poll has not fired. |
| `ready` | The proxy port is answering. Traffic will flow. |
| `down` | tinyproxy died or failed to listen; the supervisor will keep retrying. |

## How it works

```
browser ─ HTTP proxy ─> 0.0.0.0:8080 (Mac — LAN reachable)
                          │  cloudflared access tcp (raw TCP)
                          ▼
                    Cloudflare edge
                          │
                          ▼
                    Render:8080
                   tinyproxy (0.0.0.0:8080)
                          │
                          ▼
                     the internet
```

On Render, the supervisor (PID 1) runs three things: `tinyproxy`, the
`cloudflared` quick tunnel (which prints the hostname the Mac connects to), and
`keepalive.py` (the `/healthz` endpoint). The tunnel is only useful once the
proxy is already listening, so the proxy starts first.

`tinyproxy` is a single process — no TUN device, no capabilities, no custom
framing. Render routes to its port once it sees it listening, and the Mac's
`cloudflared` reaches that port through Cloudflare's edge.

The generated config (`$TCPUDP_STATE_DIR/tinyproxy.conf`) matters:

- **Binds `0.0.0.0`, not loopback.** The tunnel reaches the container from
  outside.
- **Runs as the unprivileged `tinyproxy` user.** The proxy is reachable by
  anyone who learns the hostname, so a tinyproxy bug should not hand out root.
- **Destinations limited to ports 80 and 443.** Without this the instance is a
  general-purpose relay. This is a deliberate, minimal mitigation only — it is
  **not** authentication, and anyone who learns the hostname can still use the
  proxy for its allowed destinations.

## Do I need a GitHub token?

**No.** Leave `GITHUB_PAT` blank and everything works. The supervisor records the
hostname in its state file either way, `trigger_render.sh` reads it from
`/healthz`, and it writes the hostname into your local
`github_run/cloudflare.sh` itself. You will see a `stale` warning, which is
expected and harmless.

Set `GITHUB_PAT` only if something *other* than this Mac needs the hostname to
land in the repo — a second machine, or CI. Use a fine-grained token with
`contents: write` on this repo only, and nothing else.

## Files

| File | Role |
|---|---|
| `render_supervisor.sh` | Container PID 1. Owns the proxy, the health endpoint and the tunnel; publishes the hostname. |
| `keepalive.py` | Stdlib HTTP server on `$PORT`. Serves the state file as JSON. |
| `trigger_render.sh` | Mac entry point: wake, reconcile, pin DNS, start the tunnel + keepalive on `0.0.0.0`, print the proxy URLs. |
| `net.sh` | Sourced library: edge-IP probing and hosts-file pinning. |
| `render.yaml` | Render Blueprint. |
| `Dockerfile` | Runtime image. |
| `run_tests.sh` | Runs all three test suites. |

## Tests

```bash
./render/run_tests.sh
```

50 tests (9 keepalive, 20 supervisor, 21 trigger/net). No network, no sudo, no
docker: git talks to a throwaway local repo, `/healthz` is a local python server
on `127.0.0.1`, `ping`, `cloudflared` and `pkill` are stubbed on `PATH`,
`tinyproxy` is stubbed on `PATH`, and hosts-file writes are redirected to a
sandbox file.

## Reading the proxy's log

Render discards the container filesystem on every free-tier sleep and gives no
shell to read it, so a log written only to a file inside the container is a log
nobody can read after the fact. The supervisor therefore pumps tinyproxy's
output onto its own stdout, one line per record, prefixed `proxy: `:

```
2026-09-28T14:03:35Z proxy is accepting on 0.0.0.0:8080 (pid 44)
2026-09-28T14:07:02Z proxy: CONNECT   example.com:443
2026-09-28T14:07:03Z proxy: CONNECT   cdn.example.com:443
```

Lines with no `proxy: ` prefix come from the supervisor; lines with it come from
tinyproxy. `$TCPUDP_STATE_DIR/proxy.log` also holds the same lines for the live
session.

## Configuration

All optional; the defaults are the working values.

| Variable | Default | Used by |
|---|---|---|
| `GITHUB_REPO` | `xiguichen/tcpudp` | supervisor |
| `GITHUB_PUSH_BRANCH` | `run` | supervisor, trigger |
| `GITHUB_PAT` | *(unset)* | supervisor — optional, see above |
| `TCPUDP_PROXY_PORT` | `8080` | supervisor, trigger |
| `TCPUDP_PROXY_BIND` | `0.0.0.0` | supervisor |
| `TCPUDP_PROXY_USER` | `tinyproxy` | supervisor |
| `TCPUDP_LAN_IP` | *(auto-detected)* | trigger — address printed for other machines |
| `TCPUDP_RUN_DIR` | `~/.tcpudp` | trigger — runtime pids (`cloudflared.pid`, `keepalive.pid`) and logs (`cloudflared.log`, `keepalive.log`), so re-running replaces rather than stacks daemons |
| `KEEPALIVE_INTERVAL` | `180` | trigger — seconds between `/healthz` keepalive hits; `0` disables |
| `START_TIMEOUT` | `20` | trigger — half-seconds to wait for the tunnel listener |
| `TCPUDP_REPO_DIR` | `/app/repo` | supervisor |
| `TCPUDP_INFO_DIR` | `github_run` | both |
| `TCPUDP_STATE_DIR` | `/run/tcpudp` | supervisor |
| `SUPERVISE_INTERVAL` | `5` | supervisor |
| `TUNNEL_START_TIMEOUT` | `60` | supervisor |
| `PUBLISH` | `true` | supervisor |
| `RENDER_HEALTH_URL` | `https://tcpudp.onrender.com/healthz` | trigger |

Test seams, also defaulted: `POLL_INTERVAL`, `RECONCILE_TIMEOUT`,
`HEALTH_TIMEOUT`, `HOSTS_FILE`, `CF_EDGE_IPS`, `PING_COUNT`,
`KEEPALIVE_BIND`.

## Gotchas

- **`autoDeployTrigger: 'on'` — every push to `run` builds**, and the new
  instance is live a few minutes later. Safe only while `GITHUB_PAT` stays
  blank: a blank PAT makes the supervisor write the info files locally without
  ever committing or pushing them, so nothing but our pushes can trigger a
  build. Setting a PAT brings the loop back — each publish would redeploy, wipe
  the filesystem, mint a new hostname, and publish again, forever.
- **Region cannot be changed after creation.** It is set to Oregon because
  `run.yml` blacklists Virginia. Pick deliberately.
- **`rootDir: render`** hides the rest of the repo from the build. That is what
  makes this folder self-contained, and it is also why the supervisor clones the
  repo itself at runtime.
- **Do not run the GitHub Actions tunnel and the Render tunnel at once.** Both
  write `github_run/cloudflare.sh`; last writer wins. The `source` field in
  `run_info.json` says which one wrote it.
- **`github_run/cloudflare.sh` is a command, not a config file.** `run_github.sh`
  sources it, so it must stay a single `cloudflared access tcp ...` line. The
  script writes it with `--url tcp://0.0.0.0:8080`, and that is also exactly
  what it runs, so the tunnel listener binds every interface. **Any machine on
  the same LAN can then use the proxy** with this Mac's IP as the server;
  macOS prompts to allow `cloudflared` incoming connections the first time
  (needed for the LAN address, not for `127.0.0.1`).
- **The instance is kept awake for as long as the tunnel runs.** Render's free
  tier sleeps after ~15 minutes idle and a wake wipes the filesystem + mints a
  new hostname. `trigger_render.sh` therefore runs a background keepalive that
  hits `/healthz` every `KEEPALIVE_INTERVAL` (default 180 s) and stops the
  moment the tunnel process dies. Each poll also carries the hostname: if it
  rotates without the instance sleeping (a redeploy), the keepalive restarts
  the tunnel against the new hostname and rewrites `cloudflare.sh` itself, so
  the client keeps working without re-running the script. Empty "cold start"
  answers are ignored - they mean "not ready yet", not "changed".
- **The proxy is unauthenticated.** The tunnel hostname is printed by
  `trigger_render.sh` and lives in `render/`-generated files on the `run`
  branch, and the listener binds `0.0.0.0` so the whole LAN could point its
  browsers at it. Treat it as private to your machine and network, and don't
  stand the tunnel up in public for long.