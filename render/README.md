# render/ — UDP/TCP server on Render

Self-contained deployment of the same `tcpudp` server binary that
`.github/workflows/run.yml` runs, hosted on Render's free tier behind a
Cloudflare Quick Tunnel.

Everything this needs is in this folder. Move the whole folder to another repo
and change `GITHUB_REPO`.

## Using it

```bash
./render/trigger_render.sh    # wake the instance, sync the hostname, pin DNS
./run_github.sh               # unchanged; sources github_run/cloudflare.sh
```

## Why `/healthz` exists

Render's free tier sleeps after ~15 minutes idle and **wipes the filesystem**
when it does. Every wake mints a brand-new `trycloudflare.com` hostname, so the
hostname committed to git is stale almost immediately.

`GET /healthz` is therefore the only authority on the current hostname. It is
also what wakes the instance, and it always answers `200` — with
`"hostname": null` while the tunnel is still registering — so a caller can tell
"still waking" from "broken".

```
$ curl -fsS https://tcpudp-render.onrender.com/healthz
{"status":"ok","hostname":"abc-def.trycloudflare.com","port":7001,"published":false,"source":"render","updated":"2026-09-27T12:34:56Z"}
```

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
| `render_supervisor.sh` | Container PID 1. Owns the server, the health endpoint and the tunnel; publishes the hostname. |
| `keepalive.py` | Stdlib HTTP server on `$PORT`. Serves the state file as JSON. |
| `trigger_render.sh` | Mac entry point: wake, reconcile, pin DNS, hand off. |
| `net.sh` | Sourced library: edge-IP probing and hosts-file pinning. |
| `render.yaml` | Render Blueprint. |
| `Dockerfile` | Runtime image. |
| `run_tests.sh` | Runs all three test suites. |

## Tests

```bash
./render/run_tests.sh
```

34 tests. No network, no sudo, no docker: git talks to a throwaway local repo,
`/healthz` is a local python server on `127.0.0.1`, `ping` is stubbed on `PATH`,
and hosts-file writes are redirected to a sandbox file.

## Configuration

All optional; the defaults are the working values.

| Variable | Default | Used by |
|---|---|---|
| `GITHUB_REPO` | `xiguichen/tcpudp` | supervisor |
| `GITHUB_PUSH_BRANCH` | `run` | supervisor, trigger |
| `GITHUB_PAT` | *(unset)* | supervisor — optional, see above |
| `TCPUDP_RELEASE` | `v1.1.16` | supervisor |
| `TCPUDP_SERVER_PORT` | `7001` | both |
| `TCPUDP_REPO_DIR` | `/app/repo` | supervisor |
| `TCPUDP_INFO_DIR` | `github_run` | both |
| `TCPUDP_STATE_DIR` | `/run/tcpudp` | supervisor |
| `SUPERVISE_INTERVAL` | `5` | supervisor |
| `TUNNEL_START_TIMEOUT` | `60` | supervisor |
| `PUBLISH` | `true` | supervisor |
| `RENDER_HEALTH_URL` | `https://tcpudp-render.onrender.com/healthz` | trigger |

Test seams, also defaulted: `POLL_INTERVAL`, `RECONCILE_TIMEOUT`,
`HEALTH_TIMEOUT`, `HOSTS_FILE`, `CF_EDGE_IPS`, `PING_COUNT`,
`KEEPALIVE_BIND`.

## Upgrading the server

Change `TCPUDP_RELEASE` in `render.yaml`, then redeploy. The release tarball is
fetched at container start, so no rebuild is needed.

## Gotchas

- **`autoDeployTrigger: 'off'` is load-bearing.** Left on, each publish triggers
  a redeploy, which wipes the filesystem, which mints a new hostname, which
  publishes again — forever.
- **Region cannot be changed after creation.** It is set to Oregon because
  `run.yml` blacklists Virginia. Pick deliberately.
- **`rootDir: render`** hides the rest of the repo from the build. That is what
  makes this folder self-contained, and it is also why the supervisor clones the
  repo itself at runtime.
- **Do not run the GitHub Actions tunnel and the Render tunnel at once.** Both
  write `github_run/cloudflare.sh`; last writer wins. The `source` field in
  `run_info.json` says which one wrote it.
- **`github_run/cloudflare.sh` is a command, not a config file.** `run_github.sh`
  sources it, so it must stay a single `cloudflared access tcp ...` line.

## Deploying to a different repository

1. Copy this folder into the new repo.
2. Set `GITHUB_REPO` in `render.yaml` to `owner/name`, and the default in
   `render_supervisor.sh` to match.
3. Point the new repo's release at `TCPUDP_RELEASE`.
4. Create the Blueprint against the new repo.

Nothing outside this folder needs to change.
