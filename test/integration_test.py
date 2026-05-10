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
import socket
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


def find_binaries(build_dir):
    server_bin = os.path.join(build_dir, "bin", "server")
    client_bin = os.path.join(build_dir, "bin", "udp_client")
    if not os.path.exists(server_bin):
        server_bin = os.path.join(build_dir, "server", "server")
    if not os.path.exists(client_bin):
        client_bin = os.path.join(build_dir, "client", "udp_client")
    if not os.path.exists(server_bin) or not os.path.exists(client_bin):
        print(f"Server binary: {server_bin} exists: {os.path.exists(server_bin)}")
        print(f"Client binary: {client_bin} exists: {os.path.exists(client_bin)}")
        raise FileNotFoundError("Could not find server and client binaries")
    return server_bin, client_bin


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

        print(f"Sending test data: {TEST_MSG!r}")
        test_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        test_sock.settimeout(RECV_TIMEOUT)
        test_sock.sendto(TEST_MSG, ("127.0.0.1", CLIENT_UDP_PORT))

        data, addr = test_sock.recvfrom(4096)
        test_sock.close()

        print(f"Received: {data!r} from {addr}")

        if data == TEST_MSG:
            print("\n=== INTEGRATION TEST PASSED ===")
            print("Data echoed back correctly through the pipeline.")
            sys.exit(0)
        else:
            print(f"\n=== INTEGRATION TEST FAILED ===")
            print(f"Expected: {TEST_MSG!r}")
            print(f"Received: {data!r}")
            sys.exit(1)

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
