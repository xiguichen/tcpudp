# Render HTTP Proxy — Design

Replaces the WireGuard exit design for the Render tunnel host. WireGuard cannot run
on Render; an HTTP proxy can.

Supersedes the WireGuard section of `2026-09-27-render-udp-server-deploy-design.md`.

## Goal

Let the Mac browse the web with egress from Render's IP, through the existing
`tcpudp` tunnel, so web traffic originates from Render rather than from home.

The user-facing result is one command on the Mac that prints a proxy URL, and a
browser or `curl` pointed at it.

## Why not WireGuard

Measured on the live service, not inferred. `/healthz` reported:

```
wireguard_reason: tun=absent net_admin=missing: RTNETLINK answers: Operation not permitted
```

- `/dev/net/tun` is **absent** from the container.
- `CAP_NET_ADMIN` (capability bit 12) is **missing** from the effective set.
- `CAP_MKNOD` (bit 27) is also **absent**, so the device node cannot be created
  either.
- Capabilities cannot be added from inside a container; the bounding set is fixed
  by the runtime, and Render's web service spec exposes no way to grant them.

`RTNETLINK answers: Operation not permitted` on `ip link add` is the kernel's
`CAP_NET_ADMIN` refusal.

`wg-quick` on Linux has no userspace fallback — the source has
`cmd "${WG_QUICK_USERSPACE_IMPLEMENTATION:-wireguard-go}" utun` only in the macOS
variant, which also carries the Darwin-only `netstat -nr -f inet` and `TABLE=auto`
handling. Render took the kernel path, visible as the traced command
`[#] ip link add wg0 type wireguard`. `wireguard-go` and `boringtun` on Linux both
create a TUN device and therefore need the same capability.

Conclusion: Render can never terminate a WireGuard tunnel. An HTTP proxy needs
no TUN device and no capabilities, only a socket.

## Verified constraints from the released binary

All against release commit `e305a72` (v1.1.16), not the stale `src/` on this branch.

| Constraint | Evidence | Consequence |
|---|---|---|
| The VC's only egress is `sendto()` on a UDP socket | `Server.cpp:293` hardcodes `inet_addr("127.0.0.1")` | The relay target must be on loopback, so the proxy is reached through a UDP→TCP adapter |
| The server never opens a TCP connection to a target | no `connect()` anywhere in `Server.cpp` | No configuration points the VC at a TCP proxy directly |
| Only two server options exist | `ServerConfiguration` has `portNumber` and `udpTargetPort`; `main.cpp` accepts only `--log-level`, `--port`, `--udp-target-port` | The adapter port is configured via `--udp-target-port` |
| Payload ceiling | `VC_MAX_DATA_PAYLOAD_SIZE = 2000` | Frames are ≤2000 bytes including the 10-byte header |
| Datagram boundaries survive | client does `RecvUdpData` → `vc->send`; server does VC receive callback → one `sendto` | One framed datagram is one VC packet |
| Delivery is in order, duplicates dropped | reorder thread skips `item.messageId < nextMessageId` | No reordering layer needed |
| **Packets can be silently dropped** | `Reorder timeout ({}ms): skipping messageIds {}-{}, advancing to {}` | **A per-connection sequence number is mandatory** — see below |

### The loss trap

When a gap persists past `reorderTimeoutMs`, the VC advances `nextMessageId` past
the missing packets and logs a warning. Carrying a TCP byte stream over that, a
dropped packet is indistinguishable from data: the hole is spliced in as if it
were content, and the failure surfaces as a corrupt page or a protocol error far
from its cause.

So every frame carries a per-direction, per-connection `seq`. A gap is detected on
receive and answered with `RESET`, which closes the connection. The browser retries.
That is strictly better than silent corruption, and it is the reason `seq` exists.

## Non-goals

- Transparent or system-wide routing. There is no TUN device, so nothing can be
  routed without being configured explicitly. This is a proxy you point a client
  at, not a VPN.
- UDP, QUIC or DNS carriage. An HTTP proxy carries TCP only.
- Non-TCP traffic of any kind.
- Replacing or modifying the existing `run/`, `run2/`, `run3/` tunnel hosts.
- Any change to `run_github.sh` or `github_run/cloudflare.sh`.

## Architecture

```
browser / curl -x 127.0.0.1:8889
  → mac_proxy_bridge.py          TCP conns → framed datagrams on UDP 5003
  → udp_client                   existing, unchanged
  → cloudflared access tcp       existing, unchanged
  → server --udp-target-port=41800   existing binary, one flag added
  → proxy_adapter.py             demux → one TCP conn per conn_id to 127.0.0.1:8888
  → tinyproxy                    existing package, loopback only
  → internet, egressing from Render's IP
```

