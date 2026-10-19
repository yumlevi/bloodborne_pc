# Native LAN networking

For the containerized Ubuntu shadNet server prototype and its Windows/Linux
network choices, see [deploy/shadnet/README.md](../deploy/shadnet/README.md).

bbport keeps its original disconnected network service behavior by default.
Set `BB_NETWORK_MODE=lan` to opt in to the native transport while it is under
development.

## Implemented transport foundation

- PS4-style IPv4 TCP and UDP sockets backed by the host operating system.
- Bind, connect, listen, accept, send/receive, socket options and DNS lookup.
- `sceNetEpoll*` readiness events (`WSAPoll` on Windows, `select` elsewhere).
- Online `sceNetCtl` state for the opted-in process.
- A configurable local NP identity (`BB_LAN_NPID`, default `Hunter01`) and
  signed-in state, including the state callback.
- Matching2 initialization, context start/stop callbacks and the single LAN
  world needed before room matchmaking begins.
- A small versioned LAN discovery protocol on UDP 31314: room query,
  announcement, join request, join acknowledgement and leave notification.
- Matching2 create/search/join/leave request callbacks, member joined/left room
  callbacks, and signaling-established callbacks.
- Searchable integer attributes and the first searchable binary attribute are
  carried with room announcements, so Bloodborne can identify compatible
  rooms instead of receiving an empty shell.
- `sceNpMatching2SignalingGetConnectionStatus` reports the peer's IPv4 address
  and fixed P2P UDP port after the join handshake.
- P2P UDP framing compatible with Wozzardman/shadp2p commit `6e97b404`:
  two network-order virtual ports followed by the guest payload.
- `BB_P2P_PORT=<port>` pins the host UDP port. The launcher uses 31316 for the
  native LAN preview so the address advertised during matching is the same
  one used by the guest P2P socket.
- `BB_LAN_DISCOVERY_TARGET`, `BB_LAN_DISCOVERY_PORT`, and the optional
  `BB_LAN_DISCOVERY_TARGET_PORT` can override the broadcast target, local
  discovery port, and destination port for diagnostics. Production defaults
  are `255.255.255.255` and 31314 for both ports.

`build.sh --test` performs real two-endpoint loopback tests for normal
datagrams, P2P framing, room discovery, join acknowledgement, Matching2 member
events, and signaling address lookup. A sandbox that blocks loopback
networking must run those tests outside the sandbox.

## Not implemented yet

The matching and signaling path is now present, but this remains a development
preview rather than a claim of playable co-op. The next validation is a real
two-PC Bloodborne bell test. Any additional game-observed Matching2 calls,
attribute semantics, or P2P socket behavior found there must be implemented
from the captured logs. This path does not embed or launch the full shadPS4
emulator.
