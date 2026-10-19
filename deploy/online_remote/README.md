# Remote online test host (82.157.168.20)

This Compose project is intentionally independent of the local Docker test project.
Do not use `down -v`: named volumes hold shadNet accounts, Headscale nodes,
registration keys and certificate state. Only TCP 80/443 are published publicly.
Game ports 31313/TCP, 31314/UDP, 31315/TCP and portal 18080/TCP bind to
127.0.0.1 and are published to the private tailnet by the server bridge.

The bootstrap portal and Headscale share `https://82.157.168.20`. The Nginx
proxy exposes only registration/status/device-key routes from the portal and
blocks its administrator routes. Headscale's administrator API is not proxied.
The portal automatically approves unverified QQ/nickname applications, as
requested for this trial; that is **not identity verification**. Limit the
number of testers, monitor abuse, and replace with a verified registration
process before broad public release. The Headscale policy limits private
traffic to bbport ports. Default public Tailscale DERP is still enabled:
control-plane hosting is private, but relay is not fully self-hosted yet.

The traditional endpoint now opts into shadNet's relaxed seamless summon
matching. This changes sign discovery/claim handling, not the game's own
session lifetime and not BBSC's game hooks. Its container, database, bridge
and 100.64.0.1 identity remain separate from the BBSC node at 100.64.0.2.
The public `/v1/presence` route accepts authenticated seamless-client
heartbeats for the portal list. When replacing a host-mounted `nginx.conf`
using an atomic file replacement, recreate `proxy`: an Nginx reload alone
keeps Docker's bind mount on the old file inode.

Initial provisioning (run from this directory on the remote Ubuntu host):

1. Confirm the host firewall/security group allows inbound TCP 80/443. Never
   publish the four raw game/portal ports.
2. Request a Let's Encrypt short-lived IP certificate with Certbot 5.4+ in
   standalone mode, after accepting its terms. Use the `letsencrypt` Compose
   volume at `/etc/letsencrypt`. IP certificates last about six days.
3. `docker compose -f compose.yaml up -d --build headscale shadnet portal proxy`.
4. `docker compose -f compose.yaml exec -T headscale headscale users create players`.
   Confirm the numeric user ID and set `HEADSCALE_USER_ID` in Compose if not 1.
5. Run `sh initialize-auto-enrollment.sh`. It creates a 30-day Headscale API
   key and pipes it directly into the portal's
   `/var/lib/portal/headscale_api_key` file without printing it or putting it
   into shell history. It preserves an existing key. Install and enable
   `bbport-key-rotate.timer` so the 30-day key is rotated weekly.
6. Build the bridge, generate a one-time server preauth key, run
   `bbport-net --config /etc/bbport/server-net.json --auth-key-stdin` once to
   establish its dedicated persistent identity, then start the `bridge`
   service normally. Record the bridge's 100.64.0.0/10 address for the
   launcher's `overlay_server=`.
7. Install and enable `bbport-cert-renew.timer`. Confirm renewal with a
   staging dry-run and inspect timer status regularly.

Only after checking remote `/health`, `/v1/status`, a real launcher
enrollment and a two-player round-trip should a distributable launcher be
configured with this host. Keep the game connection behind the overlay;
never fall back to the public IP for TCP 31313.

For the online bell/STUN fix, update the server `bridge` and `shadnet` images
together; the new `BBST` source envelope is intentionally not compatible with
the older server image. Keep named volumes and the Headscale identity intact.
Because `portal` uses `network_mode: service:shadnet`, recreating `shadnet`
without recreating `portal` leaves the portal attached to the old network
namespace and `/v1/status` returns HTTP 502. After the shadNet/bridge update,
run `docker compose -f compose.yaml up -d --no-deps --force-recreate portal`
and verify `https://82.157.168.20/v1/status` before inviting testers.
After both players reinstall a client runtime built with the matching STUN
target fix, a bell test should produce two distinct `STUN ping` entries with
`100.64.0.0/10:31316` endpoints in shadNet logs. `GetServerFeatures` and
message visibility alone are insufficient. If STUN entries appear but the
bell still times out, inspect `RequestSignalingInfos` and player-to-player
UDP 31316 next; the policy file already permits that port.
