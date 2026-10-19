// SPDX-License-Identifier: GPL-2.0-or-later
// bbport game-only tailnet bridge. It does not install a virtual network adapter.
package main

import (
	"bufio"
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net"
	"net/netip"
	"net/url"
	"os"
	"os/signal"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"tailscale.com/tsnet"
)

const bridgeHeaderLen = 10

var authURL = regexp.MustCompile(`https?://[^\s"<>]+`)
var tailnetV4 = netip.MustParsePrefix("100.64.0.0/10")

type endpoint struct {
	Listen string `json:"listen"`
	Remote string `json:"remote"`
}

type publish struct {
	TailnetPort int    `json:"tailnet_port"`
	Local       string `json:"local"`
	StunSource  bool   `json:"stun_source,omitempty"`
}

type config struct {
	ControlURL    string     `json:"control_url"`
	StateDir      string     `json:"state_dir"`
	Hostname      string     `json:"hostname"`
	GameUDPPort   int        `json:"game_udp_port"`
	UDPBridgePort int        `json:"udp_bridge_port"`
	TCPProxies    []endpoint `json:"tcp_proxies"`
	UDPProxies    []endpoint `json:"udp_proxies"`
	TCPPublishes  []publish  `json:"tcp_publishes"`
	UDPPublishes  []publish  `json:"udp_publishes"`
}

type event struct {
	Type          string           `json:"type"`
	State         string           `json:"state"`
	IP            string           `json:"ip,omitempty"`
	UDPBridgePort int              `json:"udp_bridge_port,omitempty"`
	AuthURL       string           `json:"auth_url,omitempty"`
	Error         string           `json:"error,omitempty"`
	Traffic       *trafficSnapshot `json:"traffic,omitempty"`
}

// Traffic counters contain only packet counts and tailnet peer addresses.
// Never include packet contents, credentials, or the tsnet state directory.
type trafficSnapshot struct {
	Sent             uint64 `json:"sent"`
	Received         uint64 `json:"received"`
	PeerSent         uint64 `json:"peer_sent"`
	PeerReceived     uint64 `json:"peer_received"`
	RejectedLocal    uint64 `json:"rejected_local"`
	RejectedTailnet  uint64 `json:"rejected_tailnet"`
	SendErrors       uint64 `json:"send_errors"`
	DeliveryErrors   uint64 `json:"delivery_errors"`
	LastSentPeer     string `json:"last_sent_peer,omitempty"`
	LastReceivedPeer string `json:"last_received_peer,omitempty"`
}

type gameTraffic struct {
	sent            atomic.Uint64
	received        atomic.Uint64
	peerSent        atomic.Uint64
	peerReceived    atomic.Uint64
	rejectedLocal   atomic.Uint64
	rejectedTailnet atomic.Uint64
	sendErrors      atomic.Uint64
	deliveryErrors  atomic.Uint64
	lastSentPeer    atomic.Value
	lastRecvPeer    atomic.Value
}

func newGameTraffic() *gameTraffic {
	t := &gameTraffic{}
	t.lastSentPeer.Store("")
	t.lastRecvPeer.Store("")
	return t
}

func (t *gameTraffic) snapshot() trafficSnapshot {
	return trafficSnapshot{
		Sent: t.sent.Load(), Received: t.received.Load(),
		PeerSent: t.peerSent.Load(), PeerReceived: t.peerReceived.Load(),
		RejectedLocal: t.rejectedLocal.Load(), RejectedTailnet: t.rejectedTailnet.Load(),
		SendErrors: t.sendErrors.Load(), DeliveryErrors: t.deliveryErrors.Load(),
		LastSentPeer:     t.lastSentPeer.Load().(string),
		LastReceivedPeer: t.lastRecvPeer.Load().(string),
	}
}

var outputMu sync.Mutex

func emit(e event) {
	e.Type = "status"
	outputMu.Lock()
	defer outputMu.Unlock()
	_ = json.NewEncoder(os.Stdout).Encode(e)
}

