# Isolated BBSC-compatible seamless laboratory

This is **experimental, not a finished online seamless release**. It pairs an
independent shadNet matchmaking server with BBSC's game API/relay and an
optional BBSC game-hook DLL. Without the DLL, it remains relaxed matchmaking
only. The exact bbport + BBSC combination still needs a two-PC test through
death, lamp travel, boss clear and reconnect before it can be called seamless.

Run this Compose project only on a test machine. It is independent of
`deploy/online_remote`: its own named data volume contains a separate account
database and registration secret. The separate BBSC game API has its own data
volume. By default all five lab ports bind only to loopback: 32313/TCP,
32314/UDP, 32315/TCP, 32443/TCP and 32444/TCP. The production traditional
service and its 31313/31314/31315 ports are untouched. Never copy production
volumes here or run `down -v` against production.

The game API image checks out pinned BBSC source revision
`315f76c38b1ca5843b082f93f47927a9193ad94a` during build; Docker must
reach GitHub once. If upstream `main` moves, the hash check deliberately
stops the build until the new source is reviewed. The upstream game API source is GPL-2.0-or-later. This
server image does **not** bundle BBSC DLLs or game files. The optional
v1.7.12+ Windows offline launcher bundles the verified BBport client mod only.

After Docker Desktop is running:

```powershell
docker compose -f deploy/seamless_lab/compose.yaml config --quiet
docker compose -f deploy/seamless_lab/compose.yaml up -d --build
curl.exe http://127.0.0.1:32315/status
curl.exe http://127.0.0.1:32443/bb-eu/ss.info
```

