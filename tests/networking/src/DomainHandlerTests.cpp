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

// Another process on the domain server's host, a visitor elsewhere, and a WebRTC peer on the same address and port
static QList<SockAddr> forgedSenders() {
    return { SockAddr(SocketType::UDP, QHostAddress::LocalHost, 40103),
        SockAddr(SocketType::UDP, QHostAddress("192.0.2.1"), 40102),
        SockAddr(SocketType::WebRTC, QHostAddress::LocalHost, 40102) };
}

static QSharedPointer<ReceivedMessage> message(PacketType type, const QByteArray& payload, const SockAddr& sender) {
    return QSharedPointer<ReceivedMessage>::create(payload, type, versionForPacketType(type), sender);
}

static QSharedPointer<ReceivedMessage> settingsPacket(const QJsonObject& settingsObject, const SockAddr& sender) {
    return message(PacketType::DomainSettings, QJsonDocument(settingsObject).toJson(), sender);
}

template <typename T>
static void appendPrimitive(QByteArray& payload, T value) {
    payload.append(reinterpret_cast<const char*>(&value), sizeof(T));
}

static QByteArray nodePayload(const Node& node) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << node << QUuid::createUuid();
    return payload;
}

// The default permissions lack canRezAvatarEntities, so processDomainList sets the session UUID and stops before it
// connects, which would look up the domain's place name on the directory server.
static QByteArray domainListPayload(const QUuid& sessionUUID, const Node& node) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << QUuid::createUuid() << Node::LocalID(1) << sessionUUID << Node::LocalID(2) << NodePermissions() << false
        << quint64(0) << quint64(0) << quint64(0) << false;
    return payload + nodePayload(node);
}

static QByteArray connectionDeniedPayload(DomainHandler::ConnectionRefusedReason reason, const QByteArray& reasonText) {
    QByteArray payload;
    appendPrimitive(payload, static_cast<uint8_t>(reason));
    appendPrimitive(payload, static_cast<quint16>(reasonText.size()));
    payload.append(reasonText);
    appendPrimitive(payload, quint16(0));
    return payload;
}

static QByteArray usernameFromIDReplyPayload() {
    QByteArray payload = QUuid::createUuid().toRfc4122();
    QByteArray username = "forged";
    appendPrimitive(payload, static_cast<uint32_t>(username.size()));
    payload.append(username);
    payload.append(QUuid::createUuid().toRfc4122());
    appendPrimitive(payload, true);
    return payload;
}

