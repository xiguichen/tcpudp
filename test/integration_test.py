#!/usr/bin/env python3
"""
Integration and performance test for the TCP/UDP proxy pipeline.

Test flow:
  test_script → UDP → client → TCP (VC) → server → UDP → echo_server → UDP
  → server → TCP (VC) → client → UDP → test_script

Modes:
  --mode=local     Run everything on localhost (default).
  --mode=remote    Server/echo are already running remotely; only start client locally.

Performance metrics:
  - Per-packet round-trip latency (min/max/avg/p50/p95/p99)
  - Throughput (Mbps) during stress test
  - Packet loss rate
  - Corruption count

Usage:
  python test/integration_test.py [--build-dir PATH] [--mode local|remote]
      [--server-addr ADDR] [--server-port PORT] [--echo-port PORT]
      [--client-port PORT] [--client-id ID] [--perf-only]
"""

import argparse
import os
import platform
import socket
import statistics
import struct
import subprocess
import sys
import threading
import time


# ---- Default ports (non-conflicting with the production server on :7001) ----
SERVER_TCP_PORT = 17001
UDP_ECHO_PORT = 17002
CLIENT_UDP_PORT = 17003
CLIENT_ID = 99
STARTUP_WAIT = 3
RECV_TIMEOUT = 15

# Stress / performance test parameters
PERF_PACKET_COUNT = 200
PERF_PACKET_SIZE = 1400


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

    for server_bin, client_bin in search_paths:
        print(f"Server binary: {server_bin} exists: {os.path.exists(server_bin)}")
        print(f"Client binary: {client_bin} exists: {os.path.exists(client_bin)}")
    raise FileNotFoundError("Could not find server and client binaries")


