# Render HTTP Proxy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the Mac browse the web with egress from Render's IP, by carrying framed TCP connections over the existing `tcpudp` virtual channel into a loopback `tinyproxy` on Render.

**Architecture:** Two new Python programs sit either side of the existing tunnel. On the Mac, `mac_proxy_bridge.py` accepts TCP connections from a browser and wraps their bytes in a 10-byte framed header as UDP datagrams on port 5003, where the existing `udp_client` picks them up unchanged. On Render, `proxy_adapter.py` binds the UDP port the server relays to, demultiplexes by connection id into one TCP connection per client to `tinyproxy` on loopback, and frames the replies back. A per-connection `seq` detects the virtual channel's reorder-timeout packet drops and answers them with `RESET` rather than splicing a hole into the stream as if it were data.

**Tech Stack:** Python 3 standard library only (`socket`, `selectors`, `struct`, `json`, `unittest`) — no third-party packages on either side. `tinyproxy` from Debian apt on Render. Bash for the supervisor and the Mac entry script.

**Spec:** `docs/superpowers/specs/2026-09-28-render-http-proxy-design.md`

## File Structure

| File | Responsibility |
|---|---|
| `render/proxy_frame.py` | **Create.** Frame constants, `Frame`, `encode`, `decode`. Pure functions, no I/O, no sockets. Imported by both the adapter and the bridge, so the two can never disagree about the wire format. |
| `render/proxy_adapter.py` | **Create.** Render side. Binds UDP `41800`, demuxes to one TCP connection per `conn_id` on `127.0.0.1:8888`, frames replies, maintains `proxy.json`. |
| `render/mac_proxy_bridge.py` | **Create.** Mac side. Listens TCP `8889`, frames outbound bytes as datagrams to UDP `5003`, unframes replies. |
| `render/run_proxy_mac.sh` | **Create.** The one command the user runs. Orchestration and cleanup only. |
| `render/test_proxy.py` | **Create.** `unittest` for framing, the adapter, and the bridge. Real sockets against a real echo server; no mocks. |
| `render/Dockerfile` | **Modify.** Add `tinyproxy` to the apt list. |
| `render/render.yaml` | **Modify.** Drop `WIREGUARD_CONFIG`/`WIREGUARD_REQUIRED`; add the four `PROXY_*` variables. |
| `render/render_supervisor.sh` | **Modify.** `start_wireguard` → `start_proxy_adapter`; `--udp-target-port` sourced from `PROXY_ADAPTER_PORT`; start `tinyproxy`; drop `wg-quick` from cleanup. |
| `render/keepalive.py` | **Modify.** `DEFAULTS` gains `proxy*`; `read_state` merges `proxy.json`. |
| `render/test_scripts.sh` | **Modify.** `wg-quick`/`ip` stubs → `tinyproxy`/adapter stubs; the nine `test_wireguard_*` tests → proxy equivalents. |
| `render/run_tests.sh` | **Modify.** Add a fourth suite, `test_proxy`. |
| `render/README.md` | **Modify.** Replace the WireGuard section with the proxy design and the Mac workflow. |

### One decision the spec left open

The spec says `/healthz` gains the `proxy*` keys but not which file holds them. **The adapter writes `proxy.json`; the supervisor keeps writing `tunnel.json`; `keepalive.py` merges both at serve time.**

This is deliberate. Putting the adapter's counters in `tunnel.json` would mean two processes doing read-modify-write on one JSON file — the supervisor rewrites it on every hostname change, and the adapter updates it continuously. One clobbers the other, and the symptom would be proxy stats silently vanishing. Two files, merged on read, has no such window. It also fits `keepalive.py`, which already merges defaults over a state file.

## Global Constraints

- Python 3 standard library only. No `pip install`, no `requirements.txt`, on either side.
- All multi-byte frame fields are little-endian, on both sides, without exception.
- `conn_id` 0 is invalid and must never be allocated or accepted.
- Frame header is exactly 10 bytes. Total frame must not exceed `VC_MAX_DATA_PAYLOAD_SIZE = 2000`.
- Default payload 1400; absolute ceiling 1990.
- `seq` is per-direction and per-connection, starting at 0, incrementing by 1. A received `seq` that is not the expected next value is a gap: send `RESET` and close. Never resynchronise a stream that has lost bytes.
- All sockets bind loopback only, on both sides.
- `render/render_supervisor.sh` is container PID 1. It must never use a pipe for a backgrounded child's stdout where `$!` is needed as the child's own pid.
- The five pre-existing dirty files must never be staged: `cloudflare.sh`, `cloudflare.bat`, `github_run/cloudflare.sh`, `github_run/cloudflare.bat`, `github_run/run_info.json`. Never `git add -A`, `.`, or `commit -a`.
- WireGuard is removed, not deprecated. No `wireguard` or `wireguard_reason` key survives in any state file, and no `wg-quick` invocation remains.
- `run_github.sh`, `github_run/cloudflare.sh`, `run/`, `run2/`, `run3/` are not touched by this plan.

