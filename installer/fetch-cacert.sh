#!/bin/sh
# The CA bundle the app verifies GitHub with. The repository ignores *.pem, so it is fetched, not committed.
set -eu
cd "$(dirname "$0")"
curl -fsSL https://curl.se/ca/cacert.pem -o romfs/cacert.pem
echo "romfs/cacert.pem: $(grep -c 'BEGIN CERTIFICATE' romfs/cacert.pem) certificates"
