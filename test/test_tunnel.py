#!/usr/bin/env python3
"""
Test script for verifying cloudflare tunnel works.

Tests HTTP echo through the tunnel using POST requests.
Since quick tunnels (trycloudflare.com) only support HTTP, not raw TCP,
we test via HTTP POST echo.

Usage:
    python test/test_tunnel.py --hostname <tunnel-hostname>
"""

import argparse
import sys
import urllib.request


def test_echo(hostname, message=b"Hello from tunnel test!"):
    """Send a POST and verify echo."""
    url = f"https://{hostname}/"
    print(f"POST {url} ({len(message)} bytes)...")
    try:
        req = urllib.request.Request(url, data=message, method='POST')
        with urllib.request.urlopen(req, timeout=15) as resp:
            body = resp.read()
            print(f"  Response: {resp.status}, {len(body)} bytes")
            if body == message:
                print("  Echo PASSED!")
                return True
            else:
                print(f"  Echo FAILED: expected {len(message)} bytes, got {len(body)} bytes")
                return False
    except Exception as e:
        print(f"  Echo FAILED: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(description="Test cloudflare tunnel echo")
    parser.add_argument("--hostname", required=True, help="Tunnel hostname (trycloudflare.com)")
    args = parser.parse_args()

    hostname = args.hostname.replace("https://", "").replace("http://", "")

    # Basic echo test
    passed = test_echo(hostname)

    # Multiple echo tests
    if passed:
        print("\nRunning multiple echo tests...")
        for i in range(5):
            msg = f"Test message {i}".encode()
            if not test_echo(hostname, msg):
                passed = False
                break

    # Large payload test
    if passed:
        print("\nLarge payload test...")
        large_msg = b"x" * 4096
        passed = test_echo(hostname, large_msg)

    if passed:
        print("\n=== ALL TESTS PASSED ===")
        return 0
    else:
        print("\n=== SOME TESTS FAILED ===")
        return 1


if __name__ == "__main__":
    sys.exit(main())
