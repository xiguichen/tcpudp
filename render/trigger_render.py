#!/usr/bin/env python3
"""Mac-side entry point for the Render tunnel.

    ./render/trigger_render.py && ./run_github.sh

What it does, in order:
  1. syncs the local checkout to the push branch
  2. wakes the Render instance by polling /healthz until it reports a hostname
  3. reconciles the local tunnel-info file against the live hostname
  4. pins the hostname to a fast Cloudflare edge IP
  5. starts the tunnel in the background (listening on 0.0.0.0, so other
     machines on the LAN can proxy through it) and prints the proxy URLs
  6. starts a keepalive that keeps hitting /healthz so Render cannot fall
     asleep mid-browse and take the tunnel's hostname down with it; if the
     hostname rotates anyway (a redeploy), it restarts the tunnel against
     the new one without a re-run

Why step 3 exists: Render's free tier sleeps after ~15 minutes idle and wipes
its filesystem, so the tunnel gets a brand-new hostname every time. The
hostname committed to git is therefore usually stale, and the only authority
on the current one is /healthz. The live hostname always wins.

Options:
  --health-url URL   override RENDER_HEALTH_URL
  --timeout SECONDS  how long to wait for a hostname (default 300)
  --no-wait          single poll instead of waiting

Env knobs (all optional): RENDER_HEALTH_URL, GITHUB_PUSH_BRANCH,
TCPUDP_INFO_DIR, TCPUDP_RUN_DIR, TCPUDP_PROXY_PORT, TCPUDP_LAN_IP,
POLL_INTERVAL, RECONCILE_TIMEOUT, HEALTH_TIMEOUT, HOSTS_FILE, CF_EDGE_IPS,
PING_COUNT, PING_TIMEOUT_SEC, START_TIMEOUT, KEEPALIVE_INTERVAL.

Python port of render/trigger_render.sh, same contract and same console
output. It is self-contained on purpose - no bash, no net.sh - because net.sh
is only a bash-shaped wrapper around ping and the hosts file, both of which
this file does directly (the way trigger_run.py already does). Stdlib only.
"""

import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Optional

SCRIPT_PATH = Path(__file__).resolve()
HERE = SCRIPT_PATH.parent
REPO_ROOT = HERE.parent

# Must agree with `name:` in render.yaml. Pinned by a test, because a mismatch
# here is a silent 404 rather than an error: Render answers an unknown service
# name with 404 and "x-render-routing: no-server", which reads as a dead
# service rather than a typo.
DEFAULT_HEALTH_URL = "https://tcpudp.onrender.com/healthz"

# net.sh's defaults, kept here so the pinning step behaves identically.
DEFAULT_EDGE_IPS = "162.159.38.209 104.17.213.97"

# Hidden child mode: `trigger_render.py --keepalive PID HOST` is the detached
# /healthz loop that start_keepalive spawns. The shell version got this from a
# backgrounded subshell; Python gets it from re-exec'ing this file.
KEEPALIVE_FLAG = "--keepalive"


# --------------------------------------------------------------------------- #
# Environment
# --------------------------------------------------------------------------- #
def env_str(name: str, default: str) -> str:
    """`${NAME:-default}`: empty counts as unset, same as the shell."""
    value = os.environ.get(name)
    return value if value else default


def env_int(name: str, default: int) -> int:
    raw = os.environ.get(name)
    if not raw:
        return default
    try:
        return int(raw.strip())
    except ValueError:
        return default


# --------------------------------------------------------------------------- #
# Output
# --------------------------------------------------------------------------- #
def log(message: str = "") -> None:
    print(message, flush=True)


def warn(message: str) -> None:
    print(f"WARNING: {message}", file=sys.stderr, flush=True)


def die(message: str) -> "None":
    print(f"ERROR: {message}", file=sys.stderr, flush=True)
    raise SystemExit(1)


