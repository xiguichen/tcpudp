#!/usr/bin/env bash
#
# Shared network helpers for the Render tunnel path.
#
# Sourced by trigger_render.sh; defines functions only and has no side effects
# at load time, so it is safe to source from a test.
#
# Env knobs (all optional, all defaulted):
#   CF_EDGE_IPS     space-separated candidate Cloudflare edge IPs
#   PING_COUNT      pings per candidate (default 5)
#   PING_TIMEOUT_SEC per-candidate timeout (default 2)
#   HOSTS_FILE      hosts file to modify (default /etc/hosts)
#
# HOSTS_FILE exists so the pinning logic is testable without sudo. In normal use
# it is left alone and /etc/hosts is written via sudo when it is not writable.

# --------------------------------------------------------------------------
# probe_best_edge_ip
#
# Echo "<ip> <avg_rtt_ms>" for the fastest reachable candidate, or exit 1 and
# echo nothing when none of them answer. Candidate IPs are probed in parallel
# because each one can burn the full timeout.
# --------------------------------------------------------------------------
probe_best_edge_ip() {
  local ips=${CF_EDGE_IPS:-"162.159.38.209 104.17.213.97"}
  local count=${PING_COUNT:-5}
  local timeout_ms=$((${PING_TIMEOUT_SEC:-2} * 1000))
  local tmpdir ip avg i best

  tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/tcpudp-probe.XXXXXX") || return 1
  i=0
  for ip in $ips; do
    (
      # Bare `ping` so a test can stub it on PATH. awk takes the average out of
      # the min/avg/max/stddev field; Linux spells the line `rtt`, macOS
      # `round-trip`, so accept either.
      avg=$(ping -c "$count" -W "$timeout_ms" "$ip" 2>/dev/null |
        awk -F' = ' '/^(round-trip|rtt)/{split($2,a,"/"); print a[2]; exit}')
      if [ -n "$avg" ]; then
        printf '%s %s\n' "$avg" "$ip"
      else
        printf '999999 %s unreachable\n' "$ip"
      fi
    ) >"$tmpdir/$i" &
    i=$((i + 1))
  done
  wait

  best=$(sort -n "$tmpdir"/* 2>/dev/null | head -1)
  rm -rf "$tmpdir"

  case "$best" in
    '' | *unreachable*) return 1 ;;
  esac
  printf '%s %s\n' "$(printf '%s' "$best" | awk '{print $2}')" \
    "$(printf '%s' "$best" | awk '{print $1}')"
  return 0
}

# --------------------------------------------------------------------------
# _hosts_write / _hosts_append
#
# Install (or append) file contents, using sudo only when the current user
# cannot write the target. Keeping the privilege check here means the tests
# need no sudo and production still works.
#
# Both propagate the underlying command's exit status. The caller must check
# it: a silent failure here is the worst kind, because the user would be told
# DNS is pinned while still being handed the slow CN edge IP that this whole
# function exists to avoid.
# --------------------------------------------------------------------------
_hosts_write() {
  local dest=$1 src=$2
  if [ -w "$dest" ] 2>/dev/null; then
    cat "$src" >"$dest"
  elif command -v sudo >/dev/null 2>&1; then
    sudo tee "$dest" >/dev/null <"$src"
  else
    cat "$src" >"$dest"
  fi
}

_hosts_append() {
  local dest=$1 src=$2
  if [ -w "$dest" ] 2>/dev/null; then
    cat "$src" >>"$dest"
  elif command -v sudo >/dev/null 2>&1; then
    sudo tee -a "$dest" >/dev/null <"$src"
  else
    cat "$src" >>"$dest"
  fi
}

# --------------------------------------------------------------------------
# pin_tunnel_hostname HOST
#
# Point HOST at the fastest reachable Cloudflare edge IP, so the client is not
# at the mercy of whatever DNS hands back (a known problem in CN).
#
# Echoes one of:
#   already-pinned   the correct line was already there; file untouched
#   pinned <ip>      a line was added
#   fallback <ip>    no candidate answered, so the first was used anyway
# --------------------------------------------------------------------------
pin_tunnel_hostname() {
  local host=${1:-}
  if [ -z "$host" ]; then
    printf 'pin_tunnel_hostname: no hostname given\n' >&2
    return 1
  fi

  local hosts_file=${HOSTS_FILE:-/etc/hosts}
  local probe rc=0
  probe=$(probe_best_edge_ip) || rc=$?

  local best_ip mode
  if [ "$rc" -eq 0 ]; then
    best_ip=${probe%% *}
    mode=pinned
  else
    # Nothing answered. Pin the first candidate rather than leaving the client
    # to resolve the name itself and get whichever IP DNS feels like.
    best_ip=${CF_EDGE_IPS:-"162.159.38.209 104.17.213.97"}
    best_ip=${best_ip%% *}
    mode=fallback
  fi

  # Already exactly right? Then leave the file alone. The line is anchored at
  # the start because the appended form is "<ip> <host>" with no leading space.
  if [ -n "$(grep -E "^[[:space:]]*$best_ip[[:space:]]+$host\$" "$hosts_file" 2>/dev/null | head -1)" ]; then
    printf 'already-pinned\n'
    return 0
  fi

  if grep -qE "[[:space:]]$host\$" "$hosts_file" 2>/dev/null; then
    # A stale entry for this host exists. Rewrite the file without it so we
    # never end up with two lines claiming the same name.
    local tmp
    tmp=$(mktemp "${TMPDIR:-/tmp}/tcpudp-hosts.XXXXXX") || return 1
    grep -vE "[[:space:]]$host\$" "$hosts_file" >"$tmp" 2>/dev/null || true
    printf '%s %s\n' "$best_ip" "$host" >>"$tmp"
    if ! _hosts_write "$hosts_file" "$tmp"; then
      rm -f "$tmp"
      printf 'pin_tunnel_hostname: could not write %s (need sudo?)\n' "$hosts_file" >&2
      return 1
    fi
    rm -f "$tmp"
  else
    # Absent: a plain append is enough and cannot disturb the rest of the file.
    local tmp
    tmp=$(mktemp "${TMPDIR:-/tmp}/tcpudp-hosts.XXXXXX") || return 1
    printf '%s %s\n' "$best_ip" "$host" >"$tmp"
    if ! _hosts_append "$hosts_file" "$tmp"; then
      rm -f "$tmp"
      printf 'pin_tunnel_hostname: could not write %s (need sudo?)\n' "$hosts_file" >&2
      return 1
    fi
    rm -f "$tmp"
  fi

  printf '%s %s\n' "$mode" "$best_ip"
  return 0
}