func readConfig(path string) (config, error) {
	var c config
	f, err := os.Open(path)
	if err != nil {
		return c, err
	}
	defer f.Close()
	d := json.NewDecoder(io.LimitReader(f, 65536))
	d.DisallowUnknownFields()
	if err := d.Decode(&c); err != nil {
		return c, err
	}
	var extra any
	if err := d.Decode(&extra); err != io.EOF {
		return c, errors.New("trailing JSON content")
	}
	return c, c.validate()
}

func validPort(p int) bool { return p > 0 && p <= 65535 }

func loopbackAddress(value string) error {
	host, port, err := net.SplitHostPort(value)
	if err != nil {
		return err
	}
	ip := net.ParseIP(host)
	if ip == nil || !ip.IsLoopback() {
		return fmt.Errorf("local listener must use numeric loopback IP: %s", value)
	}
	p, err := net.LookupPort("tcp", port)
	if err != nil || !validPort(p) {
		return fmt.Errorf("invalid local port: %s", value)
	}
	return nil
}

func tailnetAddress(value string) error {
	host, port, err := net.SplitHostPort(value)
	if err != nil {
		return err
	}
	ip, err := netip.ParseAddr(host)
	if err != nil || !tailnetV4.Contains(ip) {
		return fmt.Errorf("remote must be a Headscale 100.64.0.0/10 IPv4: %s", value)
	}
	p, err := net.LookupPort("tcp", port)
	if err != nil || !validPort(p) {
		return fmt.Errorf("invalid remote port: %s", value)
	}
	return nil
}

func (c config) validate() error {
	u, err := neturl(c.ControlURL)
	if err != nil {
		return err
	}
	if u == "" || !filepath.IsAbs(c.StateDir) || c.Hostname == "" {
		return errors.New("control_url, absolute state_dir and hostname are required")
	}
	if c.GameUDPPort != 0 && !validPort(c.GameUDPPort) {
		return errors.New("invalid game_udp_port")
	}
	if c.UDPBridgePort != 0 && (!validPort(c.UDPBridgePort) || c.GameUDPPort == 0) {
		return errors.New("udp_bridge_port requires game_udp_port")
	}
	for _, e := range append(append([]endpoint{}, c.TCPProxies...), c.UDPProxies...) {
		if err := loopbackAddress(e.Listen); err != nil {
			return err
		}
		if err := tailnetAddress(e.Remote); err != nil {
			return err
		}
	}
	for _, e := range append(append([]publish{}, c.TCPPublishes...), c.UDPPublishes...) {
		if !validPort(e.TailnetPort) {
			return errors.New("invalid tailnet publish port")
		}
		if err := loopbackAddress(e.Local); err != nil {
			return err
		}
	}
	for _, e := range c.TCPPublishes {
		if e.StunSource {
			return errors.New("stun_source requires UDP publish")
		}
	}
	for _, e := range c.UDPPublishes {
		if e.StunSource && e.TailnetPort != 31314 && e.TailnetPort != 32314 {
			return errors.New("stun_source is only valid for shadNet UDP 31314 or 32314")
		}
	}
	return nil
}

// Avoid accepting credentials, paths or plaintext non-local control endpoints.
func neturl(raw string) (string, error) {
	u, err := url.Parse(raw)
	if err != nil || u == nil || u.Hostname() == "" || u.User != nil ||
		u.RawQuery != "" || u.Fragment != "" || (u.Path != "" && u.Path != "/") {
		return "", errors.New("invalid control_url")
	}
	if u.Scheme == "https" {
		return u.String(), nil
	}
	if u.Scheme == "http" {
		ip := net.ParseIP(u.Hostname())
		if ip != nil && ip.IsLoopback() {
			return u.String(), nil
		}
	}
	return "", errors.New("control_url must use HTTPS or loopback HTTP")
}

