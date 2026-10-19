// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QHostAddress>
#include <QObject>
#include <QUdpSocket>
#include "client_session.h"

class StunServer : public QObject {
    Q_OBJECT
public:
    explicit StunServer(SharedState* shared, QObject* parent = nullptr);
    bool Start(const QHostAddress& addr, uint16_t port);

private slots:
    void OnReadyRead();

private:
    void HandleStunPing(const QByteArray& data, const QHostAddress& sender, uint16_t senderPort,
                        const QHostAddress& advertisedSender, uint16_t advertisedPort);

    QUdpSocket* m_socket;
    SharedState* m_shared;
};