## Review Focus

Five input classes the spec implies. Each gets a test in the task that owns the code.

1. **A browser opening many connections at once.** A ceiling that is too low hangs pages rather than failing them visibly, and browsers routinely open 6–10 per page. Tests: adapter refuses past `PROXY_MAX_CONNS`; bridge refuses past its ceiling. Tasks 2, 3.
2. **Render asleep or the adapter dead.** The Mac's `sendto` still succeeds, because UDP to a dead port fails silently, so nothing surfaces an error and the browser hangs forever. Test: bridge closes a connection that goes `MAC_PROXY_IDLE_TIMEOUT` seconds with no frame. Task 3.
3. **The virtual channel's reorder-timeout drop.** Without `seq` this is undetectable stream corruption, surfacing as a corrupt page far from its cause. Tests: adapter sends `RESET` on a gap; bridge closes on a gap. Tasks 2, 3.
4. **`tinyproxy` cannot reach the destination.** This must arrive as a normal HTTP error the browser displays, not a hang or a reset. Not reachable by unit test — verified in Task 6 against a deliberately unroutable host.
5. **A stale binary on the other end.** An old `udp_client` or server speaking a different format must be rejected, not misparsed into plausible-looking garbage. Test: `decode` returns `None` for an unknown `version` byte. Task 1.

---

### Task 1: Frame format

The wire format both sides depend on. Nothing else can be built until it exists and is pinned.

**Files:**
- Create: `render/proxy_frame.py`
- Test: `render/test_proxy.py`

**Interfaces:**
- Consumes: nothing.
- Produces: everything below. Tasks 2 and 3 import this module and must not redefine any of it.

```python
VERSION: int = 1
HEADER_SIZE: int = 10
MAX_PAYLOAD: int = 1400        # default, overridable by the caller
ABS_MAX_PAYLOAD: int = 1990    # VC_MAX_DATA_PAYLOAD_SIZE - HEADER_SIZE
MAX_CONNS: int = 128
IDLE_TIMEOUT: float = 30.0

OPEN: int = 0x01
DATA: int = 0x02
CLOSE: int = 0x04
RESET: int = 0x08

class Frame(NamedTuple):
    version: int
    flags: int
    conn_id: int
    seq: int
    payload: bytes

def encode(flags: int, conn_id: int, seq: int, payload: bytes = b"") -> bytes
def decode(buf: bytes) -> Frame | None
```

`decode` returns `None` — never raises — for a buffer shorter than `HEADER_SIZE`, a `version` byte that is not `VERSION`, or a `conn_id` of 0. All four cases are "drop and count" to the caller, so collapsing them into one sentinel is deliberate; do not add a reason code.

`encode` uses struct format `<BBHIH`: version, flags, conn_id, seq, length, then payload. That is 10 bytes with no padding, which is why the format string is fixed and not computed.

- [ ] **Step 1: Write the failing tests**

```python
class FrameTestCase(unittest.TestCase):
    def test_round_trip_preserves_every_field(self):
        f = proxy_frame.encode(proxy_frame.DATA, 7, 42, b"hello")
        got = proxy_frame.decode(f)
        self.assertEqual(got.flags, proxy_frame.DATA)
        self.assertEqual(got.conn_id, 7)
        self.assertEqual(got.seq, 42)
        self.assertEqual(got.payload, b"hello")

    def test_header_is_ten_bytes(self):
        self.assertEqual(proxy_frame.HEADER_SIZE, 10)
        self.assertEqual(len(proxy_frame.encode(proxy_frame.OPEN, 1, 0)), 10)

    def test_control_frames_carry_no_payload(self):
        for flag in (proxy_frame.OPEN, proxy_frame.CLOSE, proxy_frame.RESET):
            self.assertEqual(proxy_frame.decode(proxy_frame.encode(flag, 3, 1)).payload, b"")

    def test_truncated_frame_is_rejected(self):
        self.assertIsNone(proxy_frame.decode(b"\x01\x02\x00"))

    def test_unknown_version_is_rejected(self):
        bad = bytearray(proxy_frame.encode(proxy_frame.DATA, 1, 0, b"x"))
        bad[0] = 0x7f
        self.assertIsNone(proxy_frame.decode(bytes(bad)))

    def test_zero_conn_id_is_rejected(self):
        self.assertIsNone(proxy_frame.decode(proxy_frame.encode(proxy_frame.DATA, 0, 0, b"x")))

    def test_max_payload_fits_inside_the_vc_limit(self):
        f = proxy_frame.encode(proxy_frame.DATA, 1, 0, b"x" * proxy_frame.ABS_MAX_PAYLOAD)
        self.assertLessEqual(len(f), 2000)

    def test_max_seq_and_conn_id_survive(self):
        got = proxy_frame.decode(proxy_frame.encode(proxy_frame.DATA, 65535, 2**32 - 1, b"z"))
        self.assertEqual(got.conn_id, 65535)
        self.assertEqual(got.seq, 2**32 - 1)
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd render && python3 -m unittest test_proxy -v`
Expected: `ModuleNotFoundError: No module named 'proxy_frame'`