func bridgeFrame(ip netip.Addr, port uint16, payload []byte) ([]byte, error) {
	if !ip.Is4() || port == 0 || len(payload) > 65507-bridgeHeaderLen {
		return nil, errors.New("invalid BBTS frame")
	}
	out := make([]byte, bridgeHeaderLen+len(payload))
	copy(out[:4], "BBTS")
	a := ip.As4()
	copy(out[4:8], a[:])
	binary.BigEndian.PutUint16(out[8:10], port)
	copy(out[bridgeHeaderLen:], payload)
	return out, nil
}

// BBST is added only by the trusted server-side tailnet bridge. The game
// packet cannot claim its own source address across Docker's UDP NAT.
func stunSourceFrame(ip netip.Addr, port uint16, payload []byte) ([]byte, error) {
	if !tailnetV4.Contains(ip) || port != 31316 || len(payload) > 65507-bridgeHeaderLen {
		return nil, errors.New("invalid BBST source")
	}
	out := make([]byte, bridgeHeaderLen+len(payload))
	copy(out[:4], "BBST")
	a := ip.As4()
	copy(out[4:8], a[:])
	binary.BigEndian.PutUint16(out[8:10], port)
	copy(out[bridgeHeaderLen:], payload)
	return out, nil
}

func parseBridgeFrame(frame []byte) (netip.AddrPort, []byte, error) {
	if len(frame) < bridgeHeaderLen || string(frame[:4]) != "BBTS" {
		return netip.AddrPort{}, nil, errors.New("invalid BBTS header")
	}
	var ip [4]byte
	copy(ip[:], frame[4:8])
	port := binary.BigEndian.Uint16(frame[8:10])
	if port == 0 || !tailnetV4.Contains(netip.AddrFrom4(ip)) {
		return netip.AddrPort{}, nil, errors.New("BBTS target is not a tailnet peer")
	}
	return netip.AddrPortFrom(netip.AddrFrom4(ip), port), frame[10:], nil
}

func main() {
	path := flag.String("config", "", "path to JSON config")
	authKeyStdin := flag.Bool("auth-key-stdin", false, "read one-time auth key from stdin")
	flag.Parse()
	if *path == "" {
		emit(event{State: "error", Error: "--config is required"})
		os.Exit(2)
	}
	c, err := readConfig(*path)
	if err != nil {
		emit(event{State: "error", Error: err.Error()})
		os.Exit(2)
	}
	var authKey string
	if *authKeyStdin {
		reader := bufio.NewReaderSize(io.LimitReader(os.Stdin, 4096), 4096)
		line, readErr := reader.ReadString('\n')
		if readErr != nil && readErr != io.EOF {
			emit(event{State: "error", Error: "cannot read device auth key"})
			os.Exit(2)
		}
		authKey = strings.TrimSpace(line)
		if !strings.HasPrefix(authKey, "hskey-auth-") || len(authKey) > 512 {
			emit(event{State: "error", Error: "invalid device auth key"})
			os.Exit(2)
		}
	}
	// Headscale disables logtail after connection; opt out before enrollment too.
	_ = os.Setenv("TS_NO_LOGS_NO_SUPPORT", "true")
	if err := os.MkdirAll(c.StateDir, 0700); err != nil {
		emit(event{State: "error", Error: err.Error()})
		os.Exit(1)
	}
	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer cancel()
	srv := &tsnet.Server{Dir: c.StateDir, Hostname: c.Hostname, ControlURL: c.ControlURL, AuthKey: authKey}
	srv.UserLogf = func(format string, args ...any) {
		message := fmt.Sprintf(format, args...)
		if u := authURL.FindString(message); u != "" {
			emit(event{State: "auth", AuthURL: strings.TrimRight(u, ".,;")})
		}
	}
	defer srv.Close()
	emit(event{State: "connecting"})
	if _, err := srv.Up(ctx); err != nil {
		emit(event{State: "error", Error: err.Error()})
		os.Exit(1)
	}
	ip4, _ := srv.TailscaleIPs()
	if !ip4.Is4() {
		emit(event{State: "error", Error: "no tailnet IPv4 assigned"})
		os.Exit(1)
	}
	closers, bridgePort, err := startBridges(ctx, srv, c, ip4)
	if err != nil {
		emit(event{State: "error", Error: err.Error()})
		os.Exit(1)
	}
	defer func() {
		for _, closer := range closers {
			_ = closer.Close()
		}
	}()
	emit(event{State: "ready", IP: ip4.String(), UDPBridgePort: bridgePort})
	<-ctx.Done()
	emit(event{State: "stopped"})
}