In the launcher, use native LAN mode with server `127.0.0.1:32313`, turn off
"this PC is the LAN server", and enable the experimental seamless-matching
checkbox. The v1.7.12+ offline EXE installs the verified BBSC components
automatically; older builds require downloading the official
[`BBSC-0.0.1-BBport.zip`](https://github.com/Classmg22/BBSC/releases/tag/v0.0.1_hotfix_2)
(SHA-256 `c8e19e72d957d5278f1aec6ea907e75383e6fac3a2060a490ff3e1e913f1ffe8`)
and using the launcher's **导入 BBSC 官方测试包** button. The launcher verifies the
archive and all six required files (the two binaries, `LICENSE.txt`, `NOTICE.txt`,
`SUPPORTERS.txt`, and `mod.json`) before placing them in
`<installed runtime>/runtime/bbsc/`; it never overwrites different existing
files. Then enable the separate "实验性 BBSC 游戏补丁" checkbox. A missing or
modified file fails closed before loading the DLL. Do not put these files in
the production runtime or enable this checkbox with the public online mode. The launcher points only
this test launch at the lab's game API and relay. The shadNet WebAPI port is
TCP port + 2 and STUN is TCP port + 1. Register a **new test account** on this
isolated server before attempting a bell test. On your own test machine, read
the generated registration secret with
`docker compose -f deploy/seamless_lab/compose.yaml exec -T shadnet cat /var/lib/shadnet/registration_secret`
and paste it into the launcher's LAN registration field; do not share it in
logs or screenshots. Production credentials are not
copied. The launcher uses the same player-data directory and character save
selected in **存档管理** as ordinary native play. It forces a save backup before
and after experimental sessions; keep a separate copy as well, because the
BBSC game hook may change the live save. Older `seamless-lab/` test data is
left untouched and is not merged into the ordinary save automatically.

The shadNet image applies `deploy/shadnet/patches/seamless-rejoin.patch` and
runs its summon-broker regression test during the build. Only seamless mode
reopens an advertisement when an old Claimed/Delivered claim is at least two
minutes old and the owner posts the next `create`; instances with
`SHADNET_SEAMLESS=false` retain the original behavior. The 2026-10-19 cloud
deployment also enabled relaxed matching on the separate traditional node,
without adding BBSC game hooks there. The server logs actual delete
`consumed/retained` counts and adds
diagnostic response headers without changing Bloodborne's JSON reply. This is
a server-side recovery path, not a rebuilt BBSC client DLL: the bundled DLL
still does not retry a failed first cleanup. To inspect a suspected failure,
compare both players' IDs and the isolated shadNet logs around `delete`,
`claim`, and `expired old claim`. Rebuild only the isolated `shadnet` service
after merging this patch into an existing server deployment; do not replace
the traditional online Compose project or delete either data volume.
`deploy/seamless_lab/patches/bbsc-http-cleanup-retry.patch` documents the
matching client source fix, but it is not applied to the hash-verified official
DLL in existing Windows packages. It needs a separately built and verified
BBSC DLL before the client retry can be claimed as shipped.

Lab sessions generate `host_overrides.bbsc.json` in that directory; ordinary
`host_overrides.json` and any `host_overrides.custom.json` remain untouched.
For these early tests, the graphical launcher forces BBSC's default
shared-progress writeback and Discord rich presence off. A developer running
`scripts/run_game.py` directly can explicitly opt in through the corresponding
environment variables after taking a separate save backup.

For a two-PC **trusted LAN** test, set both `SEAMLESS_BIND_IP` and
`SEAMLESS_ADVERTISE_IP` to the server PC's LAN IPv4 before `docker compose up`;
use that IP with `:32313` in both launchers. Restrict the five lab ports to
the trusted LAN in the host firewall. For Internet testing, add a separate
authenticated tailnet bridge to this lab; do not expose the raw game/API/relay
ports publicly or reconfigure the production shadNet service.

## Isolated Headscale overlay

The lab can reuse the authenticated userspace-network mechanism used by
traditional online play without sharing its service ports, Docker volumes, or
server-node identity. On its separate tailnet IP, the seamless bridge reuses the
already permitted TCP 31313, 31315, 31316 and 18080 plus UDP 31314, forwarding
them only to the lab's loopback 32313-32444 services. Clients distinguish the
lab with `127.0.0.3`; traditional online remains on `127.0.0.2`. Game P2P still
uses fixed UDP 31316 and local bridge 31416. The server bridge authenticates the
STUN source before wrapping it in `BBST`, so shadNet records each player's real
100.64/10 endpoint instead of Docker Desktop's common gateway address. No
production Headscale ACL change is required.

For Windows + Docker Desktop, leave all five Compose bindings on loopback and
run the native bridge in a separate PowerShell window:

```powershell
docker compose -f deploy/seamless_lab/compose.yaml `
  -f deploy/seamless_lab/compose.overlay.yaml up -d --build
.\deploy\seamless_lab\Start-OverlayBridge.ps1 `
  -ControlUrl https://YOUR-HEADSCALE -PortalUrl https://YOUR-ACCOUNT-PORTAL
```

The `server-bridge` service is profile-gated, so the first command applies the
trusted-STUN and client-advertise settings but does not start the Linux bridge
container on Windows.

On first start the script reads the current Windows user's launcher registration
credential from `%LOCALAPPDATA%\BloodbornePC\launcher.ini`, decrypts it with
DPAPI, and calls the existing `/v1/device-key` endpoint. The portal accepts only
a completed online-account registration and returns the same ten-minute,
one-use key used by the launcher; neither the application token nor the key is
written to the bridge config or command line. The server identity is stored
under `BloodbornePC\seamless-server-net`, separate from the launcher client and
traditional server identities. For the repository's localhost-only Headscale
test, omit both URL parameters; two different PCs require mutually reachable
HTTPS control and portal URLs.

On a Linux host, use the optional Compose overlay instead. It has its own
`seamless-bridge-state` volume and does not join or recreate
`deploy/online_remote`:

```sh
docker compose -f deploy/seamless_lab/compose.yaml \
  -f deploy/seamless_lab/compose.overlay.yaml --profile server-bridge build bridge
read -rsp 'Seamless server pre-auth key: ' BBSC_AUTH_KEY; echo
printf '%s\n' "$BBSC_AUTH_KEY" | docker compose \
  -f deploy/seamless_lab/compose.yaml -f deploy/seamless_lab/compose.overlay.yaml \
  --profile server-bridge run --rm -T bridge \
  --config /etc/bbport/server-net.json --auth-key-stdin
unset BBSC_AUTH_KEY
docker compose -f deploy/seamless_lab/compose.yaml \
  -f deploy/seamless_lab/compose.overlay.yaml --profile server-bridge up -d
docker compose -f deploy/seamless_lab/compose.yaml \
  -f deploy/seamless_lab/compose.overlay.yaml --profile server-bridge logs bridge
```

Record the `100.64.0.0/10` address from the bridge's ready event. Build the
remote launcher with both independent server addresses:

```powershell
.\tools\setup\BuildRemoteLauncher.ps1 \
  -OverlayServerIp 100.64.TRADITIONAL.NODE \
  -SeamlessOverlayServerIp 100.64.SEAMLESS.NODE
```

Each player first connects the launcher's embedded private network. In native
LAN mode, selecting the seamless checkbox then routes `127.0.0.3:31313` and the
BBSC API/relay loopback ports to only the configured seamless node. Leaving the
checkbox off continues to use the traditional node at `127.0.0.2:31313`. Do not
set both package keys to the same server identity, and do not reuse either
bridge's state volume.

The client exposes both `127.0.0.3:31315` for the normal shadNet status route
and `127.0.0.3:32315` as a compatibility alias for BBSC's wrapped
`summon_messenger` calls. Both connect to tailnet port 31315 on the isolated
node; 32315 is not added to the Headscale ACL and is never exposed remotely.

For the bundled two-PC Windows test package, the server PC can use its existing
enrolled player identity as the configured seamless node. Start all four local
TCP lab endpoints (32313, 32315, 32443 and 32444) before connecting the embedded
private network. The launcher detects them and adds the server publishes to the
same `bbport-net` process that provides the local client proxies and UDP game
bridge. The other PC uses the identical launcher but, because those loopback
services are absent, starts only the client routes. This does not reuse or
modify the traditional service node. Restart the launcher's private-network
connection after starting Docker if it was connected earlier.

BBSC's published mod and server APIs are the compatibility target, but this
adapter has not yet been demonstrated in a live two-PC session. Keep the
traditional online mode as the stable choice until the test matrix passes.

## Region-specific status response

The pinned BBSC game API emits EU `ss.info` fields by default. A game whose
metadata selects `/bb-asia/ss.info` (observed with the CUSA03023 NP title)
requires the `...4` status fields, while `/bb-us/ss.info` requires `...1`.
Otherwise the request returns HTTP 200 but Bloodborne can still report
"fail to get server status" before it calls `/basic_utils/login`.
`lab_region_info.py` adapts only those two response bodies in the isolated
game-api container; EU remains byte-for-byte unchanged. The region-specific
tag sets were checked against a known working community response. This is a
server-only change; the v1.7.14 launcher does not need reinstalling.

## Local verification on 2026-10-16

- The isolated shadNet `/status` and BBSC `/bb-eu/ss.info` returned HTTP 200.
  Decoded `ss.info` advertised the lab summon broker at port 32315; the
  broker route rejected an empty request with HTTP 400, rather than 404.
- A disposable test account authenticated to the isolated shadNet instance.
  The upstream BBSC DLL logged a verified game image, 52 written patch sites,
  159/159 installed hooks, a ready P2P channel, and a connection to the lab
  relay at port 32444. This verifies initialization, **not** successful co-op.
- The first game runs exited before play: the unmodified control crashed in
  `GXRenderThread` with its initial high-memory test defaults. After configuring
  the isolated test profile for 720p startup rendering (`live_resolution=0`),
  unmodified bbport stayed running through title initialization and exited
  cleanly when its window was closed. The official `bbsc-launch.exe --no-mod`
  wrapper did likewise. A DLL-enabled launch initially returned code 1 because
  the GUI-subsystem injector was started without explicit standard handles.
  `scripts/run_game.py` now supplies inherited stdout/stderr pipes and forwards
  the game log. With this fix, the DLL-enabled game remained running for over
  a minute, authenticated to the isolated shadNet, sent STUN pings, and exited
  cleanly on window close. **This verifies a single-client startup only.**
  No bell pairing, death, travel, boss clear or reconnect has been verified with
  two PCs; seamless mode is not yet a working player release.
- The game API image built locally. A fresh shadNet image build was blocked by
  the local Docker proxy failing to fetch Ubuntu packages, so the lab reused
  a previously built shadNet image from this machine. This does not establish
  clean-build reproducibility.
