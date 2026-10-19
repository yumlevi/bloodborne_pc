#!/bin/sh
set -eu

data_dir=/var/lib/shadnet
secret_file="$data_dir/registration_secret"
config_file="$data_dir/shadnet.cfg"
umask 077
mkdir -p "$data_dir/db"
cp /opt/shadnet/shadnet "$data_dir/shadnet"
if [ ! -e "$data_dir/worlds.cfg" ]; then
    cp /opt/shadnet/worlds.cfg "$data_dir/worlds.cfg"
fi
if [ ! -e "$data_dir/scoreboards.cfg" ]; then
    cp /opt/shadnet/scoreboards.cfg "$data_dir/scoreboards.cfg"
fi

if [ ! -s "$secret_file" ]; then
    head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n' > "$secret_file"
fi
secret=$(cat "$secret_file")
case "$secret" in
    *[!A-Za-z0-9_-]*|'') echo "Invalid registration secret" >&2; exit 1 ;;
esac

case "${SHADNET_SEAMLESS:-false}" in
    true|false) seamless=${SHADNET_SEAMLESS:-false} ;;
    *) echo "SHADNET_SEAMLESS must be true or false" >&2; exit 1 ;;
esac

cat > "$config_file.tmp" <<EOF
[General]
Host=0.0.0.0
UnsecuredPort=31313
Matching2Enabled=true
MatchingUdpPort=31314
WebApiPort=31315
BloodborneSeamlessCoop=$seamless
StatsEnabled=true
StatsPort=31320
StatsPath=stats
StatsCacheLife=10
RegistrationSecretKey=$secret
EOF
mv "$config_file.tmp" "$config_file"

echo "Starting shadNet (traditional=$([ "$seamless" = false ] && echo yes || echo no))"
exec "$data_dir/shadnet"