The `server` binds `INADDR_ANY` on `--port` and relays the virtual channel to
`127.0.0.1:--udp-target-port`. The adapter owns that port. Nothing else on the host
reads it.

## Framing

A 10-byte header carried as the VC data payload.

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `version` | `0x01`. Anything else is dropped and counted |
| 1 | 1 | `flags` | bit0 OPEN, bit1 DATA, bit2 CLOSE, bit3 RESET |
| 2 | 2 | `conn_id` | little-endian, 1–65535, never 0 |
| 4 | 4 | `seq` | little-endian, per-direction per-connection, starts at 0 |
| 8 | 2 | `length` | little-endian payload byte count |
| 10 | … | `payload` | ≤1400 by default; 1990 absolute ceiling |

All multi-byte fields are little-endian on both ends. Both programs are ours, so
the format is internal and needs no version negotiation beyond the `version` byte.

### Flags

- **OPEN** — `length` 0. Sent when the Mac bridge accepts a TCP connection. The
  adapter opens the matching TCP connection to tinyproxy on receipt, so no upstream
  connection exists until a client actually wants one.
- **DATA** — `length` > 0.
- **CLOSE** — `length` 0. Half-close in that direction.
- **RESET** — `length` 0. Sent on a `seq` gap or an upstream failure. Both ends
  tear the connection down; no attempt is made to resynchronise a stream that has
  already lost bytes.

### Multiplexing

- Up to 128 concurrent `conn_id`s, configurable. Beyond that, new connections are
  refused and the refusal is logged and counted. Browsers open many connections at
  once, so a low ceiling hangs pages; 128 is generous for a browser.
- `conn_id` is allocated monotonically and wraps at 65535, skipping ids still in use.
- `conn_id` 0 is reserved as invalid so a zeroed frame is never mistaken for a
  connection.

## Components

### `render/mac_proxy_bridge.py` (Mac)

Listens TCP on `127.0.0.1:8889`. For each accepted connection, allocates a
`conn_id`, sends `OPEN`, then wraps every write as a `DATA` frame in a datagram to
`127.0.0.1:5003`. Reads returning datagrams, demuxes by `conn_id`, verifies `seq`,
and writes the payload to the owning socket. Sends `CLOSE` when the browser closes.

Binds loopback only, matching the existing `udp_client` and `trigger_render.sh`
convention, so it does not trigger macOS's incoming-connections prompt.

### `render/proxy_adapter.py` (Render)

Binds UDP `127.0.0.1:41800`. The server's own UDP socket binds `127.0.0.1:0`, so
the adapter learns the server's ephemeral source address from the first datagram it
receives and sends replies there. It does not verify the source beyond loopback.

Maintains one TCP connection to `127.0.0.1:8888` per `conn_id`. Reads from those
sockets, frames the bytes with the same `conn_id` and its own outbound `seq`, and
sends them to the learned source.

Frame parsing and connection state are separate units so the framing can be tested
without any sockets.

### `render/tinyproxy` (Render)

Apt package, listening on `127.0.0.1:8888` only, cache and upstream disabled.
Chosen over a hand-written proxy because the only genuinely new logic here is the
framing and the demux, and HTTP is the part most likely to have subtle bugs:
absolute-URI requests, keep-alive and chunked encoding are where hand-rolled
proxies break, and a browser finds those immediately.

### `render/run_proxy_mac.sh` (Mac)

The single command the user runs. Sequentially:

1. `trigger_render.sh` — resolves the live hostname, writes DNS pinning, returns
   the health document.
2. `cloudflared access tcp --url tcp://localhost:7001 --hostname <host>`.
3. `udp_client`, pointed at `127.0.0.1:5003`.
4. `mac_proxy_bridge.py` on `127.0.0.1:8889`.
5. A loopback pinger against `/healthz` every 60s.
6. Print `http://127.0.0.1:8889` and wait.

Entirely standalone. It does not read, write or invoke `run_github.sh`,
`github_run/cloudflare.sh`, `run/`, `run2/` or `run3/`.

The pinger exists because a free-tier container sleeps after roughly 15 minutes of
inactivity, and `keepalive.py` only *serves* `/healthz` — nothing polls it. A cold
container costs roughly 30–60s on the first request. The pinger runs only while the
script runs, so an idle proxy is not held warm at the cost of the container's
always-on minutes.

