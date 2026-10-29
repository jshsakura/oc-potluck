#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
export LDN_SERVER_IMAGE=${1:?Pass the locally built server image}
export LDN_BIND_IP=127.0.0.1 LDN_PORT=30457 LDN_STATUS_PORT=30458
project=potluck-server-verify
resolved=$(mktemp /tmp/potluck-server-verify.XXXXXX.yml)
compose=(docker compose -p "$project" -f "$resolved")
cleanup() {
    "${compose[@]}" down -v || true
    rm -f "$resolved"
}
trap cleanup EXIT

bash scripts/ci/run-limited.sh --setup
docker compose -p "$project" -f compose.server.yml -f server/compose.test.yml config > "$resolved"
"${compose[@]}" up -d --wait --wait-timeout 90
for container in $("${compose[@]}" ps -q); do
    pid=$(docker inspect --format '{{.State.Pid}}' "$container")
    path=$(sudo cut -d: -f3 "/proc/$pid/cgroup")
    case "$path" in
        /potluck.slice/potluck-ci.slice/potluck-ci-tests.slice/*) ;;
        *) echo 'Test container escaped the shared memory budget' >&2; exit 1 ;;
    esac
    [[ $(docker inspect --format '{{.Config.User}}' "$container") == 10001:10001 ]]
    [[ $(docker inspect --format '{{.HostConfig.ReadonlyRootfs}}' "$container") == true ]]
done
bash scripts/ci/run-limited.sh make -C tests/integration -j1 all
bash scripts/ci/run-limited.sh env INTEGRATION_COMPOSE_FILE="$resolved" \
    tests/integration/run_integration_tests 127.0.0.1 30457

# The status page: off above, so "/" was a 404 there. On, it serves only its own files.
LDN_WEB_ENABLED=true LDN_SITE_ORIGIN=https://relay.example.com \
    docker compose -p "$project" -f compose.server.yml -f server/compose.test.yml config > "$resolved"
"${compose[@]}" up -d --wait --wait-timeout 90
base=http://127.0.0.1:30458
fail() { echo "Status page check failed: $*" >&2; exit 1; }
code() { curl -s -o /dev/null -w '%{http_code}' "$@" || true; }
page=$(curl -fsS -D - "$base/") || fail "GET / did not answer"
grep -qi '^Content-Type: text/html' <<<"$page" || fail "/ is not HTML"
grep -qi '^Content-Security-Policy: ' <<<"$page" || fail "/ has no CSP"
grep -q 'https://relay.example.com/og-image.png' <<<"$page" || fail "share card origin was not filled in"
! grep -q '__SITE_ORIGIN__' <<<"$page" || fail "origin placeholder left in the page"
rooms=$(curl -fsS -D - "$base/api/rooms") || fail "GET /api/rooms did not answer"
grep -qi '^Content-Type: application/json' <<<"$rooms" || fail "/api/rooms is not JSON"
[[ $(code "$base/potluck.css?v=1") == 200 ]] || fail "a page asset with a query string was not served"
for path in /DESIGN.md /og-image.svg /../etc/passwd /%2e%2e/etc/passwd /nope; do
    got=$(code --path-as-is "$base$path")
    [[ $got == 404 ]] || fail "$path answered $got, not 404"
done
got=$(code -X POST -d x "$base/")
[[ $got == 405 ]] || fail "POST / answered $got, not 405"

# Limits: a burst past the budget is refused, an oversized request is cut off.
codes=$(for _ in $(seq 80); do code "$base/rooms"; echo; done | sort | uniq -c)
grep -q ' 429$' <<<"$codes" || fail "no 429 after a burst: $codes"
sleep 3
huge=$(head -c 20000 /dev/zero | tr '\0' a)
got=$(code -H "X-Pad: $huge" "$base/rooms")
[[ $got == 000 ]] || fail "an oversized request answered $got"
echo "Status page checks passed"
