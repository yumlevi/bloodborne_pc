package main

import (
	"encoding/json"
	"net/netip"
	"strings"
	"testing"
)

func TestTrafficSnapshotContainsOnlyCountersAndPeerAddresses(t *testing.T) {
	traffic := newGameTraffic()
	traffic.sent.Add(2)
	traffic.received.Add(1)
	traffic.peerSent.Add(1)
	traffic.peerReceived.Add(1)
	traffic.rejectedLocal.Add(3)
	traffic.sendErrors.Add(1)
	traffic.lastSentPeer.Store("100.64.0.8:31316")
	snapshot := traffic.snapshot()
	if snapshot.Sent != 2 || snapshot.Received != 1 ||
		snapshot.PeerSent != 1 || snapshot.PeerReceived != 1 || snapshot.RejectedLocal != 3 ||
		snapshot.SendErrors != 1 || snapshot.LastSentPeer != "100.64.0.8:31316" {
		t.Fatalf("unexpected traffic snapshot: %+v", snapshot)
	}
	encoded, err := json.Marshal(event{State: "traffic", Traffic: &snapshot})
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(encoded), `"sent":2`) ||
		!strings.Contains(string(encoded), `"last_sent_peer":"100.64.0.8:31316"`) {
		t.Fatalf("traffic event missing diagnostic fields: %s", encoded)
	}
}

func TestBridgeFrameRoundTrip(t *testing.T) {
	ip := netip.MustParseAddr("100.64.1.12")
	frame, err := bridgeFrame(ip, 31316, []byte{0, 1, 2, 3})
	if err != nil {
		t.Fatal(err)
	}
	peer, payload, err := parseBridgeFrame(frame)
	if err != nil {
		t.Fatal(err)
	}
	if peer.Addr() != ip || peer.Port() != 31316 || string(payload) != string([]byte{0, 1, 2, 3}) {
		t.Fatalf("unexpected round-trip: %v %v", peer, payload)
	}
}

func TestBridgeRejectsOutsideTailnet(t *testing.T) {
	frame, err := bridgeFrame(netip.MustParseAddr("192.168.31.7"), 31316, []byte{1})
	if err != nil {
		t.Fatal(err)
	}
	if _, _, err := parseBridgeFrame(frame); err == nil {
		t.Fatal("accepted non-tailnet peer")
	}
	frame[0] = 0
	if _, _, err := parseBridgeFrame(frame); err == nil {
		t.Fatal("accepted bad magic")
	}
}

func TestStunSourceFrame(t *testing.T) {
	ip := netip.MustParseAddr("100.64.1.12")
	frame, err := stunSourceFrame(ip, 31316, []byte{0, 1, 2})
	if err != nil {
		t.Fatal(err)
	}
	want := []byte{'B', 'B', 'S', 'T', 100, 64, 1, 12, 0x7a, 0x54, 0, 1, 2}
	if string(frame) != string(want) {
		t.Fatalf("unexpected BBST frame: %v", frame)
	}
	if _, err := stunSourceFrame(netip.MustParseAddr("192.168.1.2"), 31316, []byte{1}); err == nil {
		t.Fatal("accepted non-tailnet source")
	}
	if _, err := stunSourceFrame(ip, 1234, []byte{1}); err == nil {
		t.Fatal("accepted non-game source port")
	}
}

func TestConfigRequiresLocalBindingsAndSafeControlURL(t *testing.T) {
	c := config{ControlURL: "http://127.0.0.1:18081", StateDir: t.TempDir(), Hostname: "hunter",
		TCPProxies: []endpoint{{Listen: "127.0.0.2:31313", Remote: "100.64.0.1:31313"}}}
	if err := c.validate(); err != nil {
		t.Fatal(err)
	}
	c.ControlURL = "http://example.com"
	if err := c.validate(); err == nil {
		t.Fatal("accepted remote plaintext control URL")
	}
	c.ControlURL = "https://headscale.example.com"
	c.TCPProxies[0].Listen = "0.0.0.0:31313"
	if err := c.validate(); err == nil {
		t.Fatal("accepted public local listener")
	}
}

func TestStunSourceOnlyOnShadNetUDP(t *testing.T) {
	c := config{ControlURL: "http://127.0.0.1:18081", StateDir: t.TempDir(), Hostname: "server",
		UDPPublishes: []publish{{TailnetPort: 31314, Local: "127.0.0.1:31314", StunSource: true}}}
	if err := c.validate(); err != nil {
		t.Fatal(err)
	}
	c.UDPPublishes[0] = publish{TailnetPort: 32314, Local: "127.0.0.1:32314", StunSource: true}
	if err := c.validate(); err != nil {
		t.Fatalf("rejected isolated seamless STUN publish: %v", err)
	}
	c.UDPPublishes[0].TailnetPort = 31315
	if err := c.validate(); err == nil {
		t.Fatal("accepted BBST on non-STUN UDP publish")
	}
}