static SockAddr mixerSocket() {
    return SockAddr(SocketType::UDP, QHostAddress::LocalHost, 40104);
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

// Issue #101: the packets below are unsourced too, and only the domain server may send them.
void DomainHandlerTests::testDomainListFromOtherSocket() {
    auto nodeList = DependencyManager::get<NodeList>();
    QSignalSpy received(nodeList.data(), &NodeList::receivedDomainServerList);
    QUuid sessionUUID = nodeList->getSessionUUID();
    size_t numNodes = nodeList->size();
    Node node(QUuid::createUuid(), NodeType::AudioMixer, mixerSocket(), mixerSocket());

    for (const SockAddr& sender : forgedSenders()) {
        nodeList->processDomainList(message(PacketType::DomainList, domainListPayload(QUuid::createUuid(), node), sender));
    }

    QCOMPARE(nodeList->getSessionUUID(), sessionUUID);
    QCOMPARE(nodeList->size(), numNodes);
    QCOMPARE(received.count(), 0);
}

void DomainHandlerTests::testDomainListFromDomainServer() {
    auto nodeList = DependencyManager::get<NodeList>();
    QSignalSpy received(nodeList.data(), &NodeList::receivedDomainServerList);
    QUuid sessionUUID = QUuid::createUuid();
    Node node(QUuid::createUuid(), NodeType::AudioMixer, mixerSocket(), mixerSocket());

    nodeList->processDomainList(message(PacketType::DomainList, domainListPayload(sessionUUID, node), domainServer()));

    QCOMPARE(nodeList->getSessionUUID(), sessionUUID);
    QCOMPARE(received.count(), 1);
}

void DomainHandlerTests::testAddedNodeFromOtherSocket() {
    auto nodeList = DependencyManager::get<NodeList>();
    Node node(QUuid::createUuid(), NodeType::AudioMixer, mixerSocket(), mixerSocket());
    node.setLocalID(3);

    for (const SockAddr& sender : forgedSenders()) {
        nodeList->processDomainServerAddedNode(message(PacketType::DomainServerAddedNode, nodePayload(node), sender));
    }

    QVERIFY(!nodeList->nodeWithUUID(node.getUUID()));
}

void DomainHandlerTests::testRemovedNode() {
    auto nodeList = DependencyManager::get<NodeList>();
    Node node(QUuid::createUuid(), NodeType::AudioMixer, mixerSocket(), mixerSocket());
    node.setLocalID(4);
    nodeList->processDomainServerAddedNode(message(PacketType::DomainServerAddedNode, nodePayload(node), domainServer()));
    QVERIFY(nodeList->nodeWithUUID(node.getUUID()));

    for (const SockAddr& sender : forgedSenders()) {
        nodeList->processDomainServerRemovedNode(
            message(PacketType::DomainServerRemovedNode, node.getUUID().toRfc4122(), sender));
    }
    QVERIFY(nodeList->nodeWithUUID(node.getUUID()));

    nodeList->processDomainServerRemovedNode(
        message(PacketType::DomainServerRemovedNode, node.getUUID().toRfc4122(), domainServer()));
    QVERIFY(!nodeList->nodeWithUUID(node.getUUID()));
}

void DomainHandlerTests::testConnectionToken() {
    auto nodeList = DependencyManager::get<NodeList>();
    DomainHandler& domainHandler = nodeList->getDomainHandler();
    QUuid connectionToken = domainHandler.getConnectionToken();

    for (const SockAddr& sender : forgedSenders()) {
        nodeList->processDomainServerConnectionTokenPacket(
            message(PacketType::DomainServerConnectionToken, QUuid::createUuid().toRfc4122(), sender));
    }
    QCOMPARE(domainHandler.getConnectionToken(), connectionToken);

    connectionToken = QUuid::createUuid();
    nodeList->processDomainServerConnectionTokenPacket(
        message(PacketType::DomainServerConnectionToken, connectionToken.toRfc4122(), domainServer()));
    QCOMPARE(domainHandler.getConnectionToken(), connectionToken);
}

void DomainHandlerTests::testUsernameFromIDReply() {
    auto nodeList = DependencyManager::get<NodeList>();
    QSignalSpy received(nodeList.data(), &NodeList::usernameFromIDReply);

    for (const SockAddr& sender : forgedSenders()) {
        nodeList->processUsernameFromIDReply(message(PacketType::UsernameFromIDReply, usernameFromIDReplyPayload(), sender));
    }
    QCOMPARE(received.count(), 0);

    nodeList->processUsernameFromIDReply(
        message(PacketType::UsernameFromIDReply, usernameFromIDReplyPayload(), domainServer()));
    QCOMPARE(received.count(), 1);
}

// The address-only check this replaced let another process on the domain server's host deny the connection.
void DomainHandlerTests::testConnectionDenied() {
    DomainHandler& domainHandler = DependencyManager::get<NodeList>()->getDomainHandler();
    QSignalSpy refused(&domainHandler, &DomainHandler::domainConnectionRefused);
    const auto reason = DomainHandler::ConnectionRefusedReason::TooManyUsers;

    for (const SockAddr& sender : forgedSenders()) {
        domainHandler.processDomainServerConnectionDeniedPacket(
            message(PacketType::DomainConnectionDenied, connectionDeniedPayload(reason, "forged"), sender));
    }
    QCOMPARE(refused.count(), 0);

    domainHandler.processDomainServerConnectionDeniedPacket(
        message(PacketType::DomainConnectionDenied, connectionDeniedPayload(reason, "real"), domainServer()));
    QCOMPARE(refused.count(), 1);
    QCOMPARE(domainHandler.getLastDomainConnectionError(), static_cast<int>(reason));
}