- [ ] **Step 3: Implement `render/proxy_frame.py`**

Define the constants and `Frame` exactly as in the Interfaces block. `encode` packs with `struct.pack("<BBHIH", VERSION, flags, conn_id, seq, len(payload))` then concatenates the payload. `decode` checks length, version and `conn_id` before unpacking with `struct.unpack_from("<BBHIH", buf, 0)`, returning the payload as `bytes(buf[HEADER_SIZE:HEADER_SIZE + length])`.

- [ ] **Step 4: Run to verify it passes**

Run: `cd render && python3 -m unittest test_proxy -v`
Expected: 8 tests, OK

- [ ] **Step 5: Commit**

```bash
git add render/proxy_frame.py render/test_proxy.py
git commit -m "render: add the proxy frame format both sides will share"
```

---

### Task 2: Render-side adapter

Turns framed datagrams into TCP connections to `tinyproxy`. The demux and the gap detection are the two things that can silently corrupt a stream, so both are pinned here.

**Files:**
- Create: `render/proxy_adapter.py`
- Test: `render/test_proxy.py`

**Interfaces:**
- Consumes: all of `proxy_frame` from Task 1.
- Produces:

```python
# proxy_adapter.py
def write_state(state_dir: str, status: str, conns: int, bytes_in: int,
                bytes_out: int, resets: int, last_error: str) -> None
def run(bind_host: str = "127.0.0.1", bind_port: int = 41800,
        upstream_host: str = "127.0.0.1", upstream_port: int = 8888,
        state_dir: str = "/run/tcpudp", max_conns: int = 128,
        idle_timeout: float = 30.0) -> None
```

`write_state` writes `proxy.json` in the same directory as `tunnel.json`, containing exactly these seven keys and nothing else:

```json
{"proxy":"ready","proxy_conns":0,"proxy_bytes_in":0,"proxy_bytes_out":0,"proxy_resets":0,"proxy_last_error":""}
```

Write it atomically — write `proxy.json.tmp` then `os.replace` — because `keepalive.py` may read it at any moment and a half-written file would fall back to defaults, hiding a live adapter.

Per-`conn_id` state the adapter must keep: the TCP socket, the next `seq` it expects from the Mac, and the last time it saw traffic. `conn_id` allocation starts at 1, increments, wraps at 65535, and skips ids currently in use.

- [ ] **Step 1: Write the failing tests**

Use a real local TCP echo server as the stand-in for `tinyproxy`, and a real UDP socket as the stand-in for the `server`. No mocks.

