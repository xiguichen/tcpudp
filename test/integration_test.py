#!/usr/bin/env python3
"""
Integration test for the TCP/UDP proxy pipeline.

Test flow:
  test_script → UDP → client → TCP (VC) → server → UDP → echo_server → UDP
  → server → TCP (VC) → client → UDP → test_script

Usage:
  python test/integration_test.py [--build-dir PATH]
"""

import argparse
import os
import platform
import socket
import struct
import subprocess
import sys
import threading
import time


SERVER_TCP_PORT = 12000
UDP_ECHO_PORT = 12001
CLIENT_UDP_PORT = 12002
CLIENT_ID = 1
TEST_MSG = b"Hello from integration test!"
STARTUP_WAIT = 3
RECV_TIMEOUT = 15

# Stress test parameters
STRESS_PACKET_COUNT = 200
STRESS_PACKET_SIZE = 1400  # Large but below typical MTU to avoid IP fragmentation
STRESS_RECV_TIMEOUT = 30


def find_binaries(build_dir):
    exe_suffix = ".exe" if platform.system() == "Windows" else ""

    search_paths = [
        (os.path.join(build_dir, "bin", f"server{exe_suffix}"),
         os.path.join(build_dir, "bin", f"udp_client{exe_suffix}")),
        (os.path.join(build_dir, "server", f"server{exe_suffix}"),
         os.path.join(build_dir, "client", f"udp_client{exe_suffix}")),
    ]

    for server_bin, client_bin in search_paths:
        if os.path.exists(server_bin) and os.path.exists(client_bin):
            return server_bin, client_bin

    # Print diagnostics
    for server_bin, client_bin in search_paths:
        print(f"Server binary: {server_bin} exists: {os.path.exists(server_bin)}")
        print(f"Client binary: {client_bin} exists: {os.path.exists(client_bin)}")
    raise FileNotFoundError("Could not find server and client binaries")


def run_echo_server(port, stop_event):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", port))
    sock.settimeout(0.5)
    while not stop_event.is_set():
        try:
            data, addr = sock.recvfrom(4096)
            sock.sendto(data, addr)
        except socket.timeout:
            continue
        except OSError:
            break
    sock.close()


def run_basic_test(test_sock):
    """Send a single message and verify echo."""
    print(f"\n--- Basic Echo Test ---")
    print(f"Sending: {TEST_MSG!r}")
    test_sock.sendto(TEST_MSG, ("127.0.0.1", CLIENT_UDP_PORT))

    data, addr = test_sock.recvfrom(4096)
    print(f"Received: {data!r} from {addr}")

    if data != TEST_MSG:
        print(f"FAILED: Expected {TEST_MSG!r}, got {data!r}")
        return False
    print("Basic echo test PASSED")
    return True


