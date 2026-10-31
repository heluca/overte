//
//  AvatarIdentityTests.cpp
//  tests/avatars/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "AvatarIdentityTests.h"

#include <AccountManager.h>
#include <AddressManager.h>
#include <AvatarData.h>
#include <AvatarHashMap.h>
#include <DependencyManager.h>
#include <DomainAccountManager.h>
#include <NodeList.h>
#include <ReceivedMessage.h>
#include <StatTracker.h>

QTEST_MAIN(AvatarIdentityTests)

namespace {

const QUuid OWN_SESSION { "{6f1c2a9e-3b4d-4e5f-8a7b-1c2d3e4f5a6b}" };
const QUuid OTHER_SESSION { "{0a1b2c3d-4e5f-4a6b-8c7d-9e0f1a2b3c4d}" };

// What sendIdentityPacket() puts on the wire after a change, without needing a mixer to send it to.
QByteArray sendIdentity(AvatarData& avatar) {
    avatar.pushIdentitySequenceNumber();
    return avatar.identityByteArray();
}

// AvatarMixer::handleAvatarIdentityPacket() then manageIdentityData(): on a new display name the mixer assigns a
// session display name and echoes the identity back twice, bumping its sequence number in between.
QList<QByteArray> mixerReceives(AvatarData& mixerAvatar, const QByteArray& identity, bool& displayNameChanged) {
    QDataStream identityStream(identity);
    bool identityChanged = false;
    displayNameChanged = false;
    mixerAvatar.processAvatarIdentity(identityStream, identityChanged, displayNameChanged);
    if (!displayNameChanged) {
        return {};
    }
    mixerAvatar.setSessionDisplayName(mixerAvatar.getDisplayName());
    const QByteArray firstEcho = mixerAvatar.identityByteArray();
    mixerAvatar.pushIdentitySequenceNumber();
    return { firstEcho, mixerAvatar.identityByteArray() };
}

void deliver(const QByteArray& identity) {
    auto message = QSharedPointer<ReceivedMessage>::create(identity, PacketType::AvatarIdentity,
                                                           versionForPacketType(PacketType::AvatarIdentity), SockAddr());
    DependencyManager::get<AvatarHashMap>()->processAvatarIdentityPacket(message, SharedNodePointer());
}

QSharedPointer<AvatarData> makeOwnAvatar() {
    auto ownAvatar = QSharedPointer<AvatarData>::create();
    ownAvatar->setSessionUUID(OWN_SESSION);
    DependencyManager::get<AvatarHashMap>()->setOwnAvatar(ownAvatar);
    return ownAvatar;
}

}  // namespace

void AvatarIdentityTests::initTestCase() {
    DependencyManager::set<StatTracker>();
    DependencyManager::registerInheritance<LimitedNodeList, NodeList>();
    DependencyManager::set<AccountManager>(true);
    DependencyManager::set<DomainAccountManager>();
    DependencyManager::set<AddressManager>();
    DependencyManager::set<NodeList>(NodeType::Agent, 0)->setSessionUUID(OWN_SESSION);
    DependencyManager::set<AvatarHashMap>();
}

void AvatarIdentityTests::cleanupTestCase() {
    DependencyManager::destroy<AvatarHashMap>();
}

void AvatarIdentityTests::testRenamesReachMixerAfterSessionName() {
    auto ownAvatar = makeOwnAvatar();
    AvatarData mixerAvatar;
    mixerAvatar.setSessionUUID(OWN_SESSION);
    bool displayNameChanged = false;

    const QStringList names { "Ada (guest)", "Ada (guest) (away)", "Ada (guest)" };
    for (const QString& name : names) {
        ownAvatar->setDisplayName(name);
        const QList<QByteArray> echoes = mixerReceives(mixerAvatar, sendIdentity(*ownAvatar), displayNameChanged);
        QVERIFY2(displayNameChanged, qPrintable("mixer dropped " + name));
        QCOMPARE(mixerAvatar.getDisplayName(), name);
        QCOMPARE(echoes.size(), 2);
        for (const QByteArray& echo : echoes) {
            deliver(echo);
        }
        QCOMPARE(ownAvatar->getDisplayName(), name);
    }
}

void AvatarIdentityTests::testOwnIdentityNotListed() {
    auto ownAvatar = makeOwnAvatar();
    ownAvatar->setDisplayName("Ada");
    deliver(sendIdentity(*ownAvatar));
    QVERIFY(!DependencyManager::get<AvatarHashMap>()->getAvatarIdentifiers().contains(OWN_SESSION));
}

void AvatarIdentityTests::testOtherIdentityListed() {
    makeOwnAvatar();
    AvatarData otherAvatar;
    otherAvatar.setSessionUUID(OTHER_SESSION);
    otherAvatar.setDisplayName("Bea");
    deliver(sendIdentity(otherAvatar));

    auto listed = DependencyManager::get<AvatarHashMap>()->getAvatarBySessionID(OTHER_SESSION);
    QVERIFY(listed);
    QCOMPARE(listed->getDisplayName(), QString("Bea"));
}