```python
class AdapterTestCase(unittest.TestCase):
    def setUp(self):
        self.echo = _start_echo_server()          # TCP, echoes what it reads
        self.state_dir = tempfile.mkdtemp()
        self.adapter = _start_adapter(self.echo.port, self.state_dir)
        self.client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.client.bind(("127.0.0.1", 0))
        self.client.settimeout(5)

    def test_open_then_data_is_forwarded_and_echoed_back(self):
        self._send(proxy_frame.encode(proxy_frame.OPEN, 1, 0))
        self._send(proxy_frame.encode(proxy_frame.DATA, 1, 0, b"ping"))
        self.assertEqual(self._recv().payload, b"ping")

    def test_two_connections_do_not_cross(self):
        self._send(proxy_frame.encode(proxy_frame.OPEN, 1, 0))
        self._send(proxy_frame.encode(proxy_frame.OPEN, 2, 0))
        self._send(proxy_frame.encode(proxy_frame.DATA, 1, 0, b"one"))
        self._send(proxy_frame.encode(proxy_frame.DATA, 2, 0, b"two"))
        first, second = self._recv(), self._recv()
        self.assertEqual({first.payload, second.payload}, {b"one", b"two"})
        self.assertNotEqual(first.conn_id, second.conn_id)

    def test_seq_gap_sends_reset(self):
        self._send(proxy_frame.encode(proxy_frame.OPEN, 1, 0))
        self._send(proxy_frame.encode(proxy_frame.DATA, 1, 0, b"a"))
        self._recv()
        self._send(proxy_frame.encode(proxy_frame.DATA, 1, 5, b"b"))   # gap
        self.assertTrue(self._recv().flags & proxy_frame.RESET)

    def test_close_from_mac_closes_upstream(self):
        self._send(proxy_frame.encode(proxy_frame.OPEN, 1, 0))
        self._send(proxy_frame.encode(proxy_frame.CLOSE, 1, 1))
        self.assertTrue(self.echo.saw_close())

    def test_upstream_refused_sends_reset_and_records_degraded(self):
        # No listener on this port, so connect() is refused.
        self._send(proxy_frame.encode(proxy_frame.OPEN, 9, 0), port=self.dead_port)
        self.assertTrue(self._recv(port=self.dead_port).flags & proxy_frame.RESET)
        self.assertEqual(_read_proxy_json(self.state_dir)["proxy"], "degraded")

    def test_malformed_datagram_does_not_kill_the_adapter(self):
        self._send(b"\x01\x02\x00")
        self._send(b"garbage")
        self._send(proxy_frame.encode(proxy_frame.OPEN, 4, 0))
        self._send(proxy_frame.encode(proxy_frame.DATA, 4, 0, b"still here"))
        self.assertEqual(self._recv().payload, b"still here")

    def test_state_file_reports_counters(self):
        self._send(proxy_frame.encode(proxy_frame.OPEN, 1, 0))
        self._send(proxy_frame.encode(proxy_frame.DATA, 1, 0, b"abcd"))
        self._recv()
        state = _wait_for(lambda: _read_proxy_json(self.state_dir)["proxy_bytes_out"] >= 4)
        self.assertEqual(state["proxy"], "ready")
        self.assertEqual(state["proxy_conns"], 1)
        self.assertEqual(set(state), {"proxy", "proxy_conns", "proxy_bytes_in",
                                      "proxy_bytes_out", "proxy_resets", "proxy_last_error"})

    def test_conn_id_ceiling_refuses_further_connections(self):
        # max_conns=2 for this run, so the third OPEN is refused.
        self._send(proxy_frame.encode(proxy_frame.OPEN, 1, 0))
        self._send(proxy_frame.encode(proxy_frame.OPEN, 2, 0))
        self._send(proxy_frame.encode(proxy_frame.OPEN, 3, 0))
        self._send(proxy_frame.encode(proxy_frame.DATA, 3, 0, b"nope"))
        self.assertTrue(self._recv().flags & proxy_frame.RESET)
        self.assertEqual(_read_proxy_json(self.state_dir)["proxy_conns"], 2)

    def test_conn_id_wraparound_skips_ids_in_use(self):
        # Allocates ids 1..65535 then wraps; the id already in use must not be
        # handed out twice, so the new connection gets a different id.
        first = self._allocate_after_wrap(65535)
        self.assertNotEqual(first, 65535)
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd render && python3 -m unittest test_proxy -v`
Expected: `ModuleNotFoundError: No module named 'proxy_adapter'`

- [ ] **Step 3: Implement `render/proxy_adapter.py`**

Use `selectors` with a single UDP socket registered `EVENT_READ` plus one selector per live TCP connection, all handled in one loop. That is enough for a proxy: every connection is loopback-fast, so a thread per connection buys nothing and costs a context switch per packet.

The loop, per readable socket:

- **UDP readable** — `recvfrom(65535)`. The first datagram from a given peer address establishes the server's address; store it and ignore datagrams from any other address. Decode; on `None`, increment a drop counter and continue. On `OPEN`, allocate a `conn_id` slot if the id is free and under the ceiling, `connect()` to the upstream, and register it; on a refused connection send `RESET` and set `last_error`/`degraded`. On `DATA`, check `seq` against the expected value — a mismatch sends `RESET`, closes, and increments `resets`; a match forwards the payload and increments. On `CLOSE`, `shutdown(SHUT_WR)` on the upstream and drop the slot after it drains.
- **TCP readable** — `recv`; empty result means the upstream closed, so send `CLOSE` to the Mac and drop the slot. Otherwise frame it with the connection's own outbound `seq`, `sendto` the server's address, and add to `bytes_out`.