def run(cmd: list, cwd: Optional[Path] = None) -> subprocess.CompletedProcess:
    """Run a command. Never raises, never prints, never checks for us."""
    try:
        return subprocess.run(
            cmd,
            cwd=str(cwd) if cwd else None,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
    except OSError:
        return subprocess.CompletedProcess(cmd, 127, "", "")


def port_open(port: int) -> bool:
    """`nc -z -G 1 127.0.0.1 PORT`: True when something is listening."""
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1.0):
            return True
    except OSError:
        return False


def pid_alive(pid: int) -> bool:
    """`kill -0`: an existence check that sends nothing."""
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True  # someone else's process; it exists
    except OSError:
        return False
    return True


# --------------------------------------------------------------------------- #
# Configuration
# --------------------------------------------------------------------------- #
class RenderTunnel:
    def __init__(self) -> None:
        self.health_url = env_str("RENDER_HEALTH_URL", DEFAULT_HEALTH_URL)
        self.branch = env_str("GITHUB_PUSH_BRANCH", "run")
        self.info_dir = env_str("TCPUDP_INFO_DIR", "github_run")
        self.run_dir = Path(env_str("TCPUDP_RUN_DIR", str(Path.home() / ".tcpudp")))
        self.server_port = env_int("TCPUDP_PROXY_PORT", 8080)
        self.timeout = 300
        self.no_wait = False
        self.poll_interval = env_int("POLL_INTERVAL", 5)
        self.reconcile_timeout = env_int("RECONCILE_TIMEOUT", 120)
        self.health_timeout = env_int("HEALTH_TIMEOUT", 10)
        self.start_timeout = env_int("START_TIMEOUT", 20)
        self.keepalive_interval = env_int("KEEPALIVE_INTERVAL", 180)
        # Pinning (net.sh's knobs).
        self.hosts_file = env_str("HOSTS_FILE", "/etc/hosts")
        self.edge_ips = env_str("CF_EDGE_IPS", DEFAULT_EDGE_IPS).split()
        self.ping_count = env_int("PING_COUNT", 5)
        self.ping_timeout_sec = env_int("PING_TIMEOUT_SEC", 2)
        # Filled by parse_args when re-exec'd in keepalive mode.
        self.keepalive_child: Optional[tuple] = None

    # -- CLI ---------------------------------------------------------------- #

    def parse_args(self, argv: list) -> None:
        args = list(argv)
        while args:
            arg = args.pop(0)
            if arg == KEEPALIVE_FLAG:
                if len(args) < 2:
                    die(f"{KEEPALIVE_FLAG} needs a pid and a hostname")
                pid_arg, host = args.pop(0), args.pop(0)
                try:
                    self.keepalive_child = (int(pid_arg), host)
                except ValueError:
                    die(f"{KEEPALIVE_FLAG} needs a numeric pid, got: {pid_arg}")
            elif arg == "--health-url":
                self.health_url = args.pop(0) if args else ""
            elif arg.startswith("--health-url="):
                self.health_url = arg.split("=", 1)[1]
            elif arg == "--timeout":
                self.timeout = self._timeout(args.pop(0) if args else "")
            elif arg.startswith("--timeout="):
                self.timeout = self._timeout(arg.split("=", 1)[1])
            elif arg == "--no-wait":
                self.no_wait = True
            elif arg in ("-h", "--help"):
                print(__doc__, end="", flush=True)
                raise SystemExit(0)
            else:
                die(f"unknown option: {arg}")

    @staticmethod
    def _timeout(raw: str) -> int:
        try:
            return int(raw.strip())
        except ValueError:
            die(f"--timeout needs a number of seconds, got: {raw!r}")

    # -- /healthz ----------------------------------------------------------- #

    def _health_json(self) -> Optional[dict]:
        """Fetch /healthz and parse it. Anything that is not JSON is 'not yet'.

        curl -fsS --max-time N: an HTTP error, a timeout, a refused connection
        and an HTML "spinning up" page are all normal early answers, so they all
        come back as None instead of raising. Render answers a cold start with
        an HTML page, and before the tunnel is up with a 200 carrying
        hostname:null; both mean "keep polling", not "broken".
        """
        try:
            with urllib.request.urlopen(self.health_url, timeout=self.health_timeout) as resp:
                body = resp.read()
        except (urllib.error.URLError, OSError, ValueError):
            return None
        if not body:
            return None
        try:
            data = json.loads(body)
        except ValueError:
            return None
        return data if isinstance(data, dict) else None

    def health_hostname(self) -> Optional[str]:
        """The hostname /healthz reports, or None when it is not up yet."""
        data = self._health_json()
        if data is None:
            return None
        host = data.get("hostname")
        return host if isinstance(host, str) and host else None

    def health_published(self) -> Optional[bool]:
        """The published flag, or None when the service does not report one.

        This is the difference between "the push has not landed yet" and "no
        push is coming". Without it, a service with GITHUB_PAT unset is polled
        for the full reconcile timeout on every run, always ending in the same
        fallback.
        """
        data = self._health_json()
        if data is None:
            return None
        published = data.get("published")
        return published if isinstance(published, bool) else None

    # -- git ---------------------------------------------------------------- #

    def git(self, *args: str) -> subprocess.CompletedProcess:
        return run(["git", *args])

    def branch_has_hostname(self, host: str) -> bool:
        """True when the push branch already carries this hostname.

        i.e. the supervisor's git write-back beat us to it.
        """
        result = self.git("show", f"origin/{self.branch}:{self.info_dir}/cloudflare.sh")
        if result.returncode != 0:
            return False
        return f"--hostname {host}" in result.stdout

    def checkout_info_file(self) -> None:
        self.git("checkout", self.branch, "--", f"{self.info_dir}/cloudflare.sh")

    # -- tunnel-info file --------------------------------------------------- #

    def write_local_tunnel_info(self, host: str) -> bool:
        """Write the file run_github.sh sources, in its one-line command format.

        It must never be reduced to a bare hostname: that file is executed, not
        parsed, so a bare name would fail with "command not found".
        """
        line = (
            f"cloudflared access tcp --url tcp://0.0.0.0:{self.server_port} "
            f"--hostname {host}\n"
        )
        try:
            info_dir = Path(self.info_dir)
            info_dir.mkdir(parents=True, exist_ok=True)
            (info_dir / "cloudflare.sh").write_text(line, encoding="utf-8")
            (info_dir / "cloudflare.bat").write_text(line, encoding="utf-8")
        except OSError:
            return False
        return True

    # -- LAN address -------------------------------------------------------- #

    def lan_ip(self) -> str:
        """The address other machines on the LAN should use for this Mac.

        That is the proxy-server field on their side. Override with
        TCPUDP_LAN_IP. Falls back to 127.0.0.1 when no address can be found,
        which simply means "this machine only".
        """
        override = os.environ.get("TCPUDP_LAN_IP")
        if override:
            return override

        interface = ""
        default_route = run(["route", "-n", "get", "default"])
        if default_route.returncode == 0:
            for line in default_route.stdout.splitlines():
                if "interface:" in line:
                    fields = line.split()
                    if len(fields) >= 2:
                        interface = fields[1]
                    break
        if not interface:
            return "127.0.0.1"
        addr = run(["ipconfig", "getifaddr", interface])
        return addr.stdout.strip() if addr.returncode == 0 and addr.stdout.strip() else "127.0.0.1"

    # -- DNS pinning (was net.sh) ------------------------------------------- #

    def ping_avg(self, ip: str) -> Optional[float]:
        """Average RTT in ms for one candidate, or None if it does not answer.

        macOS spells the summary `round-trip`, Linux `rtt`; both are accepted
        and the avg is the second field. A missing `ping` is "unreachable",
        exactly like net.sh's silent `2>/dev/null`.
        """
        cmd = ["ping", "-c", str(self.ping_count), "-W", str(self.ping_timeout_sec * 1000), ip]
        result = run(cmd)
        if result.returncode != 0:
            return None
        match = re.search(r"(?:rtt|round-trip)[^=]*=\s*[\d.]+/([\d.]+)/", result.stdout)
        if not match:
            return None
        try:
            return float(match.group(1))
        except ValueError:
            return None

    def probe_best_edge_ip(self) -> Optional[str]:
        """The fastest reachable candidate IP, or None when none answer.

        Candidates are probed in parallel because each one can burn its whole
        timeout, and a dead one would otherwise serialise the whole step.
        """
        if not self.edge_ips:
            return None
        with ThreadPoolExecutor(max_workers=len(self.edge_ips)) as pool:
            results = list(pool.map(self.ping_avg, self.edge_ips))
        # net.sh sorts "avg ip" numerically and takes the first line, so a tie
        # is broken by the IP string - keep the same ordering.
        reachable = [(rtt, ip) for ip, rtt in zip(self.edge_ips, results) if rtt is not None]
        if not reachable:
            return None
        return sorted(reachable, key=lambda pair: (pair[0], pair[1]))[0][1]

    def _hosts_lines(self) -> list:
        try:
            with open(self.hosts_file, "r", encoding="utf-8", errors="replace") as handle:
                return handle.read().splitlines()
        except OSError:
            return []

    def _install_hosts(self, text: str, append: bool) -> bool:
        """Write (or append) the hosts file, using sudo only when needed.

        The privilege check lives here so the tests never need sudo and
        production still works. A silent failure here is the worst kind: the
        user would be told DNS is pinned while still getting the slow CN edge
        IP this whole step exists to avoid.
        """
        if os.access(self.hosts_file, os.W_OK):
            try:
                with open(self.hosts_file, "a" if append else "w", encoding="utf-8") as handle:
                    handle.write(text)
                return True
            except OSError:
                return False
        sudo = shutil.which("sudo")
        if sudo:
            cmd = [sudo, "tee"] + (["-a"] if append else []) + [self.hosts_file]
            try:
                result = subprocess.run(cmd, input=text, text=True, capture_output=True)
            except OSError:
                return False
            return result.returncode == 0
        try:  # no sudo: try anyway, so the diagnosis is a real permission error
            with open(self.hosts_file, "a" if append else "w", encoding="utf-8") as handle:
                handle.write(text)
            return True
        except OSError:
            return False

    def pin_tunnel_hostname(self, host: str) -> tuple:
        """Point HOST at the fastest reachable edge IP.

        Returns (message, ok) where ok is False when the file could not be
        written. The message is one of:
          already-pinned        the correct line was already there
          pinned <ip>           a line was added
          fallback <ip>         no candidate answered, so the first was used
        """
        best_ip = self.probe_best_edge_ip()
        if best_ip:
            mode = "pinned"
        else:
            # Nothing answered. Pin the first candidate rather than leaving the
            # client to resolve the name itself and get whichever IP DNS feels
            # like handing out.
            best_ip = self.edge_ips[0] if self.edge_ips else DEFAULT_EDGE_IPS.split()[0]
            mode = "fallback"

        lines = self._hosts_lines()
        # Anchored at the start because the appended form is "<ip> <host>" with
        # no leading space.
        exact = re.compile(r"^\s*" + re.escape(best_ip) + r"\s+" + re.escape(host) + r"$")
        if any(exact.match(line) for line in lines):
            return ("already-pinned", True)

        stale = re.compile(r"\s" + re.escape(host) + r"\s*$")
        replacement = f"{best_ip} {host}\n"
        if any(stale.search(line) for line in lines):
            # A stale entry for this host exists. Rewrite the file without it,
            # so we never end up with two lines claiming the same name.
            kept = "".join(line + "\n" for line in lines if not stale.search(line))
            ok = self._install_hosts(kept + replacement, append=False)
        else:
            # Absent: a plain append is enough and cannot disturb the rest.
            ok = self._install_hosts(replacement, append=True)

        if not ok:
            print(
                f"pin_tunnel_hostname: could not write {self.hosts_file} (need sudo?)",
                file=sys.stderr,
                flush=True,
            )
            return ("", False)
        return (f"{mode} {best_ip}", True)

    def pin_dns(self, host: str) -> None:
        log("")
        log("=== Pinning DNS for lower latency ===")
        message, ok = self.pin_tunnel_hostname(host)
        if not ok:
            # Not fatal - the tunnel works without it - but say so loudly,
            # because the consequence is exactly the slow edge IP this step
            # exists to avoid.
            warn("DNS pinning FAILED. The client may be routed to a slow Cloudflare")
            warn("edge IP. Re-run with sudo, or add the line by hand:")
            warn(f"  echo '<edge-ip> {host}' | sudo tee -a {self.hosts_file}")
        elif message.startswith("already-pinned"):
            log("  /etc/hosts already correct")
        elif message.startswith("pinned"):
            log(f"  Added {message} to {self.hosts_file}")
        elif message.startswith("fallback"):
            warn(f"  No candidate edge IP answered; using {message}")
        else:
            log(f"  {message}")

    # -- tunnel process ----------------------------------------------------- #

    def start_tunnel(self, host: str) -> bool:
        """Run `cloudflared access tcp` in the background, always restarted.

        The previous run's daemon is killed via the pidfile and any stray tunnel
        matching our port is pkilled, so a fresh listener can bind. The listener
        is then probed for up to START_TIMEOUT half-seconds; a failure to come
        up is not fatal - cloudflared may still be registering with the edge -
        but it is reported together with the log path.
        """
        pidfile = self.run_dir / "cloudflared.pid"
        logfile = self.run_dir / "cloudflared.log"
        killed = False

        try:
            self.run_dir.mkdir(parents=True, exist_ok=True)
        except OSError:
            return False

        if pidfile.exists():
            try:
                prev = pidfile.read_text(encoding="utf-8", errors="replace").strip()
            except OSError:
                prev = ""
            try:
                pidfile.unlink()
            except OSError:
                pass
            if prev:
                try:
                    os.kill(int(prev), signal.SIGTERM)
                    killed = True
                    log(f"  stopped previous tunnel pid {prev}")
                except (ValueError, OSError):
                    pass

        stray = run(["pkill", "-f", f"cloudflared.*access tcp --url tcp://.*:{self.server_port}"])
        if stray.returncode == 0:
            killed = True

        if killed:
            # Let the old socket drain, so the bind below cannot race EADDRINUSE.
            time.sleep(1)

        # --no-autoupdate must precede the subcommand: `cloudflared access tcp`
        # on current releases rejects flags after the subcommand name.
        argv = [
            "cloudflared",
            "--no-autoupdate",
            "access",
            "tcp",
            "--url",
            f"tcp://0.0.0.0:{self.server_port}",
            "--hostname",
            host,
        ]
        try:
            with open(logfile, "w", encoding="utf-8") as handle:
                proc = subprocess.Popen(
                    argv,
                    stdout=handle,
                    stderr=subprocess.STDOUT,
                    cwd=str(REPO_ROOT),
                    start_new_session=True,  # nohup + `&`: it outlives this script
                )
        except OSError:
            warn(f"could not start cloudflared; see {logfile}")
            return False

        try:
            pidfile.write_text(f"{proc.pid}\n", encoding="utf-8")
        except OSError:
            pass
        log(f"  started pid {proc.pid}: {' '.join(argv)}")
        log(f"  log: {logfile}")

        for _ in range(self.start_timeout):
            if port_open(self.server_port):
                log(f"  listening on 0.0.0.0:{self.server_port}")
                return True
            if proc.poll() is not None:
                warn(f"tunnel process exited early; see {logfile}")
                return False
            time.sleep(0.5)
        warn(
            f"tunnel has not opened port {self.server_port} "
            f"after {self.start_timeout}x0.5s; see {logfile}"
        )
        return False

    # -- keepalive ---------------------------------------------------------- #

    def start_keepalive(self, tunnel_pid: int, host: str) -> None:
        """Spawn the detached /healthz loop that keeps the instance awake.

        Render's free tier sleeps after ~15 minutes with no traffic, and a wake
        wipes the filesystem and mints a new hostname - killing whatever tunnel
        pointed at the old one. While the tunnel is up this loop keeps hitting
        /healthz so the instance cannot fall asleep mid-browse, and it restarts
        the tunnel if the hostname rotates anyway. It watches the tunnel pid and
        stops when the tunnel dies, so it never keeps warming a dead hostname.
        KEEPALIVE_INTERVAL=0 disables it entirely. Replaced on every run like
        the tunnel, via its own pidfile.
        """
        pidfile = self.run_dir / "keepalive.pid"
        logfile = self.run_dir / "keepalive.log"

        try:
            self.run_dir.mkdir(parents=True, exist_ok=True)
        except OSError:
            return

        if pidfile.exists():
            try:
                prev = pidfile.read_text(encoding="utf-8", errors="replace").strip()
            except OSError:
                prev = ""
            try:
                pidfile.unlink()
            except OSError:
                pass
            if prev:
                try:
                    os.kill(int(prev), signal.SIGTERM)
                except (ValueError, OSError):
                    pass

        if self.keepalive_interval == 0:
            log("  keepalive disabled (KEEPALIVE_INTERVAL=0)")
            return

        argv = [
            sys.executable,
            str(SCRIPT_PATH),
            KEEPALIVE_FLAG,
            str(tunnel_pid),
            host,
            # The loop needs the effective URL, which the shell version got for
            # free by inheriting HEALTH_URL into its subshell.
            "--health-url",
            self.health_url,
        ]
        try:
            with open(logfile, "w", encoding="utf-8") as handle:
                child = subprocess.Popen(
                    argv,
                    stdout=handle,
                    stderr=subprocess.STDOUT,
                    cwd=str(REPO_ROOT),
                    start_new_session=True,
                )
        except OSError:
            warn(f"could not start keepalive; see {logfile}")
            return

        try:
            pidfile.write_text(f"{child.pid}\n", encoding="utf-8")
        except OSError:
            pass
        log(f"  keepalive pid {child.pid} (hits {self.health_url} every {self.keepalive_interval}s)")

    def run_keepalive(self, tunnel_pid: int, host: str) -> int:
        """The detached loop itself, in the re-exec'd child."""
        wanted = host
        watched = tunnel_pid
        while pid_alive(watched):
            # Sleep first: a keepalive must not judge the hostname on the
            # instant it starts - one interval of "seen it, it's fine" is what
            # makes re-runs and cold starts deterministic.
            time.sleep(self.keepalive_interval)
            seen = self.health_hostname()
            # An empty answer - a cold start still spinning up - is "not yet",
            # not "changed", and is ignored.
            if seen and seen != wanted:
                log(f"  hostname rotated: {wanted} -> {seen}; restarting tunnel")
                wanted = seen
                self.write_local_tunnel_info(seen)
                self.start_tunnel(seen)
                # start_tunnel replaced the process; watch the fresh pid so the
                # loop keeps running until THAT tunnel dies.
                try:
                    fresh = (self.run_dir / "cloudflared.pid").read_text(
                        encoding="utf-8", errors="replace"
                    ).strip()
                except OSError:
                    fresh = ""
                if fresh:
                    try:
                        watched = int(fresh)
                    except ValueError:
                        pass
        return 0

    # -- main flow ---------------------------------------------------------- #

    def run(self) -> int:
        log("=== Render tunnel ===")
        log(f"Repo:   {REPO_ROOT}")
        log(f"Health: {self.health_url}")

        # 1. sync the checkout. A failure here is not fatal: the reconcile step
        #    below works from the live hostname and can repair the file locally.
        head = self.git("rev-parse", "--abbrev-ref", "HEAD")
        current_branch = head.stdout.strip() if head.returncode == 0 else ""
        if current_branch != self.branch:
            log(f"Switching to {self.branch}")
            if self.git("checkout", self.branch).returncode != 0:
                warn(f"could not switch to {self.branch}; continuing on {current_branch}")
        if self.git("pull", "--quiet", "origin", self.branch).returncode != 0:
            warn(f"could not pull {self.branch}; continuing with the local checkout")

        # 2. wake the instance and wait for a hostname.
        log("")
        log(f"Waiting for a tunnel hostname (up to {self.timeout}s)...")
        host: Optional[str] = None
        if self.no_wait:
            host = self.health_hostname()
            if not host:
                die("no hostname available and --no-wait was given")
        else:
            deadline = time.monotonic() + self.timeout
            while time.monotonic() < deadline:
                host = self.health_hostname()
                if host:
                    break
                print(".", end="", flush=True)
                time.sleep(self.poll_interval)
            print()
            if not host:
                die(f"timed out after {self.timeout}s waiting for a tunnel hostname from {self.health_url}")
        log(f"Tunnel hostname: {host}")

        # 3. reconcile the local tunnel-info file against the live hostname.
        log("")
        if self.branch_has_hostname(host):
            log(f"The {self.branch} branch already carries this hostname.")
            self.checkout_info_file()
        else:
            # Only wait when a publish could still land. The service reports
            # published=false when its own git write-back did not happen, and
            # that is the normal state for a deployment without GITHUB_PAT -
            # polling for a push that is never coming just costs the full
            # timeout. An absent field (a service predating it) keeps the old
            # behaviour, since a push may be in flight.
            if self.health_published() is False:
                log(f"{self.branch} is stale and the service reports no publish, so there is")
                log("nothing to wait for. Taking the live hostname.")
            else:
                log(f"Branch does not have it yet; waiting up to {self.reconcile_timeout}s for a publish...")
                rdeadline = time.monotonic() + self.reconcile_timeout
                while time.monotonic() < rdeadline:
                    self.git("fetch", "--quiet", "origin", self.branch)
                    if self.branch_has_hostname(host):
                        break
                    time.sleep(self.poll_interval)

            if self.branch_has_hostname(host):
                log("The supervisor published it; using the committed file.")
                self.checkout_info_file()
            else:
                warn(f"{self.branch} is stale: it does not carry {host} (this is expected when")
                warn("GITHUB_PAT is not set on the Render service). Writing the live")
                warn(f"hostname into {self.info_dir}/cloudflare.sh locally so the client works.")
                if not self.write_local_tunnel_info(host):
                    die(f"could not write {self.info_dir}/cloudflare.sh")

        # 4. pin DNS to a fast edge IP.
        self.pin_dns(host)

        # 5. start the tunnel in the background, then say where to point things.
        log("")
        log("=== Starting the tunnel ===")
        self.start_tunnel(host)

        # 6. keep the instance awake: a free-tier sleep wipes the filesystem and
        #    mints a new hostname, which silently kills the tunnel we just
        #    started.
        try:
            tunnel_pid = (self.run_dir / "cloudflared.pid").read_text(
                encoding="utf-8", errors="replace"
            ).strip()
        except OSError:
            tunnel_pid = ""
        if tunnel_pid:
            log("")
            log("=== Keeping the instance awake ===")
            try:
                pid = int(tunnel_pid)
            except ValueError:
                pid = 0
            if pid:
                self.start_keepalive(pid, host)

        log("")
        log("Proxy ready - point a browser or app at either address:")
        log(f"  this Mac:       http://127.0.0.1:{self.server_port}")
        log(f"  other machines: http://{self.lan_ip()}:{self.server_port}")
        log(f"  check:          curl -4 -x http://127.0.0.1:{self.server_port} https://ifconfig.me")
        return 0


def main(argv: list) -> int:
    tunnel = RenderTunnel()
    tunnel.parse_args(argv)

    try:
        os.chdir(REPO_ROOT)
    except OSError:
        die(f"cannot enter {REPO_ROOT}")

    if tunnel.keepalive_child is not None:
        return tunnel.run_keepalive(*tunnel.keepalive_child)

    return tunnel.run()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
