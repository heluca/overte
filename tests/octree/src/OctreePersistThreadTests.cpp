//
//  OctreePersistThreadTests.cpp
//  tests/octree/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "OctreePersistThreadTests.h"

#include <QTemporaryDir>

#include <AccountManager.h>
#include <AddressManager.h>
#include <DependencyManager.h>
#include <DomainAccountManager.h>
#include <EntityTree.h>
#include <Gzip.h>
#include <NodeList.h>
#include <OctreePersistThread.h>
#include <ReceivedMessage.h>

QTEST_MAIN(OctreePersistThreadTests)

class TestPersistThread : public OctreePersistThread {
public:
    using OctreePersistThread::OctreePersistThread;
    using OctreePersistThread::handleOctreeDataFileReply;
};

static SockAddr domainServer() {
    return SockAddr(SocketType::UDP, QHostAddress::LocalHost, 50102);
}

// Another process on the domain server's host, a visitor elsewhere, and a WebRTC peer on the same address and port
static QList<SockAddr> forgedSenders() {
    return { SockAddr(SocketType::UDP, QHostAddress::LocalHost, 50103),
        SockAddr(SocketType::UDP, QHostAddress("192.0.2.1"), 50102),
        SockAddr(SocketType::WebRTC, QHostAddress::LocalHost, 50102) };
}

static QByteArray entityData() {
    QByteArray json = QString("{ \"DataVersion\": 1, \"Entities\": [], \"Id\": \"%1\", \"Version\": 1 }")
        .arg(QUuid::createUuid().toString()).toUtf8();
    QByteArray gzipped;
    gzip(json, gzipped);
    return gzipped;
}

static QSharedPointer<ReceivedMessage> dataFileReply(const QByteArray& newData, const SockAddr& sender) {
    QByteArray payload;
    payload.append(static_cast<char>(true));
    payload.append(newData);
    return QSharedPointer<ReceivedMessage>::create(payload, PacketType::OctreeDataFileReply,
        versionForPacketType(PacketType::OctreeDataFileReply), sender);
}

void OctreePersistThreadTests::initTestCase() {
    DependencyManager::registerInheritance<LimitedNodeList, NodeList>();
    DependencyManager::set<AccountManager>();
    DependencyManager::set<DomainAccountManager>();
    DependencyManager::set<AddressManager>();
    DependencyManager::set<NodeList>(NodeType::EntityServer, 0);
    DependencyManager::get<NodeList>()->getDomainHandler().setSockAddr(domainServer(), QString());
}

void OctreePersistThreadTests::testDataFileReplyFromDomainServer() {
    QTemporaryDir dataDirectory;
    QString filename = dataDirectory.filePath("models.json.gz");
    TestPersistThread persistThread(std::make_shared<EntityTree>(), filename);
    QSignalSpy loaded(&persistThread, &OctreePersistThread::loadCompleted);
    QByteArray newData = entityData();

    persistThread.handleOctreeDataFileReply(dataFileReply(newData, domainServer()));

    QVERIFY(persistThread.isInitialLoadComplete());
    QCOMPARE(loaded.count(), 1);
    QCOMPARE(persistThread.getPersistFileContents(), newData);
}

// Issue #192: OctreeDataFileReply is unsourced, so anyone who can reach the entity server's socket while it starts
// could replace its persisted entities.
void OctreePersistThreadTests::testDataFileReplyFromOtherSocket() {
    QTemporaryDir dataDirectory;
    QString filename = dataDirectory.filePath("models.json.gz");
    TestPersistThread persistThread(std::make_shared<EntityTree>(), filename);
    QSignalSpy loaded(&persistThread, &OctreePersistThread::loadCompleted);

    for (const SockAddr& sender : forgedSenders()) {
        persistThread.handleOctreeDataFileReply(dataFileReply(entityData(), sender));
    }

    QVERIFY(!persistThread.isInitialLoadComplete());
    QCOMPARE(loaded.count(), 0);
    QVERIFY(!QFile::exists(filename));
}
