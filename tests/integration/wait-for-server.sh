#!/usr/bin/env bash
# wait-for-server.sh — Wait for LdnServer to accept TCP connections
# Uses python3 (no nc dependency)
# Usage: ./wait-for-server.sh [host] [port] [timeout_seconds]
set -e

HOST="${1:-localhost}"
PORT="${2:-30456}"
TIMEOUT="${3:-60}"

echo "[wait] Waiting for LdnServer at ${HOST}:${PORT} (timeout: ${TIMEOUT}s)"

# Ready means a connection is accepted and still open a second later, which
# a half-started listener or a port forwarder with nothing behind it fails.
# It does not prove the server answers LDN Initialize yet.
check_tcp() {
    python3 - "$HOST" "$PORT" <<'PY' 2>/dev/null
import socket, sys
s = socket.create_connection((sys.argv[1], int(sys.argv[2])), timeout=2)
s.settimeout(1)
try:
    if s.recv(1) == b"":
        sys.exit(1)  # closed at once: nothing behind the proxy yet
except socket.timeout:
    pass  # still open after a second: the server accepted us
sys.exit(0)
PY
}

# SECONDS counts wall time, so each probe's own connect and recv time is
# part of the limit, not on top of it
SECONDS=0
while ! check_tcp; do
    if [ "$SECONDS" -ge "$TIMEOUT" ]; then
        echo "[wait] Timed out: server not ready after ${TIMEOUT}s" >&2
        exit 1
    fi
    sleep 1
    printf "[wait] Still waiting... (%ds)\r" "$SECONDS"
done

echo "[wait] Server is ready at ${HOST}:${PORT} (took ~${SECONDS}s)"