func startBridges(ctx context.Context, srv *tsnet.Server, c config, ip netip.Addr) ([]io.Closer, int, error) {
	var closers []io.Closer
	stop := func(err error) ([]io.Closer, int, error) {
		for _, closer := range closers {
			_ = closer.Close()
		}
		return nil, 0, err
	}
	for _, p := range c.TCPProxies {
		ln, err := net.Listen("tcp", p.Listen)
		if err != nil {
			return stop(err)
		}
		closers = append(closers, ln)
		go serveTCP(ctx, ln, func() (net.Conn, error) { return srv.Dial(ctx, "tcp", p.Remote) })
	}
	for _, p := range c.TCPPublishes {
		ln, err := srv.Listen("tcp", net.JoinHostPort(ip.String(), fmt.Sprint(p.TailnetPort)))
		if err != nil {
			return stop(err)
		}
		closers = append(closers, ln)
		go serveTCP(ctx, ln, func() (net.Conn, error) { return net.DialTimeout("tcp", p.Local, 5*time.Second) })
	}
	for _, p := range c.UDPProxies {
		pc, err := net.ListenPacket("udp", p.Listen)
		if err != nil {
			return stop(err)
		}
		closers = append(closers, pc)
		go serveUDPProxy(ctx, srv, pc, p.Remote)
	}
	for _, p := range c.UDPPublishes {
		pc, err := srv.ListenPacket("udp", net.JoinHostPort(ip.String(), fmt.Sprint(p.TailnetPort)))
		if err != nil {
			return stop(err)
		}
		closers = append(closers, pc)
		go serveUDPPublish(ctx, pc, p.Local, p.StunSource)
	}
	bridgePort := 0
	if c.GameUDPPort != 0 && c.UDPBridgePort != 0 {
		local, err := net.ListenPacket("udp", net.JoinHostPort("127.0.0.1", fmt.Sprint(c.UDPBridgePort)))
		if err != nil {
			return stop(err)
		}
		remote, err := srv.ListenPacket("udp", net.JoinHostPort(ip.String(), fmt.Sprint(c.GameUDPPort)))
		if err != nil {
			_ = local.Close()
			return stop(err)
		}
		closers = append(closers, local, remote)
		traffic := newGameTraffic()
		go serveGameOutbound(ctx, local, remote, c.GameUDPPort, traffic)
		go serveGameInbound(ctx, local, remote, c.GameUDPPort, traffic)
		go reportGameTraffic(ctx, traffic)
		bridgePort = c.UDPBridgePort
	}
	return closers, bridgePort, nil
}

func serveTCP(ctx context.Context, ln net.Listener, dial func() (net.Conn, error)) {
	for {
		client, err := ln.Accept()
		if err != nil {
			return
		}
		go func() {
			defer client.Close()
			remote, err := dial()
			if err != nil {
				return
			}
			defer remote.Close()
			go func() { _, _ = io.Copy(remote, client); _ = remote.Close() }()
			_, _ = io.Copy(client, remote)
		}()
	}
}

func reportGameTraffic(ctx context.Context, traffic *gameTraffic) {
	ticker := time.NewTicker(15 * time.Second)
	defer ticker.Stop()
	previous := traffic.snapshot()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			current := traffic.snapshot()
			if current != previous {
				emit(event{State: "traffic", Traffic: &current})
				previous = current
			}
		}
	}
}

