#!/usr/bin/env bash
# prepare-compose.sh: generate the integration-test docker-compose from the
# deployment compose file (compose.server.yml).
#
# Builds the server from server/Dockerfile (the same image server.yml
# publishes), unless SERVER_IMAGE names a prebuilt one. Doing this at
# generation time picks up deployment changes without manual edits.
#
# Usage:
#   [SERVER_PORT=30456] ./prepare-compose.sh [SERVER_IMAGE]
#
# SERVER_PORT is the host port; pick another one when a real relay server
# already listens on 30456 on this machine. STATUS_PORT (default SERVER_PORT + 1)
# publishes the room list on loopback.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_COMPOSE="${SCRIPT_DIR}/../../compose.server.yml"
OUTPUT_COMPOSE="${SCRIPT_DIR}/docker-compose.yml"
SERVER_IMAGE="${1:-}"
SERVER_PORT="${SERVER_PORT:-30456}"
STATUS_PORT="${STATUS_PORT:-$((SERVER_PORT + 1))}"

cp "$SOURCE_COMPOSE" "$OUTPUT_COMPOSE"

# Do not restart during the test lifecycle, publish on the chosen port, and
# give the networks test names so they never clash with a production stack.
SERVER_PORT="$SERVER_PORT" STATUS_PORT="$STATUS_PORT" yq -i eval '
  .services.ldn-server.restart = "no" |
  .services.ldn-server.ports = [strenv(SERVER_PORT) + ":30456", "127.0.0.1:" + strenv(STATUS_PORT) + ":30457"] |
  .services.redis.restart = "no" |
  .networks.default.name = "ldn-test" |
  .networks.metrics.name = "ldn-test-metrics"
' "$OUTPUT_COMPOSE"

if [ -n "$SERVER_IMAGE" ]; then
    SERVER_IMAGE="$SERVER_IMAGE" yq -i eval '.services.ldn-server.image = strenv(SERVER_IMAGE)' "$OUTPUT_COMPOSE"
else
    yq -i eval '
      del(.services.ldn-server.image) |
      .services.ldn-server.build = {"context": "../..", "dockerfile": "server/Dockerfile"}
    ' "$OUTPUT_COMPOSE"
fi

echo "Generated $OUTPUT_COMPOSE (server: ${SERVER_IMAGE:-built from server/Dockerfile}, port $SERVER_PORT)"
