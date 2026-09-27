#!/usr/bin/env python3
"""HTTP health endpoint reporting the live tunnel state.

Single responsibility: serve the supervisor's state file as JSON on GET /healthz
and GET /. The supervisor writes $TCPUDP_STATE_DIR/tunnel.json (Contract A); this
module only reads it, re-reading on every request so the Mac side can poll for a
hostname that changes when cloudflared restarts.

The endpoint always answers 200 - a missing, truncated or unparseable state file
yields hostname: null rather than an error, so Render's health check never flaps
and the caller can distinguish "waking up" from "broken".

Stdlib only.
"""

import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# Contract A, minus status. A state file that omits a key falls back to the
# value here, so /healthz always reports all five keys.
DEFAULTS = {
    "hostname": None,
    "port": 7001,
    "published": False,
    "source": "render",
    "updated": "",
    # One of: skipped, up, up-not-routed, failed, error, unknown.
    # "up" alone does not mean egress is tunnelled - "up-not-routed" is an
    # interface that came up but is not in the default route, which is the
    # failure worth noticing.
    "wireguard": "skipped",
}

DEFAULT_PORT = 10000
DEFAULT_STATE_DIR = "/run/tcpudp"
STATE_FILENAME = "tunnel.json"

# Render's health check reaches the container from outside, so production must
# bind all interfaces. Tests override this with 127.0.0.1, because binding a
# non-loopback address on macOS raises an Application Firewall prompt.
DEFAULT_BIND = "0.0.0.0"


def state_path() -> str:
    return os.path.join(
        os.environ.get("TCPUDP_STATE_DIR", DEFAULT_STATE_DIR), STATE_FILENAME
    )


def read_state() -> dict:
    """Return the state file's contents, defaulted, plus status: ok.

    Never raises and never caches: a missing file, malformed JSON or a non-object
    document all degrade to DEFAULTS with hostname None.
    """
    try:
        with open(state_path(), "r", encoding="utf-8") as handle:
            parsed = json.load(handle)
    except (OSError, ValueError):
        # OSError covers a missing/unreadable file; ValueError covers
        # JSONDecodeError and UnicodeDecodeError.
        parsed = None
    state = dict(DEFAULTS)
    if isinstance(parsed, dict):
        state.update(parsed)
    # status is added at serve time only; it is never written to the state file.
    state["status"] = "ok"
    return state


class Handler(BaseHTTPRequestHandler):
    server_version = "keepalive/1.0"

    def _respond(self, code, content_type, body):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        # Ignore the query string: Render's health check may append its own.
        route = self.path.split("?", 1)[0]
        if route in ("/healthz", "/"):
            payload = json.dumps(read_state()).encode("utf-8")
            self._respond(200, "application/json", payload)
        else:
            self._respond(404, "text/plain", b"not found\n")

    def log_message(self, fmt, *args):
        """Silence the default per-request stderr logging."""


def main():
    port = int(os.environ.get("PORT", DEFAULT_PORT))
    bind = os.environ.get("KEEPALIVE_BIND", DEFAULT_BIND)
    httpd = ThreadingHTTPServer((bind, port), Handler)
    # Test hook: signal readiness once the socket is listening so tests can wait
    # on a file instead of sleeping. Production never sets this.
    ready = os.environ.get("KEEPALIVE_READY_FILE")
    if ready:
        open(ready, "w").close()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()


if __name__ == "__main__":
    main()
