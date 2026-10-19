#!/bin/sh
set -eu

compose_file=/opt/bbport-online/deploy/online_remote/compose.yaml
key_file=/var/lib/portal/headscale_api_key

if docker compose -f "$compose_file" exec -T portal test -s "$key_file"; then
    echo "Portal API key already exists; preserving it."
    exit 0
fi

key=$(docker compose -f "$compose_file" exec -T headscale \
    headscale apikeys create --expiration 30d)
case "$key" in
    hskey-api-*) ;;
    *) echo "Headscale did not return a valid API key." >&2; exit 1 ;;
esac
printf '%s\n' "$key" | docker compose -f "$compose_file" exec -T -u 10001:10001 portal \
    sh -c 'umask 077; cat > /var/lib/portal/headscale_api_key'
unset key
echo "Automatic enrollment initialized; rotate the portal API key before 30 days."
