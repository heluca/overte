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

#include <memory>

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
const QUuid NAMED_SESSION { "{5d4c3b2a-1f0e-4d9c-8b7a-6f5e4d3c2b1a}" };

class TestAvatarHashMap : public AvatarHashMap {
public:
    using AvatarHashMap::processAvatarIdentityPacket;
};

std::unique_ptr<TestAvatarHashMap> avatarHashMap;

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
    avatarHashMap->processAvatarIdentityPacket(message, SharedNodePointer());
}

QSharedPointer<AvatarData> makeOwnAvatar() {
    auto ownAvatar = QSharedPointer<AvatarData>::create();
    ownAvatar->setSessionUUID(OWN_SESSION);
    avatarHashMap->setOwnAvatar(ownAvatar);
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
    avatarHashMap = std::make_unique<TestAvatarHashMap>();
}

void AvatarIdentityTests::cleanupTestCase() {
    avatarHashMap.reset();
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
    QVERIFY(!avatarHashMap->getAvatarIdentifiers().contains(OWN_SESSION));
}

void AvatarIdentityTests::testOtherIdentityListed() {
    makeOwnAvatar();
    AvatarData otherAvatar;
    otherAvatar.setSessionUUID(OTHER_SESSION);
    otherAvatar.setDisplayName("Bea");
    deliver(sendIdentity(otherAvatar));

    auto listed = avatarHashMap->getAvatarBySessionID(OTHER_SESSION);
    QVERIFY(listed);
    QCOMPARE(listed->getDisplayName(), QString("Bea"));
}

void AvatarIdentityTests::testOtherSessionDisplayNameListed() {
    makeOwnAvatar();
    // Stands in for the mixer's copy of another avatar, whose session display name it assigned.
    AvatarData otherAvatar;
    otherAvatar.setSessionUUID(NAMED_SESSION);
    otherAvatar.setDisplayName("Cy");
    otherAvatar.setSessionDisplayName("Cy_2");
    deliver(sendIdentity(otherAvatar));

    auto listed = avatarHashMap->getAvatarBySessionID(NAMED_SESSION);
    QVERIFY(listed);
    QCOMPARE(listed->getSessionDisplayName(), QString("Cy_2"));

    QSignalSpy changed(listed.get(), &AvatarData::sessionDisplayNameChanged);
    otherAvatar.setSessionDisplayName("Cy_3");
    deliver(sendIdentity(otherAvatar));
    QCOMPARE(listed->getSessionDisplayName(), QString("Cy_3"));
    QCOMPARE(changed.count(), 1);

    deliver(sendIdentity(otherAvatar));
    QCOMPARE(changed.count(), 1);
}

void AvatarIdentityTests::testMixerIgnoresClientSessionDisplayName() {
    AvatarData clientAvatar;
    clientAvatar.setSessionUUID(OWN_SESSION);
    clientAvatar.setDisplayName("Dee");
    clientAvatar.setSessionDisplayName("Admin");

    AvatarData mixerAvatar;
    mixerAvatar.setSessionUUID(OWN_SESSION);
    const QByteArray identity = sendIdentity(clientAvatar);
    QDataStream identityStream(identity);
    bool identityChanged = false;
    bool displayNameChanged = false;
    mixerAvatar.processAvatarIdentity(identityStream, identityChanged, displayNameChanged);

    QCOMPARE(mixerAvatar.getDisplayName(), QString("Dee"));
    QVERIFY(mixerAvatar.getSessionDisplayName().isEmpty());
}
