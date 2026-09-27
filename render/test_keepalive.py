#!/usr/bin/env python3
"""Tests for keepalive.py's /healthz contract (Contract B).

Stdlib only. Each test boots a real keepalive.py subprocess against a private
state directory and talks to it over HTTP, so the assertions cover the wire
format rather than internal helpers.

Run from inside render/:

    python3 -m unittest test_keepalive -v
"""

import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
KEEPALIVE = os.path.join(HERE, "keepalive.py")
READY_FILENAME = "ready.marker"
READY_TIMEOUT = 10.0
HTTP_TIMEOUT = 10.0


def free_port():
    """Reserve an ephemeral port and hand it back."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def get(path, port):
    """GET path from the server on port; return (status, parsed_json_or_None)."""
    url = "http://127.0.0.1:%d%s" % (port, path)
    try:
        with urllib.request.urlopen(url, timeout=HTTP_TIMEOUT) as response:
            status = response.status
            raw = response.read()
    except urllib.error.HTTPError as exc:
        # 404 and friends are expected outcomes here, not test errors.
        status = exc.code
        raw = exc.read()
        exc.close()
    try:
        return status, json.loads(raw.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        return status, None


class KeepaliveTestCase(unittest.TestCase):
    """Base fixture: a private state dir plus a live keepalive.py on it."""

    def setUp(self):
        self.state_dir = tempfile.mkdtemp(prefix="keepalive-state-")
        self.addCleanup(shutil.rmtree, self.state_dir, ignore_errors=True)
        self.port = free_port()
        self.proc = self.start_server(self.state_dir, self.port)

    def start_server(self, state_dir, port):
        """Launch keepalive.py on port, wait for the ready file, register cleanup.

        Readiness is signalled through KEEPALIVE_READY_FILE so the test never
        has to sleep-and-hope; production does not set that variable.
        """
        ready = os.path.join(state_dir, READY_FILENAME)
        env = dict(os.environ)
        env["TCPUDP_STATE_DIR"] = state_dir
        env["PORT"] = str(port)
        env["KEEPALIVE_READY_FILE"] = ready
        proc = subprocess.Popen(
            [sys.executable, KEEPALIVE],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        self.addCleanup(self._stop_server, proc)
        self._wait_ready(proc, ready)
        return proc

    def _wait_ready(self, proc, ready):
        deadline = time.monotonic() + READY_TIMEOUT
        while time.monotonic() < deadline:
            if os.path.exists(ready):
                return
            if proc.poll() is not None:
                self.fail(
                    "keepalive.py exited with %s before signalling readiness:\n%s"
                    % (proc.returncode, self._drain(proc))
                )
            time.sleep(0.02)
        self.fail("keepalive.py did not signal readiness within %ss" % READY_TIMEOUT)

    @staticmethod
    def _drain(proc):
        out = proc.stdout.read().decode("utf-8", "replace")
        proc.stdout.close()
        return out

    @staticmethod
    def _stop_server(proc):
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)
        if proc.stdout is not None and not proc.stdout.closed:
            proc.stdout.close()

    # -- fixture helpers ---------------------------------------------------

    def write_state(self, payload=None, raw=None):
        """Write tunnel.json, either as a JSON object or as literal text."""
        path = os.path.join(self.state_dir, "tunnel.json")
        text = raw if raw is not None else json.dumps(payload)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
        return path


class HealthzContractTest(KeepaliveTestCase):

    def test_healthz_returns_state_file_contents(self):
        self.write_state(
            {
                "hostname": "abc-def.trycloudflare.com",
                "port": 7001,
                "published": True,
                "source": "render",
                "updated": "2026-09-27T12:34:56Z",
            }
        )
        status, body = get("/healthz", self.port)
        self.assertEqual(200, status)
        self.assertEqual("abc-def.trycloudflare.com", body["hostname"])
        self.assertIs(True, body["published"])
        self.assertEqual("render", body["source"])

    def test_root_path_serves_same_payload(self):
        self.write_state(
            {
                "hostname": "abc-def.trycloudflare.com",
                "port": 7001,
                "published": True,
                "source": "render",
                "updated": "2026-09-27T12:34:56Z",
            }
        )
        root_status, root_body = get("/", self.port)
        healthz_status, healthz_body = get("/healthz", self.port)
        self.assertEqual(200, root_status)
        self.assertEqual(200, healthz_status)
        self.assertEqual(healthz_body, root_body)

    def test_status_ok_always_present(self):
        self.write_state({"hostname": "abc-def.trycloudflare.com"})
        status, body = get("/healthz", self.port)
        self.assertEqual(200, status)
        self.assertEqual("ok", body["status"])

    def test_unknown_path_returns_404(self):
        status, _ = get("/nope", self.port)
        self.assertEqual(404, status)

    def test_missing_state_file_returns_200_with_null_hostname(self):
        # Post-wake, pre-tunnel: the endpoint must answer, never 500.
        self.assertFalse(os.path.exists(os.path.join(self.state_dir, "tunnel.json")))
        status, body = get("/healthz", self.port)
        self.assertEqual(200, status)
        self.assertIsNone(body["hostname"])

    def test_corrupt_state_file_returns_200_with_null_hostname(self):
        self.write_state(raw='{"hostname": ')
        status, body = get("/healthz", self.port)
        self.assertEqual(200, status)
        self.assertIsNone(body["hostname"])

    def test_null_hostname_in_state_file_is_preserved(self):
        self.write_state({"hostname": None, "published": False})
        status, body = get("/healthz", self.port)
        self.assertEqual(200, status)
        self.assertIsNone(body["hostname"])

    def test_defaults_applied_for_missing_keys(self):
        self.write_state({"hostname": "abc-def.trycloudflare.com"})
        status, body = get("/healthz", self.port)
        self.assertEqual(200, status)
        self.assertEqual(7001, body["port"])
        self.assertEqual("render", body["source"])
        self.assertIs(False, body["published"])
        self.assertIsInstance(body["updated"], str)

    def test_state_file_is_reread_per_request(self):
        self.write_state({"hostname": "first.trycloudflare.com"})
        _, first = get("/healthz", self.port)
        self.assertEqual("first.trycloudflare.com", first["hostname"])
        self.write_state({"hostname": "second.trycloudflare.com"})
        _, second = get("/healthz", self.port)
        self.assertEqual("second.trycloudflare.com", second["hostname"])


if __name__ == "__main__":
    unittest.main()
