//
//  PacketReceiverTests.cpp
//  tests/networking/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "PacketReceiverTests.h"

#include <NLPacket.h>
#include <PacketReceiver.h>

QTEST_MAIN(PacketReceiverTests)

void PacketReceiverTests::handlePacket(QSharedPointer<ReceivedMessage> message) {
    Q_UNUSED(message);
    ++_received;
}

// A packet type nobody listens for leaves a null listener in the map; unregistering a real listener
// afterwards must step over it (issue #54: every listening Agent crashed on reset).
void PacketReceiverTests::testUnregisterAfterUnhandledType() {
    PacketReceiver receiver;
    QObject owner;
    QVERIFY(receiver.registerListener(PacketType::SelectedAudioFormat,
        PacketReceiver::makeUnsourcedListenerReference<PacketReceiverTests>(this, &PacketReceiverTests::handlePacket)));

    receiver.handleVerifiedPacket(NLPacket::create(PacketType::AudioStreamStats));
    receiver.handleVerifiedPacket(NLPacket::create(PacketType::AudioEnvironment));

    receiver.unregisterListener(&owner);
    receiver.unregisterListener(this);

    _received = 0;
    receiver.handleVerifiedPacket(NLPacket::create(PacketType::SelectedAudioFormat));
    QCoreApplication::processEvents();
    QCOMPARE(_received, 0);
}

// Registering for a type that already has the null placeholder replaces it and gets the packets.
void PacketReceiverTests::testRegisterAfterUnhandledType() {
    PacketReceiver receiver;
    receiver.handleVerifiedPacket(NLPacket::create(PacketType::AudioStreamStats));

    QVERIFY(receiver.registerListener(PacketType::AudioStreamStats,
        PacketReceiver::makeUnsourcedListenerReference<PacketReceiverTests>(this, &PacketReceiverTests::handlePacket)));

    _received = 0;
    receiver.handleVerifiedPacket(NLPacket::create(PacketType::AudioStreamStats));
    QCoreApplication::processEvents();
    QCOMPARE(_received, 1);

    receiver.unregisterListener(this);
}
