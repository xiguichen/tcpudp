#!/usr/bin/env python3
"""
Trigger a GitHub Actions run by pushing a trigger file to the 'run' branch,
then poll via git fetch until the Action completes and commits cloudflare info.

The GitHub Action (run.yml) triggers on any push to 'run' that modifies
trigger/*. When it finishes, it commits an "Auto-update" with the cloudflare
tunnel scripts.

This script uses ONLY git — no API access needed (works through firewalls
and restrictive networks where git operations succeed but GitHub API calls
might be blocked).

Supports Linux, macOS, and Windows.
On Windows, DNS pinning (modifying the hosts file) requires running as
Administrator; otherwise the script will print the line to add manually.

Usage:
    python trigger_run.py [--timeout SECONDS] [--no-wait]

After completion:
    ./run_github.sh   # Linux/macOS
    run_github.bat    # Windows
"""

import argparse
import ctypes
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone

# --------------------------------------------------------------------------- #
# Platform / constants
# --------------------------------------------------------------------------- #
PLATFORM   = platform.system()          # "Linux", "Darwin", "Windows", ...
IS_WINDOWS = PLATFORM == "Windows"
IS_MACOS   = PLATFORM == "Darwin"

HOSTS_FILE = (
    os.path.join(
        os.environ.get("SystemRoot", r"C:\Windows"),
        "System32", "drivers", "etc", "hosts",
    )
    if IS_WINDOWS else "/etc/hosts"
)

BAD_REGIONS   = {"Virginia"}
MAX_RETRIES   = 5
POLL_INTERVAL = 10

CANDIDATE_IPS    = ["162.159.38.209", "104.17.213.97"]
PING_COUNT       = 5
PING_TIMEOUT_SEC = 2


# --------------------------------------------------------------------------- #
# Helpers
# --------------------------------------------------------------------------- #
def eprint(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)


def run(cmd, check=True, capture=True, **kwargs):
    """Run a command. Return stdout (stripped) if capture else ''."""
    result = subprocess.run(
        cmd,
        shell=isinstance(cmd, str),
        capture_output=capture,
        text=True,
        encoding="utf-8",
        errors="replace",
        **kwargs,
    )
    if check and result.returncode != 0:
        eprint(f"ERROR: command failed ({result.returncode}): {cmd}")
        if capture and result.stderr:
            eprint(result.stderr)
        sys.exit(1)
    return result.stdout.strip() if capture else ""


def git(*args, check=True, capture=True):
    return run(["git", *args], check=check, capture=capture)


def is_admin():
    if IS_WINDOWS:
        try:
            return bool(ctypes.windll.shell32.IsUserAnAdmin())
        except Exception:
            return False
    return os.geteuid() == 0


# --------------------------------------------------------------------------- #
# Branch / trigger
# --------------------------------------------------------------------------- #
def ensure_run_branch():
    branch = git("branch", "--show-current")
    if branch != "run":
        print("=== Switching to run branch ===")
        git("checkout", "run", capture=False)


def pull_run():
    print("=== Pulling latest run branch ===")
    git("pull", "origin", "run", capture=False)


def create_and_push_trigger():
    timestamp    = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    trigger_path = f"trigger/{int(time.time())}"

    os.makedirs("trigger", exist_ok=True)
    with open(trigger_path, "w") as f:
        f.write(f"trigger {timestamp}\n")

    print("=== Triggering GitHub run via git push ===")
    git("add", trigger_path, capture=False)
    git("commit", "-m", f"trigger: {timestamp}", capture=False)
    git("push", "origin", "run", capture=False)

    push_commit = git("rev-parse", "HEAD")
    print(f"  Trigger commit: {push_commit[:7]}")
    print(f"  Timestamp:      {timestamp}")
    return push_commit