def run_echo_server(port, stop_event):
    """Simple UDP echo server. Runs until stop_event is set."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", port))
    sock.settimeout(0.5)
    while not stop_event.is_set():
        try:
            data, addr = sock.recvfrom(65536)
            sock.sendto(data, addr)
        except socket.timeout:
            continue
        except OSError:
            break
    sock.close()


# ---------------------------------------------------------------------------
# Performance test
# ---------------------------------------------------------------------------

def run_perf_test(test_sock, server_addr, client_port,
                  packet_count=PERF_PACKET_COUNT,
                  packet_size=PERF_PACKET_SIZE):
    """
    Send numbered packets as fast as reasonably possible, measure per-packet
    round-trip latency, and compute aggregate throughput.
    """
    print(f"\n{'='*60}")
    print(f"Performance Test: {packet_count} packets × {packet_size} bytes")
    print(f"Target: {server_addr}:{client_port}")
    print(f"{'='*60}")

    latencies = []
    lost = 0
    corrupted = 0
    sent_packets = {}

    test_sock.settimeout(0.001)  # non-blocking sends

    # ---- Send phase ----
    send_start = time.monotonic()
    for seq in range(packet_count):
        timestamp = time.monotonic()
        # 8-byte timestamp (double) + 4-byte seq + fill pattern
        header = struct.pack(">dI", timestamp, seq)
        fill_byte = (seq % 251).to_bytes(1, "big")
        payload = header + fill_byte * (packet_size - len(header))
        sent_packets[seq] = payload
        test_sock.sendto(payload, (server_addr, client_port))
        # Pace: slight delay every 10 packets to avoid overwhelming the VC send queue
        if seq % 10 == 9:
            time.sleep(0.002)
    send_end = time.monotonic()
    send_duration = send_end - send_start
    print(f"Sent {packet_count} packets in {send_duration:.2f}s "
          f"({packet_count / send_duration:.0f} pkt/s, "
          f"{packet_count * packet_size * 8 / send_duration / 1e6:.2f} Mbps send rate)")

    # ---- Receive phase ----
    print(f"Waiting for echoes (timeout={PERF_PACKET_COUNT * 2}s)...")
    test_sock.settimeout(2.0)
    received_seqs = set()
    deadline = time.monotonic() + PERF_PACKET_COUNT * 2
    recv_start = time.monotonic()

    while len(received_seqs) + corrupted < packet_count and time.monotonic() < deadline:
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            test_sock.settimeout(min(remaining, 5.0))
            data, _addr = test_sock.recvfrom(65536)

            if len(data) < 12:
                corrupted += 1
                continue

            send_ts, seq = struct.unpack(">dI", data[:12])

            if seq >= packet_count:
                corrupted += 1
                continue

            expected = sent_packets.get(seq)
            if expected and data == expected:
                rtt = (time.monotonic() - send_ts) * 1000.0  # ms
                # Sanity check: ignore negative/impossible RTTs (clock skew or
                # packet misdelivery, which is rare on localhost but possible).
                if 0.0 <= rtt < 600_000.0:
                    latencies.append(rtt)
                received_seqs.add(seq)
            else:
                corrupted += 1
        except socket.timeout:
            continue

    recv_end = time.monotonic()
    recv_duration = recv_end - recv_start

    # ---- Results ----
    recv_count = len(latencies)
    lost = packet_count - recv_count - corrupted

    print(f"\n--- Performance Results ---")
    print(f"  Packets: sent={packet_count} received={recv_count} "
          f"lost={lost} corrupted={corrupted}")

    if recv_count == 0:
        print("  FAILED: No packets received — pipeline is broken")
        return {"passed": False, "latencies": [], "throughput_mbps": 0.0}

    latencies.sort()
    avg_lat = statistics.mean(latencies)
    min_lat = latencies[0]
    max_lat = latencies[-1]
    p50 = latencies[int(len(latencies) * 0.50)]
    p95 = latencies[int(len(latencies) * 0.95)]
    p99 = latencies[int(len(latencies) * 0.99)]
    loss_pct = (lost / packet_count) * 100.0
    goodput_mbps = (recv_count * packet_size * 8) / recv_duration / 1e6 if recv_duration > 0 else 0.0

    print(f"  Latency (ms): min={min_lat:.2f} avg={avg_lat:.2f} max={max_lat:.2f} "
          f"p50={p50:.2f} p95={p95:.2f} p99={p99:.2f}")
    print(f"  Loss: {loss_pct:.1f}%")
    print(f"  Goodput: {goodput_mbps:.2f} Mbps")
    print(f"  Corruption: {corrupted}")

    # Determine pass/fail
    passed = True
    if recv_count < max(20, packet_count * 0.5):
        print(f"  FAILED: Too few packets received ({recv_count} < {int(packet_count * 0.5)})")
        passed = False
    if corrupted > 0:
        print(f"  FAILED: {corrupted} corrupted packets detected")
        passed = False

    if passed:
        print(f"  Performance test PASSED")

    return {
        "passed": passed,
        "latencies": latencies,
        "throughput_mbps": goodput_mbps,
        "loss_pct": loss_pct,
        "corrupted": corrupted,
        "recv_count": recv_count,
        "avg_lat_ms": avg_lat,
        "p50_lat_ms": p50,
        "p95_lat_ms": p95,
        "p99_lat_ms": p99,
        "send_rate_mbps": packet_count * packet_size * 8 / send_duration / 1e6,
    }


# ---------------------------------------------------------------------------
# Basic correctness test
# ---------------------------------------------------------------------------

def run_basic_test(test_sock, server_addr, client_port):
    """Send a single message and verify echo."""
    print(f"\n--- Basic Echo Test ---")
    msg = b"Hello from integration test!"
    print(f"Sending: {msg!r}")
    test_sock.sendto(msg, (server_addr, client_port))

    test_sock.settimeout(RECV_TIMEOUT)
    data, addr = test_sock.recvfrom(4096)
    print(f"Received: {data!r} from {addr}")

    if data != msg:
        print(f"FAILED: Expected {msg!r}, got {data!r}")
        return False
    print("Basic echo test PASSED")
    return True


# ---------------------------------------------------------------------------
# Stress test (corruption detection)
# ---------------------------------------------------------------------------

def run_stress_test(test_sock, server_addr, client_port,
                    packet_count=PERF_PACKET_COUNT,
                    packet_size=PERF_PACKET_SIZE):
    """
    Send many large packets rapidly to stress the VC pipeline.
    Each packet has a 4-byte sequence number followed by a known pattern.
    This triggers partial TCP sends under buffer pressure.
    """
    print(f"\n--- Stress Test ({packet_count} packets × {packet_size} bytes) ---")

    packets_sent = {}
    test_sock.settimeout(0.001)

    for seq in range(packet_count):
        header = struct.pack(">I", seq)
        fill_byte = (seq % 251).to_bytes(1, "big")
        payload = header + fill_byte * (packet_size - 4)
        packets_sent[seq] = payload
        test_sock.sendto(payload, (server_addr, client_port))
        if seq % 5 == 4:
            time.sleep(0.005)

    print(f"Sent {packet_count} packets, waiting for echoes...")

    test_sock.settimeout(30)
    received = {}
    corrupted = []
    deadline = time.monotonic() + 30

    while len(received) + len(corrupted) < packet_count and time.monotonic() < deadline:
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            test_sock.settimeout(min(remaining, 5.0))
            data, _addr = test_sock.recvfrom(65536)

            if len(data) < 4:
                corrupted.append(("too_short", data))
                continue

            seq = struct.unpack(">I", data[:4])[0]

            if seq >= packet_count:
                corrupted.append(("bad_seq", data))
                continue

            expected = packets_sent.get(seq)
            if expected and data == expected:
                received[seq] = True
            else:
                corrupted.append(("content_mismatch", seq, len(data),
                                  len(expected) if expected else 0))
        except socket.timeout:
            break

    recv_count = len(received)
    corrupt_count = len(corrupted)
    lost_count = packet_count - recv_count - corrupt_count

    print(f"Results: {recv_count} received OK, {corrupt_count} corrupted, {lost_count} lost/timeout")

    if corrupted:
        print(f"FAILED: {corrupt_count} corrupted packets detected!")
        for i, c in enumerate(corrupted[:5]):
            print(f"  Corrupted #{i}: {c}")
        return False

    loss_pct = (lost_count / packet_count) * 100
    print(f"Packet loss: {loss_pct:.1f}%")

    if recv_count == 0:
        print("FAILED: No packets received at all — pipeline likely broken")
        return False

    min_expected = max(20, packet_count * 0.1)
    if recv_count >= min_expected:
        print("Stress test PASSED (no corruption detected)")
        return True
    else:
        print(f"FAILED: Too few packets received ({recv_count} < {int(min_expected)})")
        return False


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Integration and performance test for the tcpudp proxy pipeline")
    parser.add_argument("--build-dir", default="Build",
                        help="Path to build directory (default: Build)")
    parser.add_argument("--mode", choices=["local", "remote"], default="local",
                        help="local = run server+echo locally; remote = only run client (default: local)")
    parser.add_argument("--server-addr", default="127.0.0.1",
                        help="Server address (default: 127.0.0.1)")
    parser.add_argument("--server-port", type=int, default=SERVER_TCP_PORT,
                        help=f"Server TCP port (default: {SERVER_TCP_PORT})")
    parser.add_argument("--echo-port", type=int, default=UDP_ECHO_PORT,
                        help=f"UDP echo port (default: {UDP_ECHO_PORT})")
    parser.add_argument("--client-port", type=int, default=CLIENT_UDP_PORT,
                        help=f"Client UDP bind port (default: {CLIENT_UDP_PORT})")
    parser.add_argument("--client-id", type=int, default=CLIENT_ID,
                        help=f"Client ID (default: {CLIENT_ID})")
    parser.add_argument("--perf-only", action="store_true",
                        help="Run only the performance test (skip basic/stress)")
    parser.add_argument("--packet-count", type=int, default=PERF_PACKET_COUNT,
                        help=f"Packets for perf/stress test (default: {PERF_PACKET_COUNT})")
    parser.add_argument("--packet-size", type=int, default=PERF_PACKET_SIZE,
                        help=f"Packet size for perf/stress test (default: {PERF_PACKET_SIZE})")
    args = parser.parse_args()

    build_dir = os.path.abspath(args.build_dir)
    server_bin, client_bin = find_binaries(build_dir)

    processes = []
    stop_echo = threading.Event()
    echo_thread = None

    try:
        # ---- Start echo server (local mode) ----
        if args.mode == "local":
            echo_thread = threading.Thread(
                target=run_echo_server,
                args=(args.echo_port, stop_echo),
                daemon=True)
            echo_thread.start()

        # ---- Start C++ server (local mode) ----
        if args.mode == "local":
            server_proc = subprocess.Popen(
                [server_bin,
                 f"--port={args.server_port}",
                 f"--udp-target-port={args.echo_port}",
                 "--log-level=INFO"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            processes.append(server_proc)
            time.sleep(0.5)

        # ---- Start C++ client ----
        client_proc = subprocess.Popen(
            [client_bin,
             f"--peer-address={args.server_addr}",
             f"--peer-port={args.server_port}",
             f"--local-udp-port={args.client_port}",
             f"--client-id={args.client_id}",
             "--log-level=INFO"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        processes.append(client_proc)

        print(f"Server PID: {client_proc.pid}")
        if args.mode == "local":
            for p in processes:
                print(f"  PID: {p.pid}")
        print(f"Mode: {args.mode}")
        print(f"Server: {args.server_addr}:{args.server_port}")
        print(f"UDP echo: {args.echo_port}")
        print(f"Client UDP: {args.client_port}")
        print(f"Client ID: {args.client_id}")
        print(f"Waiting {STARTUP_WAIT}s for connections to establish...")
        time.sleep(STARTUP_WAIT)

        test_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        test_sock.settimeout(RECV_TIMEOUT)

        all_passed = True

        if not args.perf_only:
            # Test 1: Basic echo
            if not run_basic_test(test_sock, args.server_addr, args.client_port):
                all_passed = False

            # Test 2: Stress test (corruption check)
            if not run_stress_test(test_sock, args.server_addr, args.client_port,
                                   args.packet_count, args.packet_size):
                all_passed = False

        # Test 3: Performance test (always run)
        perf_result = run_perf_test(test_sock, args.server_addr, args.client_port,
                                    args.packet_count, args.packet_size)
        if not perf_result["passed"]:
            all_passed = False

        test_sock.close()

        # ---- Summary ----
        print(f"\n{'='*60}")
        print(f"SUMMARY")
        print(f"{'='*60}")
        if perf_result["recv_count"] > 0:
            print(f"  Latency: avg={perf_result['avg_lat_ms']:.1f}ms "
                  f"p50={perf_result['p50_lat_ms']:.1f}ms "
                  f"p95={perf_result['p95_lat_ms']:.1f}ms "
                  f"p99={perf_result['p99_lat_ms']:.1f}ms")
            print(f"  Throughput: {perf_result['throughput_mbps']:.2f} Mbps goodput "
                  f"({perf_result['send_rate_mbps']:.2f} Mbps send rate)")
            print(f"  Loss: {perf_result['loss_pct']:.1f}%")
            print(f"  Corrupted: {perf_result['corrupted']}")

        # Machine-parseable JSON summary
        import json
        json_result = {
            "passed": all_passed,
            "mode": args.mode,
            "server_addr": args.server_addr,
            "server_port": args.server_port,
            "latency_ms": {
                "avg": round(perf_result["avg_lat_ms"], 2),
                "p50": round(perf_result["p50_lat_ms"], 2),
                "p95": round(perf_result["p95_lat_ms"], 2),
                "p99": round(perf_result["p99_lat_ms"], 2),
            },
            "throughput_mbps": round(perf_result["throughput_mbps"], 2),
            "loss_pct": round(perf_result["loss_pct"], 1),
            "packets_sent": PERF_PACKET_COUNT if not args.perf_only else args.packet_count,
            "packets_received": perf_result["recv_count"],
            "corrupted": perf_result["corrupted"],
        }
        print(f"\nJSON: {json.dumps(json_result)}")

        if all_passed:
            print(f"\n=== ALL TESTS PASSED ===")
            sys.exit(0)
        else:
            print(f"\n=== SOME TESTS FAILED ===")
            sys.exit(1)

    except socket.timeout:
        print(f"\n=== INTEGRATION TEST FAILED ===")
        print(f"Timeout waiting for echo response ({RECV_TIMEOUT}s)")
        sys.exit(1)
    except Exception as e:
        print(f"\n=== INTEGRATION TEST FAILED ===")
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        print("Cleaning up...")
        for proc in processes:
            try:
                proc.terminate()
                proc.wait(timeout=5)
            except Exception:
                try:
                    proc.kill()
                    proc.wait(timeout=2)
                except Exception:
                    pass

        if stop_echo is not None:
            stop_echo.set()
        if echo_thread is not None:
            echo_thread.join(timeout=3)

        print("Cleanup complete.")


if __name__ == "__main__":
    main()