def run_stress_test(test_sock):
    """
    Send many large packets rapidly to stress the VC pipeline.
    Each packet has a 4-byte sequence number followed by a known pattern.
    This triggers partial TCP sends under buffer pressure, which exposes
    the bug where incomplete sends corrupt the TCP byte stream.
    """
    print(f"\n--- Stress Test ({STRESS_PACKET_COUNT} packets x {STRESS_PACKET_SIZE} bytes) ---")

    # Build packets: 4-byte seq + repeating pattern to fill STRESS_PACKET_SIZE
    packets_sent = {}
    test_sock.settimeout(0.001)  # Non-blocking for sends

    # Send packets with pacing to avoid overwhelming the VC send queue.
    # The VC drops incoming UDP when the send queue exceeds SEND_QUEUE_DROP_THRESHOLD.
    for seq in range(STRESS_PACKET_COUNT):
        # Header: 4-byte big-endian sequence number
        header = struct.pack(">I", seq)
        # Fill remainder with a byte pattern derived from seq (for corruption detection)
        fill_byte = (seq % 251).to_bytes(1, 'big')  # Prime number for variety
        payload = header + fill_byte * (STRESS_PACKET_SIZE - 4)
        packets_sent[seq] = payload
        test_sock.sendto(payload, ("127.0.0.1", CLIENT_UDP_PORT))
        # Pace sends: wait 5ms every 5 packets so the VC can drain
        if seq % 5 == 4:
            time.sleep(0.005)

    print(f"Sent {STRESS_PACKET_COUNT} packets, waiting for echoes...")

    # Collect responses
    test_sock.settimeout(STRESS_RECV_TIMEOUT)
    received = {}
    corrupted = []
    deadline = time.time() + STRESS_RECV_TIMEOUT

    while len(received) + len(corrupted) < STRESS_PACKET_COUNT and time.time() < deadline:
        try:
            remaining = deadline - time.time()
            if remaining <= 0:
                break
            test_sock.settimeout(min(remaining, 5.0))
            data, addr = test_sock.recvfrom(4096)

            if len(data) < 4:
                corrupted.append(("too_short", data))
                continue

            seq = struct.unpack(">I", data[:4])[0]

            if seq >= STRESS_PACKET_COUNT:
                corrupted.append(("bad_seq", data))
                continue

            # Verify content integrity
            expected = packets_sent.get(seq)
            if expected and data == expected:
                received[seq] = True
            else:
                corrupted.append(("content_mismatch", seq, len(data),
                                  len(expected) if expected else 0))
        except socket.timeout:
            break

    # Report results
    recv_count = len(received)
    corrupt_count = len(corrupted)
    lost_count = STRESS_PACKET_COUNT - recv_count - corrupt_count

    print(f"Results: {recv_count} received OK, {corrupt_count} corrupted, {lost_count} lost/timeout")

    if corrupted:
        print(f"FAILED: {corrupt_count} corrupted packets detected!")
        for i, c in enumerate(corrupted[:5]):
            print(f"  Corrupted #{i}: {c}")
        return False

    # Allow some packet loss (UDP is unreliable, VC drops under pressure) but zero corruption.
    loss_pct = (lost_count / STRESS_PACKET_COUNT) * 100
    print(f"Packet loss: {loss_pct:.1f}%")

    if recv_count == 0:
        print("FAILED: No packets received at all — pipeline likely broken")
        return False

    # The partial-send bug causes corruption, not packet loss.
    # Packet loss is expected due to UDP unreliability and VC queue pressure.
    # If we received a meaningful number of packets and none were corrupted,
    # the send path correctly handles partial writes.
    min_expected = max(20, STRESS_PACKET_COUNT * 0.1)
    if recv_count >= min_expected:
        print("Stress test PASSED (no corruption detected)")
        return True
    else:
        print(f"FAILED: Too few packets received ({recv_count} < {int(min_expected)})")
        return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="Build",
                        help="Path to build directory (default: Build)")
    args = parser.parse_args()

    build_dir = os.path.abspath(args.build_dir)
    server_bin, client_bin = find_binaries(build_dir)

    processes = []
    stop_echo = threading.Event()
    echo_thread = None

    try:
        echo_thread = threading.Thread(target=run_echo_server,
                                       args=(UDP_ECHO_PORT, stop_echo),
                                       daemon=True)
        echo_thread.start()

        server_proc = subprocess.Popen(
            [server_bin,
             f"--port={SERVER_TCP_PORT}",
             f"--udp-target-port={UDP_ECHO_PORT}",
             "--log-level=INFO"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        processes.append(server_proc)

        time.sleep(0.5)

        client_proc = subprocess.Popen(
            [client_bin,
             f"--peer-address=127.0.0.1",
             f"--peer-port={SERVER_TCP_PORT}",
             f"--local-udp-port={CLIENT_UDP_PORT}",
             f"--client-id={CLIENT_ID}",
             "--log-level=INFO"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        processes.append(client_proc)

        print(f"Server PID: {server_proc.pid}, Client PID: {client_proc.pid}")
        print(f"Server TCP port: {SERVER_TCP_PORT}")
        print(f"UDP echo port: {UDP_ECHO_PORT}")
        print(f"Client UDP port: {CLIENT_UDP_PORT}")
        print(f"Waiting {STARTUP_WAIT}s for connections to establish...")
        time.sleep(STARTUP_WAIT)

        test_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        test_sock.settimeout(RECV_TIMEOUT)

        # Test 1: Basic echo
        if not run_basic_test(test_sock):
            print("\n=== INTEGRATION TEST FAILED ===")
            sys.exit(1)

        # Test 2: Stress test for partial-send corruption
        if not run_stress_test(test_sock):
            print("\n=== INTEGRATION TEST FAILED ===")
            sys.exit(1)

        test_sock.close()
        print("\n=== INTEGRATION TEST PASSED ===")
        sys.exit(0)

    except socket.timeout:
        print(f"\n=== INTEGRATION TEST FAILED ===")
        print(f"Timeout waiting for echo response ({RECV_TIMEOUT}s)")
        sys.exit(1)
    except Exception as e:
        print(f"\n=== INTEGRATION TEST FAILED ===")
        print(f"Error: {e}")
        sys.exit(1)
    finally:
        print("Cleaning up...")
        for proc in processes:
            try:
                proc.terminate()
                proc.wait(timeout=5)
            except Exception:
                proc.kill()
                proc.wait(timeout=2)

        if stop_echo is not None:
            stop_echo.set()
        if echo_thread is not None:
            echo_thread.join(timeout=3)

        print("Cleanup complete.")


if __name__ == "__main__":
    main()
