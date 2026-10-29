//
//  DomainHandlerTests.cpp
//  tests/networking/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "DomainHandlerTests.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <AccountManager.h>
#include <AddressManager.h>
#include <DependencyManager.h>
#include <DomainAccountManager.h>
#include <NodeList.h>
#include <ReceivedMessage.h>

QTEST_MAIN(DomainHandlerTests)

static SockAddr domainServer() {
    return SockAddr(SocketType::UDP, QHostAddress::LocalHost, 40102);
}

static QJsonObject settings(const QString& worldName) {
    return QJsonObject { { "descriptors", QJsonObject { { "world_name", worldName } } } };
}

static QSharedPointer<ReceivedMessage> settingsPacket(const QJsonObject& settingsObject, const SockAddr& sender) {
    return QSharedPointer<ReceivedMessage>::create(QJsonDocument(settingsObject).toJson(), PacketType::DomainSettings,
        versionForPacketType(PacketType::DomainSettings), sender);
}

void DomainHandlerTests::initTestCase() {
    DependencyManager::registerInheritance<LimitedNodeList, NodeList>();
    DependencyManager::set<AccountManager>();
    DependencyManager::set<DomainAccountManager>();
    DependencyManager::set<AddressManager>();
    DependencyManager::set<NodeList>(NodeType::Agent, 0);
    DependencyManager::get<NodeList>()->getDomainHandler().setSockAddr(domainServer(), QString());
}

void DomainHandlerTests::testSettingsFromDomainServer() {
    DomainHandler& domainHandler = DependencyManager::get<NodeList>()->getDomainHandler();
    QSignalSpy received(&domainHandler, &DomainHandler::settingsReceived);

    domainHandler.processSettingsPacketList(settingsPacket(settings("real"), domainServer()));

    QCOMPARE(domainHandler.getSettingsObject(), settings("real"));
    QCOMPARE(received.count(), 1);
}

// Issue #64: DomainSettings is unsourced, so anyone who can reach the socket could replace the settings, the script
// credentials among them
void DomainHandlerTests::testSettingsFromOtherSocket() {
    DomainHandler& domainHandler = DependencyManager::get<NodeList>()->getDomainHandler();
    domainHandler.processSettingsPacketList(settingsPacket(settings("real"), domainServer()));
    QSignalSpy received(&domainHandler, &DomainHandler::settingsReceived);

    // Another process on the domain server's host, and a visitor elsewhere
    domainHandler.processSettingsPacketList(
        settingsPacket(settings("forged"), SockAddr(SocketType::UDP, QHostAddress::LocalHost, 40103)));
    domainHandler.processSettingsPacketList(
        settingsPacket(settings("forged"), SockAddr(SocketType::UDP, QHostAddress("192.0.2.1"), 40102)));
    domainHandler.processSettingsPacketList(
        settingsPacket(QJsonObject(), SockAddr(SocketType::WebRTC, QHostAddress::LocalHost, 40102)));

    QCOMPARE(domainHandler.getSettingsObject(), settings("real"));
    QCOMPARE(received.count(), 0);
}