Write `proxy.json` on every state change, and at minimum once per second, so `/healthz` stays live without a write per packet. Never log payload bytes.

- [ ] **Step 4: Run to verify it passes**

Run: `cd render && python3 -m unittest test_proxy -v`
Expected: 17 tests, OK (8 from Task 1, 9 added here)

- [ ] **Step 5: Commit**

```bash
git add render/proxy_adapter.py render/test_proxy.py
git commit -m "render: demux framed datagrams into tinyproxy connections"
```

---

### Task 3: Mac-side bridge

**Files:**
- Create: `render/mac_proxy_bridge.py`
- Test: `render/test_proxy.py`

**Interfaces:**
- Consumes: all of `proxy_frame` from Task 1.
- Produces:

```python
# mac_proxy_bridge.py
def run(listen_host: str = "127.0.0.1", listen_port: int = 8889,
        upstream_host: str = "127.0.0.1", upstream_port: int = 5003,
        max_conns: int = 128, idle_timeout: float = 30.0) -> None
```

One UDP socket bound to `127.0.0.1:0` serves both directions: `sendto` the upstream address, `recvfrom` replies on the same socket. This mirrors the server's own design and avoids claiming a second fixed port.

Env overrides: `MAC_PROXY_PORT`, `MAC_PROXY_UPSTREAM_PORT`, `MAC_PROXY_MAX_CONNS`, `MAC_PROXY_IDLE_TIMEOUT`.

- [ ] **Step 1: Write the failing tests**

```python
class BridgeTestCase(unittest.TestCase):
    def test_browser_bytes_reach_the_upstream_and_come_back(self):
        sock = self._connect()
        sock.sendall(b"GET / HTTP/1.0\r\n\r\n")
        self.assertEqual(self.upstream.next_payload(), b"GET / HTTP/1.0\r\n\r\n")
        self.assertEqual(sock.recv(100), b"GET / HTTP/1.0\r\n\r\n")

    def test_open_frame_is_sent_before_any_data(self):
        self._connect()
        first = self.upstream.next_frame()
        self.assertTrue(first.flags & proxy_frame.OPEN)
        self.assertEqual(first.conn_id, 1)

    def test_conn_ids_differ_between_browser_connections(self):
        a = self._connect()
        b = self._connect()
        self.assertNotEqual(self.upstream.next_frame().conn_id,
                            self.upstream.next_frame().conn_id)

    def test_close_from_adapter_closes_the_browser_socket(self):
        sock = self._connect()
        self.upstream.send(proxy_frame.encode(proxy_frame.CLOSE, 1, 1))
        self.assertEqual(sock.recv(10), b"")

    def test_seq_gap_closes_the_browser_socket(self):
        sock = self._connect()
        self.upstream.send(proxy_frame.encode(proxy_frame.DATA, 1, 0, b"partial"))
        self.upstream.send(proxy_frame.encode(proxy_frame.DATA, 1, 9, b"gap"))
        self.assertEqual(sock.recv(100), b"partial")
        self.assertEqual(sock.recv(100), b"")

    def test_idle_timeout_closes_a_connection_nobody_answers(self):
        sock = self._connect()
        sock.settimeout(10)
        # Upstream goes silent entirely, as a dead Render container would.
        self.assertEqual(sock.recv(100), b"")

    def test_ceiling_refuses_extra_connections(self):
        for _ in range(self.bridge.max_conns):
            self._connect()
        extra = self._connect()
        self.assertEqual(extra.recv(10), b"")   # accepted then closed at once

    def test_malformed_reply_does_not_kill_the_bridge(self):
        sock = self._connect()
        self.upstream.send(b"\x01\x02\x00")
        self.upstream.send(proxy_frame.encode(proxy_frame.DATA, 1, 0, b"alive"))
        self.assertEqual(sock.recv(100), b"alive")
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd render && python3 -m unittest test_proxy -v`
Expected: `ModuleNotFoundError: No module named 'mac_proxy_bridge'`

- [ ] **Step 3: Implement `render/mac_proxy_bridge.py`**

Same `selectors` shape as the adapter, with the roles mirrored: the listening TCP socket is the source of new browser connections, and the single UDP socket is where framed replies arrive.

