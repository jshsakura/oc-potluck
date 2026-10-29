#!/usr/bin/env bash
# Publish web/ to a web root and point the share card (og:url, og:image) at the
# site's own origin. The repository keeps a placeholder instead of a real host.
# Usage: deploy/web-publish.sh https://relay.example.com /srv/www/relay.example.com
set -euo pipefail

origin=${1:?Pass the site origin, for example https://relay.example.com}
dest=${2:?Pass the web root to publish into}
[[ $origin =~ ^https://[A-Za-z0-9.-]+$ ]] || { echo "The origin must look like https://host" >&2; exit 1; }

src="$(cd "$(dirname "$0")/../web" && pwd)"
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

cp -r "$src"/. "$stage"/
rm -f "$stage/og-image.svg"
sed -i "s|__SITE_ORIGIN__|$origin|g" "$stage/index.html"
rsync -a --delete "$stage"/ "$dest"/
chmod -R a+rX "$dest"
echo "Published $src to $dest for $origin"