Cleanup mirrors the existing hazard notes: the script must not `pkill` a
`cloudflared` it did not start, and port 7001 may be owned by the user's own
`cloudflared`. It kills only what it recorded as its own PIDs.

## Configuration surface

Every variable has a default, so the service runs with an empty environment. None
is set on Render's dashboard; all are set on the Mac script and passed through.

| Variable | Default | Side | Meaning |
|---|---|---|---|
| `PROXY_ADAPTER_PORT` | `41800` | Render | UDP port the adapter binds and the server is pointed at via `--udp-target-port`. One variable, so the two cannot drift |
| `PROXY_LISTEN_PORT` | `8888` | Render | tinyproxy's loopback listen port |
| `PROXY_MAX_CONNS` | `128` | Render | Concurrent `conn_id`s allowed before new connections are refused |
| `PROXY_IDLE_TIMEOUT` | `30` | Render | Seconds a connection may go without a frame before the adapter resets it |
| `MAC_PROXY_PORT` | `8889` | Mac | `mac_proxy_bridge.py`'s loopback listen port; the port clients are pointed at |
| `MAC_PROXY_UPSTREAM_PORT` | `5003` | Mac | UDP port the bridge sends datagrams to, where `udp_client` listens |
| `MAC_PROXY_PING_INTERVAL` | `60` | Mac | Seconds between `/healthz` pings that hold the container warm |

`PROXY_IDLE_TIMEOUT` is a Render-side variable but governs a Mac-side symptom: the
bridge's sends succeed into a dead container, so the wait has to end somewhere. It
is enforced by the adapter when it is alive, and by the bridge's own read timeout
when it is not, which is why both default to the same 30s.

## Changes to existing files

| File | Change |
|---|---|
| `render/render_supervisor.sh` | `start_wireguard` → `start_proxy_adapter`; `--udp-target-port` taken from the adapter's port, not a WireGuard config; `wg-quick down` removed from cleanup; `wireguard`/`wireguard_reason` replaced by `proxy*` in state |
| `render/Dockerfile` | add `tinyproxy` |
| `render/render.yaml` | drop `WIREGUARD_CONFIG` and `WIREGUARD_REQUIRED`; add the Render-side variables below |
| `render/keepalive.py` | `DEFAULTS` gains the `proxy*` keys |
| `render/README.md` | rewrite the WireGuard section as the proxy design |

### Removed

The WireGuard startup path, `WIREGUARD_CONFIG`, `WIREGUARD_REQUIRED`, the
`wireguard` and `wireguard_reason` state fields, and the derivation of
`--udp-target-port` from a WireGuard `ListenPort`.

It is dead code on Render — the capability will never be granted — and leaving it
means two startup paths where one is permanently broken. Recoverable from git
history at `59a39d3` if Render's platform ever changes.

## Observability

`/healthz` gains:

| Key | Meaning |
|---|---|
| `proxy` | `ready`, `degraded`, `starting`, or `absent` |
| `proxy_conns` | currently open connections |
| `proxy_bytes_in` / `proxy_bytes_out` | cumulative payload bytes |
| `proxy_resets` | `seq` gaps plus adapter errors |
| `proxy_last_error` | short string, most recent |

`proxy` is `degraded` when tinyproxy cannot be reached, so the health document
distinguishes "the adapter is up but cannot proxy" from "nothing is running".

This is not decoration. The two failures that cost the most time in this project
were both silent: the server's own log going only to a file Render discards, and
`sendto()` to a port with no listener reporting success.

## Failure handling

1. **VC reorder-timeout drop** — `seq` gap detected on receive. Send `RESET`, close
   the connection, count it. The browser retries.
2. **Container asleep or adapter dead** — the Mac's `sendto` still succeeds, because
   UDP to a dead port fails silently. The bridge must therefore time out any
   connection that goes `PROXY_IDLE_TIMEOUT` seconds without a frame and close it.
   Without this the browser hangs indefinitely.
3. **Adapter cannot reach tinyproxy** — connection refused. Send `RESET`, record
   `proxy_last_error`, set `proxy` to `degraded`.
4. **Destination unreachable from Render** — tinyproxy returns a normal HTTP error
   which flows back as ordinary payload. The browser displays it. Correct behaviour,
   not an error path.
5. **Unknown `version`, `conn_id` 0, or a truncated frame** — drop, count, log once.
   Never fatal; a malformed frame must not take the adapter down.
