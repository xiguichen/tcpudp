# Render HTTP proxy — design

Status: **approved, implemented, verified.** Supersedes the archived draft that
attempted to carry HTTP CONNECT through the tcpudp virtual channel.

## Goal

Browse the web with egress from Render's IP, using the existing Cloudflare
tunnel and an open-source HTTP proxy. Nothing about the tcpudp server /
`udp_client` protocol participates — the goal never needed it.

## Why the earlier design was wrong

`cloudflared access tcp --url tcp://localhost:7001` is a **raw TCP forward**:
whatever connects to port 7001 on the Mac is piped byte-for-byte to port 7001 on
Render. An HTTP proxy is already TCP, so the virtual channel (which exists to
carry *UDP* over a lossy TCP tunnel — the WireGuard path, now dead) was solving
a problem the goal did not have. The elaborate framing/demux/bridge design
(proxy_frame, proxy_adapter, mac_proxy_bridge, seq→RESET, concurrency ceilings)
is archived in git history and must not be re-read as a to-do.

## Design

```
browser ─ HTTP proxy ─> Mac 127.0.0.1:7001
     cloudflared access tcp (raw TCP, already exists)
     Cloudflare edge
     Render:7001
     tinyproxy (0.0.0.0:7001, unprivileged user)
     internet
```

### Render (all in render/)

- `render_supervisor.sh`: still PID 1, but slimmed. Removed: the release-tarball
  download, `start_server`, WireGuard, `--udp-target-port`. Added: `start_proxy`
  which generates `$TCPUDP_STATE_DIR/tinyproxy.conf`, spawns `tinyproxy -d -c`,
  records the pid only once confirmed alive, and reports
  `"proxy":"ready"|"down"` through `/healthz`.
- tinyproxy config: `Listen 0.0.0.0` (Render only routes to wildcard binds),
  `Port 7001`, `User/Group tinyproxy`, `ConnectPort 80`/`ConnectPort 443` only,
  `Syslog Off`, `LogFile "/dev/stdout"`, `LogLevel Info`.
- Proxy output is pumped to the supervisor's stdout with a `proxy: ` prefix and
  mirrored to `$STATE_DIR/proxy.log` — same fix as the server's invisible log.
- `Dockerfile`: + `tinyproxy`, − `wireguard-tools`, − `tar` (no tarball).

### Mac

Nothing new. The tunnel hostname comes from `trigger_render.sh`; the proxy URL
is `http://127.0.0.1:7001`. The existing `run_github.sh` / `udp_client` / run2 /
run3 paths are untouched.

## Decisions and risks

- **Open proxy, unauthenticated** (user's explicit choice). The `run` branch
  publishes the hostname, so anyone reading it can reach the proxy. Ports 80/443
  only is the sole mitigation — accepted risk; Render's free tier may suspend a
  service it considers misused.
- WireGuard removal is recoverable from git history (`59a39d3` and earlier).

## Verification

- `render/run_tests.sh`: 44 tests (9 keepalive, 19 supervisor, 16 trigger/net).
- Live: `/healthz` carries `"proxy":"ready"` and no `wireguard` key;
  `curl -x http://127.0.0.1:7001 https://ifconfig.me` returns Render's IP.