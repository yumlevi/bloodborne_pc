#!/bin/sh
set -eu

compose_file=/opt/bbport-online/deploy/online_remote/compose.yaml
key=$(docker compose -f "$compose_file" exec -T headscale \
    headscale apikeys create --expiration 30d)
case "$key" in
    hskey-api-*) ;;
    *) echo "Headscale did not return a valid API key." >&2; exit 1 ;;
esac
printf '%s\n' "$key" | docker compose -f "$compose_file" exec -T -u 10001:10001 portal \
    sh -c 'set -eu; umask 077; tmp=$(mktemp /var/lib/portal/.headscale_api_key.XXXXXX); trap '\''rm -f "$tmp"'\'' EXIT; cat > "$tmp"; mv -f "$tmp" /var/lib/portal/headscale_api_key'
unset key
echo "Portal Headscale API key rotated."
