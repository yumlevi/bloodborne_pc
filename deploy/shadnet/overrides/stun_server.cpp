// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <QDebug>
#include <QtGlobal>
#include <QtEndian>
#include "stream_extractor.h"
#include "stun_server.h"

// Must match the client's SIGNALING_VPORT_NBO / VPORT_HEADER_SIZE
static constexpr uint16_t SIGNALING_VPORT_NBO = 0xFFFF;
static constexpr int VPORT_HEADER_SIZE = 4;

static QByteArray FrameSignaling(const QByteArray& payload) {
    QByteArray framed;
    framed.reserve(VPORT_HEADER_SIZE + payload.size());
    framed.append(reinterpret_cast<const char*>(&SIGNALING_VPORT_NBO), 2);
    framed.append(reinterpret_cast<const char*>(&SIGNALING_VPORT_NBO), 2);
    framed.append(payload);
    return framed;
}

static QByteArray StripVportHeader(const QByteArray& data) {
    if (data.size() < VPORT_HEADER_SIZE)
        return {};
    uint16_t src_vp, dst_vp;
    memcpy(&src_vp, data.constData(), 2);
    memcpy(&dst_vp, data.constData() + 2, 2);
    if (src_vp != SIGNALING_VPORT_NBO || dst_vp != SIGNALING_VPORT_NBO)
        return {}; // not a signaling packet
    return data.mid(VPORT_HEADER_SIZE);
}

StunServer::StunServer(SharedState* shared, QObject* parent)
    : QObject(parent), m_socket(new QUdpSocket(this)), m_shared(shared) {
    connect(m_socket, &QUdpSocket::readyRead, this, &StunServer::OnReadyRead);
}

bool StunServer::Start(const QHostAddress& addr, uint16_t port) {
    if (!m_socket->bind(addr, port)) {
        qCritical() << "STUN UDP bind failed on" << addr.toString() << ":" << port
                    << m_socket->errorString();
        return false;
    }
    qInfo().nospace().noquote() << "STUN UDP listener on: " << addr.toString() << ":" << port;
    return true;
}

void StunServer::OnReadyRead() {
    while (m_socket->hasPendingDatagrams()) {
        QByteArray raw;
        raw.resize(static_cast<int>(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        uint16_t senderPort = 0;
        m_socket->readDatagram(raw.data(), raw.size(), &sender, &senderPort);

        if (raw.isEmpty())
            continue;

        QHostAddress advertisedSender = sender;
        uint16_t advertisedPort = senderPort;
        // Only the server's loopback-mapped Docker bridge may supply BBST.
        // The embedded tailnet bridge derives these bytes from the authenticated
        // packet source; client-supplied STUN local_ip is never trusted.
        QHostAddress bridgeSender = sender;
        if (sender.toString().startsWith("::ffff:"))
            bridgeSender = QHostAddress(sender.toString().mid(7));
        if (qEnvironmentVariableIsSet("BBPORT_TRUST_TAILNET_STUN") &&
            (bridgeSender.isLoopback() ||
             bridgeSender.isInSubnet(QHostAddress("172.16.0.0"), 12))) {
            if (raw.size() < 10 || raw.left(4) != "BBST")
                continue;
            const auto octet = [&raw](int pos) {
                return static_cast<uint8_t>(raw.at(pos));
            };
            if (octet(4) != 100 || (octet(5) & 0xc0) != 0x40)
                continue;
            advertisedPort = (static_cast<uint16_t>(octet(8)) << 8) | octet(9);
            if (advertisedPort != 31316)
                continue;
            const uint32_t ip = (static_cast<uint32_t>(octet(4)) << 24) |
                                (static_cast<uint32_t>(octet(5)) << 16) |
                                (static_cast<uint32_t>(octet(6)) << 8) | octet(7);
            advertisedSender = QHostAddress(ip);
            raw.remove(0, 10);
        }

        // Strip signaling vport header
        QByteArray data = StripVportHeader(raw);
        if (data.isEmpty())
            continue;

        uint8_t cmd = static_cast<uint8_t>(data[0]);
        switch (cmd) {
        case 0x01:
            HandleStunPing(data, sender, senderPort, advertisedSender, advertisedPort);
            break;
        default:
            break;
        }
    }
}

void StunServer::HandleStunPing(const QByteArray& data, const QHostAddress& sender,
                                uint16_t senderPort, const QHostAddress& advertisedSender,
                                uint16_t advertisedPort) {
    // cmd(1) + online_id(16) + local_ip(4) = 21 bytes minimum
    if (data.size() < 21)
        return;

    // Extract null-padded online_id from bytes 1-16
    QByteArray rawId = data.mid(1, 16);
    int nullPos = rawId.indexOf('\0');
    QString npid = QString::fromUtf8(rawId.left(nullPos >= 0 ? nullPos : 16)).trimmed();
    if (npid.isEmpty())
        return;

    QString extIpStr = advertisedSender.toString();
    // Strip IPv6-mapped prefix if present (e.g. "::ffff:192.168.1.1" -> "192.168.1.1")
    if (extIpStr.startsWith("::ffff:"))
        extIpStr = extIpStr.mid(7);

    qInfo() << "STUN ping: npid=" << npid << "ext=" << extIpStr << ":" << advertisedPort;

    // Record external endpoint
    {
        QWriteLocker lk(&m_shared->matching.udpLock);
        m_shared->matching.udpExt[npid] = {extIpStr, advertisedPort};
    }

    // Reply: ext_ip(4 bytes network order) + ext_port(2 bytes network order)
    QHostAddress extAddr(extIpStr);
    uint32_t ipNet = qToBigEndian(static_cast<uint32_t>(extAddr.toIPv4Address()));
    uint16_t portNet = qToBigEndian(advertisedPort);

    QByteArray response;
    response.append(reinterpret_cast<const char*>(&ipNet), 4);
    response.append(reinterpret_cast<const char*>(&portNet), 2);
    m_socket->writeDatagram(FrameSignaling(response), sender, senderPort);
}
