#!/usr/bin/env bash
# Installs gscope-daemon as a systemd service.
#
# Usage:
#   sudo ./install-service.sh [path/to/gscope-daemon]
#
# Env vars (all optional):
#   GSCOPE_PORT          Listen port (default: 8081; 80 works too, see below)
#   GSCOPE_INTERVAL_MS    Sampling interval (default: 1000)
#   GSCOPE_HISTORY_SIZE   Backlog size (default: 120)
#   GSCOPE_USER           System user the service runs as (default: gscope,
#                       created if missing)
#
# The binary is a single self-contained executable (the Angular UI is
# embedded via objcopy, see ../README.md), so nothing but this one file
# needs to be copied to the target machine.
#
# Binding low ports (<1024, e.g. 80) without running as root: this grants
# the service user only CAP_NET_BIND_SERVICE via systemd's
# AmbientCapabilities, rather than running gscope-daemon as root -- least
# privilege, same technique nginx/caddy packages use.
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo "error: must be run as root (sudo) -- installs a systemd unit and a binary under /usr/local/bin" >&2
    exit 1
fi

SOURCE_BINARY="${1:-./gscope-daemon}"
if [[ ! -x "$SOURCE_BINARY" ]]; then
    echo "error: '$SOURCE_BINARY' not found or not executable" >&2
    echo "usage: sudo $0 [path/to/gscope-daemon]" >&2
    exit 1
fi

GSCOPE_PORT="${GSCOPE_PORT:-8081}"
GSCOPE_INTERVAL_MS="${GSCOPE_INTERVAL_MS:-1000}"
GSCOPE_HISTORY_SIZE="${GSCOPE_HISTORY_SIZE:-120}"
GSCOPE_USER="${GSCOPE_USER:-gscope}"

INSTALL_PATH=/usr/local/bin/gscope-daemon
UNIT_PATH=/etc/systemd/system/gscope-daemon.service

if ! id "$GSCOPE_USER" >/dev/null 2>&1; then
    useradd --system --no-create-home --shell /usr/sbin/nologin "$GSCOPE_USER"
    echo "created system user '$GSCOPE_USER'"
fi

install -m 0755 "$SOURCE_BINARY" "$INSTALL_PATH"

cat > "$UNIT_PATH" <<EOF
[Unit]
Description=gscope metrics daemon
After=network.target

[Service]
Type=simple
User=${GSCOPE_USER}
ExecStart=${INSTALL_PATH} --port ${GSCOPE_PORT} --interval-ms ${GSCOPE_INTERVAL_MS} --history-size ${GSCOPE_HISTORY_SIZE}
Restart=on-failure
RestartSec=2
AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
NoNewPrivileges=yes

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable --now gscope-daemon

echo "installed and started gscope-daemon (port ${GSCOPE_PORT}), running as '${GSCOPE_USER}'"
echo "check status with: systemctl status gscope-daemon"
echo "check logs with:   journalctl -u gscope-daemon -f"