- A new browser connection allocates a `conn_id`, sends `OPEN`, and registers the socket.
- A readable browser socket forwards its bytes as `DATA` with that connection's outbound `seq`, split into at most `MAX_PAYLOAD` chunks.
- A readable UDP socket decodes; `None` drops and counts. `DATA` is checked against the expected inbound `seq`, and a gap closes the browser socket immediately. `CLOSE` and `RESET` both close it. On close, send `CLOSE` upstream if the connection was still open.
- **The idle timeout is mandatory, not defensive.** Check elapsed time against `idle_timeout` on every loop iteration, including when no socket is readable, using `selectors.select(timeout=...)` rather than an unbounded wait. A connection past its deadline is closed. This is the only thing standing between a dead Render container and a browser that hangs forever, because `sendto` to a dead port reports success.

- [ ] **Step 4: Run to verify it passes**

Run: `cd render && python3 -m unittest test_proxy -v`
Expected: 25 tests, OK (17 through Task 2, 8 added here)

- [ ] **Step 5: Commit**

```bash
git add render/mac_proxy_bridge.py render/test_proxy.py
git commit -m "render: bridge browser TCP connections into framed datagrams"
```

---

### Task 4: Swap WireGuard for the adapter in the supervisor

**Files:**
- Modify: `render/render_supervisor.sh` — `resolve_config`, `start_wireguard` → `start_proxy_adapter`, `start_server`, `cleanup`, `main`
- Modify: `render/test_scripts.sh` — `install_stubs` (`wg-quick`/`ip` stubs out, adapter and `tinyproxy` stubs in), `new_sandbox` stub-knob resets, and the nine `test_wireguard_*` tests replaced

**Interfaces:**
- Consumes: `render/proxy_adapter.py` from Task 2, and `render/Dockerfile`'s `tinyproxy` binary.
- Produces: state file `tunnel.json` with the `wireguard` and `wireguard_reason` keys **gone**; a new `render/proxy_adapter.pidfile`; startup order `tinyproxy` → adapter → server.

- [ ] **Step 1: Replace the WireGuard stubs with proxy stubs, and the tests with proxy tests**

In `install_stubs`, delete the `wg-quick` and `ip` stubs. Add a `tinyproxy` stub that records `tinyproxy` to `$STUB_ORDER_LOG` and stays alive, and an adapter stub that records `proxy_adapter` to the same log and stays alive. In `new_sandbox`, replace the `STUB_WG_*` resets with `STUB_PROXY_FAIL STUB_PROXY_ADDR`.

Delete all nine `test_wireguard_*` functions and their `run_test` lines. Replace them with:

- `test_the_adapter_starts_before_the_server` — assert the `proxy_adapter` line precedes the `server` line in `STUB_ORDER_LOG`.
- `test_tinyproxy_starts_before_the_adapter` — assert `tinyproxy` precedes `proxy_adapter`.
- `test_the_server_is_aimed_at_the_adapter_port` — with `PROXY_ADAPTER_PORT` set to a free port, assert the recorded server argv contains `--udp-target-port=<that port>`. This replaces the old WireGuard-derived-port test and must not be satisfied by a default.
- `test_no_wireguard_remains` — assert the log contains no `wireguard`, `wg-quick` or `CAP_NET_ADMIN` text, and that the state file has no `wireguard` key.
- `test_cleanup_stops_the_adapter` — after SIGTERM, assert the adapter pid is gone and no `wg-quick down` was attempted.

- [ ] **Step 2: Run to verify it fails**

Run: `bash render/test_scripts.sh`
Expected: FAIL on the four new assertions — the supervisor still starts `wg-quick` and still aims the server at 7001.

- [ ] **Step 3: Rewire `render/render_supervisor.sh`**

In `resolve_config`, delete `WG_CONFIG`, `WG_INTERFACE`, `WG_CONFIG_PATH`, `WG_REQUIRED`, `WG_TUN`, `WG_NET_ADMIN`, `WG_REASON` and the `ListenPort` derivation. Add:

```bash
PROXY_ADAPTER_PORT="${PROXY_ADAPTER_PORT:-41800}"
PROXY_LISTEN_PORT="${PROXY_LISTEN_PORT:-8888}"
PROXY_MAX_CONNS="${PROXY_MAX_CONNS:-128}"
PROXY_IDLE_TIMEOUT="${PROXY_IDLE_TIMEOUT:-30}"
```

Derive `UDP_TARGET_PORT` from `PROXY_ADAPTER_PORT` alone. One variable, so the server and the adapter cannot drift — this is the same failure the WireGuard-derived version had.

