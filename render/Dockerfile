# Runtime image for the Render tunnel.
#
# Build context is render/ (set by rootDir in render.yaml), so the COPY paths
# below are relative to this folder. Deliberately not copied: net.sh, which only
# the Mac-side trigger script uses.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# git      - clones the repo at startup and publishes the tunnel hostname back
# curl     - fetches the server release and the ipinfo geo lookup
# tar      - unpacks the release
# python3  - runs keepalive.py
# procps   - provides pkill, used to reap the server and tunnel on restart
# iproute2 - provides ss (port check) and ip (WireGuard route verification)
# wireguard-tools - provides wg-quick, for the optional WireGuard egress tunnel
RUN apt-get update \
  && apt-get install -y --no-install-recommends \
    ca-certificates git curl tar python3 procps iproute2 wireguard-tools \
  && rm -rf /var/lib/apt/lists/*

# Latest rather than a pinned version: a stale pin would turn into a surprise
# when cloudflared publishes a new release and the old tag is dropped.
RUN curl -fsSL -o /usr/local/bin/cloudflared \
      https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64 \
  && chmod +x /usr/local/bin/cloudflared

COPY render_supervisor.sh /usr/local/bin/render_supervisor.sh
COPY keepalive.py /usr/local/bin/keepalive.py
RUN chmod +x /usr/local/bin/render_supervisor.sh

# Render injects PORT (10000 by default); keepalive.py binds it.
EXPOSE 10000

CMD ["/usr/local/bin/render_supervisor.sh"]