# --------------------------------------------------------------------------- #
# Polling
# --------------------------------------------------------------------------- #
def wait_for_action(push_commit, timeout):
    print()
    print(f"=== Waiting for GitHub Action to complete (timeout: {timeout}s) ===")
    print("  polling", end="", flush=True)

    start = time.time()
    while True:
        elapsed = int(time.time() - start)
        if elapsed > timeout:
            print()
            print(f"ERROR: Timed out after {timeout}s.")
            print("The workflow may still be running. Check:")
            print("  https://github.com/xiguichen/tcpudp/actions")
            print()
            print("To fetch results when it finishes:")
            print("  git pull origin run")
            return False

        # Tolerate transient fetch failures during polling.
        subprocess.run(
            ["git", "fetch", "origin", "run"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        remote_head = git("rev-parse", "origin/run", check=False)
        remote_msg  = git("log", "--format=%s", "-1", "origin/run", check=False)

        if remote_head and remote_head != push_commit and "Auto-update" in remote_msg:
            print()
            print("  GitHub Action completed!")
            return True

        print(".", end="", flush=True)
        time.sleep(POLL_INTERVAL)


# --------------------------------------------------------------------------- #
# Region check
# --------------------------------------------------------------------------- #
def read_region():
    path = "github_run/region.json"
    if not os.path.isfile(path):
        return None
    try:
        with open(path) as f:
            return json.load(f)
    except (json.JSONDecodeError, OSError):
        return None


def is_bad_region():
    data = read_region()
    if not data:
        return False
    region = data.get("region", "")
    if region in BAD_REGIONS:
        city    = data.get("city", "")
        country = data.get("country", "")
        print(f"  Bad region: {city}, {region}, {country} — re-triggering...")
        return True
    return False


# --------------------------------------------------------------------------- #
# Results display
# --------------------------------------------------------------------------- #
def extract_hostname():
    path = "github_run/cloudflare.sh"
    if not os.path.isfile(path):
        return "unknown"
    try:
        with open(path) as f:
            content = f.read()
    except OSError:
        return "unknown"
    m = re.search(r"\S*\.trycloudflare\.com", content)
    return m.group(0) if m else "unknown"


def display_results():
    if not os.path.isfile("github_run/cloudflare.sh"):
        print("ERROR: github_run/cloudflare.sh not found.")
        print("The run may still be in progress or may have failed.")
        print("Check: https://github.com/xiguichen/tcpudp/actions")
        sys.exit(1)

    print()
    print("=== Cloudflare tunnel ready ===")
    print()

    hostname = extract_hostname()
    with open("github_run/cloudflare.sh") as f:
        command = f.read().strip()
    print(f"  Hostname: {hostname}")
    print(f"  Command:  {command}")
    print()

    if os.path.isfile("github_run/run_info.json"):
        with open("github_run/run_info.json") as f:
            print(f"  Run info: {f.read().strip()}")
    if os.path.isfile("github_run/region.json"):
        with open("github_run/region.json") as f:
            print(f"  Region:   {f.read().strip()}")

    return hostname


# --------------------------------------------------------------------------- #
# DNS pinning
# --------------------------------------------------------------------------- #
def ping_avg(ip, count=PING_COUNT, timeout_sec=PING_TIMEOUT_SEC):
    """Return average RTT in ms, or None on failure. Cross-platform."""
    if IS_WINDOWS:
        # -n count ; -w timeout per reply in ms
        cmd = ["ping", "-n", str(count), "-w", str(timeout_sec * 1000), ip]
    elif IS_MACOS:
        # -c count ; -W timeout in ms (macOS-specific)
        cmd = ["ping", "-c", str(count), "-W", str(timeout_sec * 1000), ip]
    else:
        # Linux: -c count ; -W timeout in seconds
        cmd = ["ping", "-c", str(count), "-W", str(timeout_sec), ip]

    try:
        # NOTE: do NOT force encoding here — Windows ping output is in the
        # system OEM code page; forcing UTF-8 would garble Chinese "时间=".
        result = subprocess.run(
            cmd, capture_output=True, text=True,
            timeout=timeout_sec * count + 5,
        )
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return None

    if IS_WINDOWS:
        # English: "Reply from X: bytes=32 time=3ms TTL=56"  (or time<1ms)
        # Chinese: "来自 X 的回复: 字节=32 时间=3ms TTL=56"
        times = re.findall(r"(?:time|时间)\s*[=<]\s*(\d+)\s*ms",
                           result.stdout, re.IGNORECASE)
        if not times:
            return None
        return sum(int(t) for t in times) / len(times)
    else:
        # Linux: "rtt min/avg/max/mdev = 1.234/5.678/9.012/0.123 ms"
        # macOS: "round-trip min/avg/max/stddev = 1.234/5.678/9.012/0.123 ms"
        m = re.search(r"(?:rtt|round-trip)[^=]*=\s*[\d.]+/([\d.]+)/",
                      result.stdout)
        return float(m.group(1)) if m else None


def hosts_has_entry(hostname):
    try:
        with open(HOSTS_FILE, encoding="utf-8", errors="replace") as f:
            for line in f:
                if hostname in line.split():
                    return True
    except OSError:
        pass
    return False


def add_to_hosts(ip, hostname):
    entry = f"{ip} {hostname}\n"

    if IS_WINDOWS:
        if not is_admin():
            eprint("  Warning: not running as Administrator.")
            eprint(f"  To pin DNS, add this line to {HOSTS_FILE} manually:")
            eprint(f"    {entry.strip()}")
            eprint("  Or re-run this script from an elevated (Admin) terminal.")
            return False
        try:
            with open(HOSTS_FILE, "a", encoding="utf-8") as f:
                f.write(entry)
            return True
        except PermissionError:
            eprint(f"  Warning: access denied to {HOSTS_FILE}.")
            eprint(f"  Add this line manually: {entry.strip()}")
            return False
        except OSError as e:
            eprint(f"  ERROR: failed to update hosts file: {e}")
            return False
    else:
        try:
            result = subprocess.run(
                ["sudo", "tee", "-a", HOSTS_FILE],
                input=entry, text=True, capture_output=True,
            )
        except FileNotFoundError:
            eprint("  Warning: 'sudo' not found.")
            eprint(f"  Add this line to {HOSTS_FILE} manually:")
            eprint(f"    {entry.strip()}")
            return False
        if result.returncode != 0:
            eprint(f"  Warning: failed to update {HOSTS_FILE}.")
            eprint(f"  Add this line manually: {entry.strip()}")
            return False
        return True


def pin_dns(tunnel_host):
    print()
    print("=== Pinning DNS for lower latency ===")
    if hosts_has_entry(tunnel_host):
        print(f"  Hosts file already has an entry for {tunnel_host}")
        return

    print(f"  Probing {len(CANDIDATE_IPS)} candidate IPs "
          f"({PING_COUNT} pings each, in parallel)...")

    with ThreadPoolExecutor(max_workers=len(CANDIDATE_IPS)) as ex:
        results = list(ex.map(ping_avg, CANDIDATE_IPS))

    paired = sorted(
        zip(CANDIDATE_IPS, results),
        key=lambda x: (x[1] is None, x[1] if x[1] is not None else 0),
    )

    for ip, rtt in paired:
        if rtt is None:
            print(f"    {ip:<16}  unreachable")
        else:
            print(f"    {ip:<16}  {rtt:.3g} ms")

    best_ip, best_rtt = paired[0]
    if best_rtt is None:
        best_ip = CANDIDATE_IPS[0]
        print(f"  Warning: all candidates unreachable, falling back to {best_ip}")

    if add_to_hosts(best_ip, tunnel_host):
        print(f"  Added {best_ip} {tunnel_host} to {HOSTS_FILE}")


# --------------------------------------------------------------------------- #
# Main
# --------------------------------------------------------------------------- #
def main():
    if shutil.which("git") is None:
        eprint("ERROR: 'git' command not found.")
        return 1

    try:
        env_timeout = int(os.environ.get("TIMEOUT", "300"))
    except ValueError:
        env_timeout = 300

    parser = argparse.ArgumentParser(
        description="Trigger a GitHub Actions run and wait for results."
    )
    parser.add_argument(
        "--timeout", type=int, default=env_timeout,
        help=f"Polling timeout in seconds (default: {env_timeout})",
    )
    parser.add_argument(
        "--no-wait", action="store_true",
        help="Trigger the run but don't wait for completion",
    )
    args = parser.parse_args()

    ensure_run_branch()

    found = False
    for attempt in range(1, MAX_RETRIES + 1):
        if attempt > 1:
            print()
            print(f"=== Retry {attempt}/{MAX_RETRIES} ===")

        pull_run()
        push_commit = create_and_push_trigger()

        if args.no_wait:
            print()
            print("Run triggered. To fetch results later, run:")
            print("  git pull origin run")
            return 0

        if not wait_for_action(push_commit, args.timeout):
            return 1

        print()
        print("=== Fetching cloudflare tunnel info ===")
        git("pull", "origin", "run", capture=False)

        if is_bad_region():
            continue

        found = True
        break

    if not found:
        print()
        print(f"ERROR: All {MAX_RETRIES} attempts landed in bad regions. Giving up.")
        print("Check: https://github.com/xiguichen/tcpudp/actions")
        return 1

    hostname = display_results()

    tunnel_host = hostname
    if hostname.startswith("https://"):
        tunnel_host = hostname[len("https://"):]
    if tunnel_host and tunnel_host != "unknown":
        pin_dns(tunnel_host)

    print()
    print("Next step:  ./run_github.sh" if not IS_WINDOWS
          else r"Next step:  run_github.bat")
    return 0


if __name__ == "__main__":
    sys.exit(main())