6. **Concurrency ceiling reached** — refuse the new connection, count it, log it.
7. **Upstream TCP connection closes unexpectedly** — send `CLOSE` to the Mac so the
   browser sees a clean end of stream rather than a hang.

## Judgment calls

- **Dumb adapter plus a real proxy**, rather than one process doing demux and HTTP
  together. Fewer processes would be tidier, but merging proxy semantics into the
  demuxer makes both harder to reason about, and the demux is the part with the real
  risk. The extra loopback hop is free.
- **`seq` is mandatory, not defensive.** Without it the VC's documented drop
  behaviour becomes undetectable stream corruption.
- **Framing is not negotiated.** Both ends are ours, so `version` exists only to
  catch a stale binary talking to a new one.
- **Adapter port is the single source of truth** for `--udp-target-port`, read from
  one variable, so the server and the adapter cannot drift apart. The earlier
  WireGuard-derived version of this had exactly that drift risk.
- **Loopback binds throughout** on both sides, so nothing is exposed beyond the
  tunnel and macOS does not raise the firewall prompt.
- **Idle timeout is required, not optional**, because the failure it prevents is
  silent and unbounded.

## Testing

Deliberately small. The user has directed that the live redeploy is the
verification mechanism for the tunnel itself, and that local mock harnesses are not
worth the time.

Worth testing, because each is fast and carries real risk:

- Frame encode/decode round trip, including a truncated frame, an unknown `version`,
  and `conn_id` 0. Pure functions, milliseconds.
- `seq` gap detection produces `RESET`. The one behaviour the whole design rests on.
- `proxy_adapter.py` demux against a real local TCP echo server: two concurrent
  connections with interleaved traffic must not cross. About two seconds, no mocks.
- `mac_proxy_bridge.py` against the same echo server, round trip.
- `conn_id` exhaustion and wraparound skip-in-use.

Not tested locally: the tunnel path, cloudflared, and Render itself. Those are
verified against the live service after deploy, by watching `proxy` and
`proxy_conns` in `/healthz` while loading a page through the proxy.

## Risks

| Risk | Likelihood | Handling |
|---|---|---|
| Render's Acceptable Use Policy treats a general-purpose proxy as misuse and suspends the service | Real | Free tier, so no financial exposure, but the service can be lost. The user's call; flagged before building |
| Throughput and latency are poor | Certain | 32 TCP connections through `cloudflared`, which is TCP with head-of-line blocking, carrying ≤2000-byte payloads. Usable for browsing, poor for large downloads. A property of the tunnel, not of the proxy |
| VC reorder-timeout drops cause visible connection resets under load | Moderate | The intended, visible failure. Surfaced as `proxy_resets` |
| tinyproxy's default config blocks or filters something | Low | Loopback-only, cache and upstream disabled, no filtering configured |
| Free-tier sleep between uses | Certain | 60s pinger while the script runs; otherwise a 30–60s cold start on first use |

## Rollout

1. Write the spec and plan; get the plan approved.
2. Implement framing, then the adapter, then the bridge, then the supervisor
   changes, then `run_proxy_mac.sh`.
3. Push to `run`, which triggers the Render deploy.
4. Read `/healthz`: expect `proxy: ready`.
5. Run `run_proxy_mac.sh`, then `curl -x http://127.0.0.1:8889 https://ifconfig.me`
   and confirm the address is Render's, not home.
6. Load a real page in a browser through the proxy and watch `proxy_conns` and
   `proxy_bytes_out` move.
7. Confirm the existing `run_github.sh` path still works, untouched.

## Alternatives considered

- **WireGuard on Render** — impossible; see above.
- **boringtun SOCKS5** — still needs a TUN device on Linux, and rejected earlier as
  insufficient for a transparent exit regardless.
- **One Python process doing demux and HTTP CONNECT** — fewer processes, but
  hand-written HTTP. `CONNECT` alone is easy; absolute-URI, keep-alive and chunked
  handling are not, and browsers surface those bugs immediately.
- **SOCKS5 via dante** — a simpler protocol than HTTP, but browsers do not speak
  SOCKS without explicit configuration, and the user asked for something matching
  their existing HTTP proxy.
- **Moving the whole server to a VPS** — strictly better for a VPN, since `wg0`
  could sit beside the server on loopback with no relay and no cloudflared hop. The
  user has no VPS.
- **A loopback UDP relay to a VPS-hosted WireGuard exit** — viable in principle, and
  it needs no capabilities. Rejected only because it still requires a VPS, while
  adding a relay hop that the proxy design does not need.