Replace `start_wireguard` with `start_proxy_adapter`, which starts `tinyproxy` then the adapter, writes the adapter's pid to `$STATE_DIR/proxy_adapter.pid`, and logs the two ports. It must **not** be fatal on adapter failure: the tunnel and `/healthz` stay up, because a dead adapter should degrade the proxy, not take the service down. Remove `_wg_log_output` and `_wg_reason`.

In `start_server`, drop the `WG_STATUS` warning block — there is no longer a status to consult — and keep the existing warning for an empty `UDP_TARGET_PORT`, reworded to name `PROXY_ADAPTER_PORT`.

In `write_state`, remove the `wireguard` and `wireguard_reason` fields. The state file becomes `hostname`, `port`, `published`, `source`, `updated`.

In `cleanup`, replace `wg-quick down` with a kill of the recorded adapter and `tinyproxy` pids. In `main`, replace the `start_wireguard` call with `start_proxy_adapter`, before `start_server`.

- [ ] **Step 4: Run to verify it passes**

Run: `bash render/run_tests.sh`
Expected: all 4 suites passed. `test_scripts.sh` should report 23 tests, 0 failed — 27 minus the 9 WireGuard tests, plus the 5 new ones.

- [ ] **Step 5: Commit**

```bash
git add render/render_supervisor.sh render/test_scripts.sh
git commit -m "render: start the proxy adapter instead of WireGuard"
```

---

### Task 5: Serve the proxy state, install tinyproxy, declare the config

**Files:**
- Modify: `render/keepalive.py` — `DEFAULTS`, `read_state`, add `PROXY_STATE_FILENAME`
- Modify: `render/Dockerfile` — apt list
- Modify: `render/render.yaml` — env vars
- Modify: `render/run_tests.sh` — fourth suite
- Test: `render/test_keepalive.py`, `render/test_proxy.py`

**Interfaces:**
- Consumes: the `proxy.json` shape from Task 2.
- Produces: `/healthz` carrying the six `proxy*` keys; `run_tests.sh` running four suites.

- [ ] **Step 1: Write the failing keepalive tests**

```python
def test_proxy_keys_are_served_when_proxy_json_exists(self):
    self._write_json("proxy.json", {"proxy": "ready", "proxy_conns": 3,
                                    "proxy_bytes_in": 10, "proxy_bytes_out": 20,
                                    "proxy_resets": 1, "proxy_last_error": ""})
    body = self._get_healthz()
    self.assertEqual(body["proxy"], "ready")
    self.assertEqual(body["proxy_conns"], 3)
    self.assertEqual(body["proxy_resets"], 1)

def test_proxy_keys_default_when_proxy_json_is_absent(self):
    body = self._get_healthz()
    self.assertEqual(body["proxy"], "absent")
    self.assertEqual(body["proxy_conns"], 0)

def test_corrupt_proxy_json_does_not_break_healthz(self):
    self._write_raw("proxy.json", "{not json")
    self.assertEqual(self._get_healthz()["proxy"], "absent")
    self.assertEqual(self._get_healthz()["status"], "ok")

def test_tunnel_json_keys_survive_the_merge(self):
    self._write_json("tunnel.json", {"hostname": "h.trycloudflare.com", "published": True})
    self._write_json("proxy.json", {"proxy": "ready"})
    body = self._get_healthz()
    self.assertEqual(body["hostname"], "h.trycloudflare.com")
    self.assertEqual(body["proxy"], "ready")
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd render && python3 -m unittest test_keepalive -v`
Expected: 4 failures — `proxy` absent from the payload.

- [ ] **Step 3: Implement the merge in `keepalive.py`**

Add `PROXY_STATE_FILENAME = "proxy.json"` and the six keys to `DEFAULTS`, with `proxy` defaulting to `"absent"`. In `read_state`, after merging `tunnel.json` over `DEFAULTS`, read `proxy.json` and merge its keys the same way. Both reads must be individually fault-tolerant: one corrupt file must not blank the other's data, and either being absent must leave the defaults in place.

- [ ] **Step 4: Verify the keepalive tests pass, then the whole suite**

Run: `cd render && python3 -m unittest test_keepalive -v` — expect OK.
Then `bash render/run_tests.sh` — expect all 4 suites passed.

- [ ] **Step 5: Add `tinyproxy`, the env vars, and the fourth suite**

In `render/Dockerfile`, add `tinyproxy` to the existing apt list alongside `wireguard-tools` — which comes out in the same edit.

In `render/render.yaml`, delete `WIREGUARD_CONFIG` and `WIREGUARD_REQUIRED`. Add `PROXY_ADAPTER_PORT`, `PROXY_LISTEN_PORT`, `PROXY_MAX_CONNS`, `PROXY_IDLE_TIMEOUT` with the spec's defaults. Do not set them on Render's dashboard; the defaults are correct.