func serveGameOutbound(ctx context.Context, local, remote net.PacketConn, gamePort int, traffic *gameTraffic) {
	buf := make([]byte, 65535)
	for {
		n, source, err := local.ReadFrom(buf)
		if err != nil {
			return
		}
		src, err := netip.ParseAddrPort(source.String())
		if err != nil || !src.Addr().IsLoopback() || int(src.Port()) != gamePort {
			traffic.rejectedLocal.Add(1)
			continue
		}
		peer, payload, err := parseBridgeFrame(buf[:n])
		if err != nil {
			traffic.rejectedLocal.Add(1)
			continue
		}
		if _, err := remote.WriteTo(payload, net.UDPAddrFromAddrPort(peer)); err != nil {
			traffic.sendErrors.Add(1)
			continue
		}
		traffic.sent.Add(1)
		if peer.Port() == uint16(gamePort) {
			traffic.lastSentPeer.Store(peer.String())
			traffic.peerSent.Add(1)
		}
	}
}

func serveGameInbound(ctx context.Context, local, remote net.PacketConn, gamePort int, traffic *gameTraffic) {
	buf := make([]byte, 65535)
	game := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: gamePort}
	for {
		n, source, err := remote.ReadFrom(buf)
		if err != nil {
			return
		}
		peer, err := netip.ParseAddrPort(source.String())
		if err != nil || !tailnetV4.Contains(peer.Addr()) {
			traffic.rejectedTailnet.Add(1)
			continue
		}
		frame, err := bridgeFrame(peer.Addr(), peer.Port(), buf[:n])
		if err != nil {
			traffic.rejectedTailnet.Add(1)
			continue
		}
		if _, err := local.WriteTo(frame, game); err != nil {
			traffic.deliveryErrors.Add(1)
			continue
		}
		traffic.received.Add(1)
		if peer.Port() == uint16(gamePort) {
			traffic.lastRecvPeer.Store(peer.String())
			traffic.peerReceived.Add(1)
		}
	}
}

func serveUDPProxy(ctx context.Context, srv *tsnet.Server, local net.PacketConn, target string) {
	buf := make([]byte, 65535)
	var mu sync.Mutex
	var client net.Addr
	remote, err := srv.Dial(ctx, "udp", target)
	if err != nil {
		return
	}
	defer remote.Close()
	go func() {
		back := make([]byte, 65535)
		for {
			n, err := remote.Read(back)
			if err != nil {
				return
			}
			mu.Lock()
			dst := client
			mu.Unlock()
			if dst != nil {
				_, _ = local.WriteTo(back[:n], dst)
			}
		}
	}()
	for {
		n, src, err := local.ReadFrom(buf)
		if err != nil {
			return
		}
		ip, parseErr := netip.ParseAddrPort(src.String())
		if parseErr != nil || !ip.Addr().IsLoopback() {
			continue
		}
		mu.Lock()
		client = src
		mu.Unlock()
		_, _ = remote.Write(buf[:n])
	}
}

func serveUDPPublish(ctx context.Context, remote net.PacketConn, localTarget string, stunSource bool) {
	buf := make([]byte, 65535)
	var mu sync.Mutex
	sessions := map[string]net.Conn{}
	defer func() {
		mu.Lock()
		defer mu.Unlock()
		for _, c := range sessions {
			_ = c.Close()
		}
	}()
	for {
		n, src, err := remote.ReadFrom(buf)
		if err != nil {
			return
		}
		peer, err := netip.ParseAddrPort(src.String())
		if err != nil || !tailnetV4.Contains(peer.Addr()) {
			continue
		}
		if stunSource && peer.Port() != 31316 {
			continue
		}
		key := peer.String()
		mu.Lock()
		conn := sessions[key]
		if conn == nil {
			conn, err = net.Dial("udp", localTarget)
			if err == nil {
				sessions[key] = conn
				go func(c net.Conn, destination net.Addr) {
					back := make([]byte, 65535)
					for {
						count, e := c.Read(back)
						if e != nil {
							return
						}
						_, _ = remote.WriteTo(back[:count], destination)
					}
				}(conn, src)
			}
		}
		mu.Unlock()
		if err == nil && conn != nil {
			payload := buf[:n]
			if stunSource {
				payload, err = stunSourceFrame(peer.Addr(), peer.Port(), payload)
				if err != nil {
					continue
				}
			}
			_, _ = conn.Write(payload)
		}
	}
}
