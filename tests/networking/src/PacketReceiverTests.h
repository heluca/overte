//
//  PacketReceiverTests.h
//  tests/networking/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_PacketReceiverTests_h
#define overte_PacketReceiverTests_h

#include <QtTest/QtTest>

#include <ReceivedMessage.h>

class PacketReceiverTests : public QObject {
    Q_OBJECT
private slots:
    void testUnregisterAfterUnhandledType();
    void testRegisterAfterUnhandledType();

public slots:
    void handlePacket(QSharedPointer<ReceivedMessage> message);

private:
    int _received { 0 };
};

#endif // overte_PacketReceiverTests_h