In `render/run_tests.sh`, add a fourth suite and update the `3` in both the failure line and the success line:

```bash
run proxy           bash -c 'cd "$1" && python3 -m unittest test_proxy' _ "$HERE"
```

- [ ] **Step 6: Commit**

```bash
git add render/keepalive.py render/Dockerfile render/render.yaml render/run_tests.sh render/test_keepalive.py
git commit -m "render: serve proxy state, install tinyproxy, declare the config"
```

---

### Task 6: The Mac entry script and the README

The only thing the user actually runs. Nothing works until this exists.

**Files:**
- Create: `render/run_proxy_mac.sh`
- Modify: `render/README.md`

**Interfaces:**
- Consumes: `render/mac_proxy_bridge.py` (Task 3), `render/trigger_render.sh` (existing), `run/udp_client` (existing), `github_run/cloudflare.sh` (existing, read-only).
- Produces: one command that prints `http://127.0.0.1:8889` and blocks until interrupted.

- [ ] **Step 1: Write the script**

Sequence, each step gating the next, any failure aborting with a message naming the step:

1. `trigger_render.sh` — prints the live hostname, and writes DNS pinning exactly as it does today.
2. `cloudflared access tcp --url tcp://localhost:7001 --hostname <host>`.
3. `run/udp_client`, using `run/config.json` unchanged.
4. `mac_proxy_bridge.py` on `127.0.0.1:8889`.
5. A pinger loop hitting `/healthz` on the published URL every `MAC_PROXY_PING_INTERVAL` (60s), so the free-tier container does not sleep while the script runs.
6. Print the proxy URL and `wait`.

**Cleanup is the part that needs care.** Record each PID as it starts and kill only those, in reverse order. Do not `pkill -f cloudflared` — the existing `run_github.sh` does that, and it kills the user's own unrelated tunnel. Do not touch port 7001 if it is already owned when the script starts; if something else already holds 7001, say so and abort rather than reusing it, because silently sharing the port sends VC traffic to the wrong process. `trap` on `INT` and `TERM`.

- [ ] **Step 2: Verify the script parses and its guards fire**

Run: `bash -n render/run_proxy_mac.sh` — expect no output.
Run it with a stubbed `trigger_render.sh` that fails, and confirm it aborts naming that step and does not start `cloudflared`.
Run it with 7001 already occupied, and confirm it aborts saying so.

These are the two failure modes that would otherwise damage something the user is already running. Check them; do not skip to the happy path.

- [ ] **Step 3: Rewrite the README's WireGuard section**

Document the proxy workflow, the `PROXY_*` and `MAC_PROXY_*` variables, the framing table, and the two silent-failure modes this design exists to handle. State plainly that the container sleeps when idle and the first request after that costs 30–60s, and that throughput is bounded by `cloudflared`. Update the test count.

- [ ] **Step 4: Commit**

```bash
git add render/run_proxy_mac.sh render/README.md
git commit -m "render: add the Mac entry script and document the proxy"
```

---

### Task 7: Verify against the live service

No local test covers the tunnel, `cloudflared`, or Render. That is deliberate — the user has directed that the live redeploy is the verification mechanism.

- [ ] **Step 1: Push and wait for the deploy**

```bash
git push origin run
```

- [ ] **Step 2: Confirm the adapter is up**

```bash
curl -fsS https://tcpudp.onrender.com/healthz | python3 -m json.tool
```

Expect `"proxy": "ready"`, `"proxy_conns": 0`, and **no** `wireguard` key. If `proxy` is `degraded`, read `proxy_last_error` — it names the cause directly, which is the entire reason that field exists.

- [ ] **Step 3: Prove egress comes from Render**

```bash
./render/run_proxy_mac.sh
# in another shell:
curl -x http://127.0.0.1:8889 https://ifconfig.me
```

The address must be Render's, not home. Then watch `proxy_conns` and `proxy_bytes_out` move in `/healthz` while a real page loads in a browser.

- [ ] **Step 4: Confirm an unreachable destination surfaces as an HTTP error, not a hang**

```bash
curl -x http://127.0.0.1:8889 --max-time 20 http://203.0.113.1/
```

Expect an HTTP error response from `tinyproxy`. A hang here means Review Focus item 4 is unaddressed.

- [ ] **Step 5: Confirm the existing tunnel path is untouched**

Run the existing `run_github.sh` path and confirm it still works. This plan touches no file it depends on, and that claim needs checking rather than asserting.
