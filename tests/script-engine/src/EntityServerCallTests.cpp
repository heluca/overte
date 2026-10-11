//
//  EntityServerCallTests.cpp
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "EntityServerCallTests.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QSemaphore>
#include <QtCore/QThread>
#include <QtNetwork/QUdpSocket>

#include "AccountManager.h"
#include "AddressManager.h"
#include "DomainAccountManager.h"
#include "EntityScriptClient.h"
#include "EntityScriptingInterface.h"
#include "NodeList.h"
#include "ScriptModuleTestRunner.h"

QTEST_MAIN(EntityServerCallTests)

// Calls each of the four functions, printing "<name> returned <value>" or "<name> threw <message>", and the callbacks.
// It stops once the body has run and every call that returned has called back, during the call or later.
static const QString CALLS =
    "var ID = '{0f6c1fa8-55a3-4a63-9b7b-2f8f9b0e4c11}';\n"
    "var outstanding = 0;\n"
    "var bodyDone = false;\n"
    "function stopWhenDone() { if (bodyDone && outstanding === 0) { Script.stop(true); } }\n"
    "function calledBack() { outstanding--; stopWhenDone(); }\n"
    "function attempt(name, f) {\n"
    "    try { print(name + ' returned ' + f()); } catch (e) { print(name + ' threw ' + e.message); }\n"
    "}\n"
    "attempt('call', function() { return Entities.callEntityServerMethod(ID, 'report', ['hello']); });\n"
    "attempt('reload', function() { return Entities.reloadServerScripts(ID); });\n"
    "attempt('status', function() {\n"
    "    var made = Entities.getServerScriptStatus(ID, function(success, isRunning, status, errorInfo) {\n"
    "        print('status callback ' + success + ' ' + isRunning + ' ' + status + ' [' + errorInfo + ']');\n"
    "        calledBack();\n"
    "    });\n"
    "    outstanding++;\n"
    "    return made;\n"
    "});\n"
    "attempt('metadata', function() {\n"
    "    var made = Entities.queryPropertyMetadata(ID, 'serverScripts', function(err, result) {\n"
    "        print('metadata callback ' + err.success + ' ' + err.status + ' ' + err.message);\n"
    "        calledBack();\n"
    "    });\n"
    "    outstanding++;\n"
    "    return made;\n"
    "});\n"
    "bodyDone = true;\n"
    "stopWhenDone();\n";

// A networkless test script has no Entities, so add it, and the other globals given
static ModuleTestRun runWithEntities(const QString& source, const QList<QPair<QString, QObject*>>& globals = {}) {
    QTemporaryDir dir;
    ModuleTestOptions options;
    options.beforeRun = [globals](ScriptManager* manager) {
        auto entities = DependencyManager::get<EntityScriptingInterface>();
        manager->engine()->globalObject().setProperty("Entities", manager->engine()->newQObject(entities.data()));
        for (const auto& global : globals) {
            manager->engine()->globalObject().setProperty(global.first, manager->engine()->newQObject(global.second));
        }
    };
    return runScript(dir, { { "main.js", source } }, options);
}

void ClientTeardown::destroyClient() {
    DependencyManager::destroy<EntityScriptClient>();
    destroyed = true;
    _gate.release();
}

static QStringList sorted(QStringList list) {
    list.sort();
    return list;
}

void EntityServerCallTests::initTestCase() {
    DependencyManager::registerInheritance<LimitedNodeList, NodeList>();
    DependencyManager::set<AccountManager>(true);
    DependencyManager::set<DomainAccountManager>();
    DependencyManager::set<AddressManager>();
    // Not connected to a domain, so there is no entity script server node
    DependencyManager::set<NodeList>(NodeType::Agent, 0);
    initModuleTestDependencies();
    DependencyManager::set<EntityScriptingInterface>(false);
}

void EntityServerCallTests::testWithoutClientEachCallThrows() {
    // As in an entity script server, which sets up Entities but no EntityScriptClient
    QVERIFY(!DependencyManager::get<EntityScriptClient>());
    auto run = runWithEntities(CALLS);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({
        "call threw Entities.callEntityServerMethod is not available in this script context",
        "reload threw Entities.reloadServerScripts is not available in this script context",
        "status threw Entities.getServerScriptStatus is not available in this script context",
        "metadata threw Entities.queryPropertyMetadata(\"serverScripts\") is not available in this script context",
    }));
}

void EntityServerCallTests::testWithClientAndNoServer() {
    // As in an assignment-client Agent: the client lives on another thread than the script
    QThread clientThread;
    clientThread.start();
    auto client = DependencyManager::set<EntityScriptClient>();
    client->moveToThread(&clientThread);

    auto run = runWithEntities(CALLS);

    QMetaObject::invokeMethod(client.data(), [raw = client.data()] { raw->moveToThread(QCoreApplication::instance()->thread()); },
                              Qt::BlockingQueuedConnection);
    client.clear();
    DependencyManager::destroy<EntityScriptClient>();
    clientThread.quit();
    clientThread.wait();

    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    // Without an entity script server nothing is sent, and each status request fails at once
    QCOMPARE(sorted(run.printed), sorted({
        "call returned undefined",
        "reload returned false",
        "status returned true",
        "status callback false false error_loading_script []",
        "metadata returned true",
        "metadata callback false error_loading_script entity server script details not found",
    }));
}

void EntityServerCallTests::testClientDestroyedBeforeQueuedStart() {
    // As in an Agent whose script stops while its main thread is busy: aboutToFinish() destroys the client before the
    // status request's queued start() runs on the client's thread
    QThread clientThread;
    clientThread.start();
    DependencyManager::set<EntityScriptClient>()->moveToThread(&clientThread);
    QSemaphore gate;
    // Holds the client's thread until the script has destroyed the client, so start() stays queued until then
    QMetaObject::invokeMethod(DependencyManager::get<EntityScriptClient>().data(), [&gate] { gate.acquire(); },
                              Qt::QueuedConnection);
    ClientTeardown teardown(gate);

    auto run = runWithEntities(
        "Entities.getServerScriptStatus('{0f6c1fa8-55a3-4a63-9b7b-2f8f9b0e4c11}', function(success, isRunning, status, errorInfo) {\n"
        "    print('status callback ' + success + ' ' + isRunning + ' ' + status + ' [' + errorInfo + ']');\n"
        "    Script.stop(true);\n"
        "});\n"
        "Teardown.destroyClient();\n"
        "print('client destroyed');\n",
        { { "Teardown", &teardown } });

    if (!teardown.destroyed) {
        teardown.destroyClient();
    }
    clientThread.quit();
    clientThread.wait();

    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    // Without the guard in start(), the null client happens to fail the same way while there is no entity script server
    // node, so the error information is what tells the guarded path apart
    QCOMPARE(run.printed, QStringList({ "client destroyed",
                                        "status callback false false error_loading_script [entity script client unavailable]" }));
}

void EntityServerCallTests::testStatusWithoutReplyFails() {
    // An entity script server that never replies, as the real one does for an entity without serverScripts (#42)
    QUdpSocket silentServer;
    QVERIFY(silentServer.bind(QHostAddress(QHostAddress::LocalHost), 0));
    SockAddr address(SocketType::UDP, QHostAddress::LocalHost, silentServer.localPort());
    auto nodeList = DependencyManager::get<NodeList>();
    auto server = nodeList->addOrUpdateNode(QUuid::createUuid(), NodeType::EntityScriptServer, address, address);
    server->activatePublicSocket();
    QThread clientThread;
    clientThread.start();
    auto client = DependencyManager::set<EntityScriptClient>();
    client->moveToThread(&clientThread);

    auto run = runWithEntities(
        "Entities.getServerScriptStatus('{0f6c1fa8-55a3-4a63-9b7b-2f8f9b0e4c11}', function(success, isRunning, status, errorInfo) {\n"
        "    print('status callback ' + success + ' ' + isRunning + ' ' + status + ' [' + errorInfo + ']');\n"
        "    Script.stop(true);\n"
        "});\n");

    QMetaObject::invokeMethod(client.data(), [raw = client.data()] { raw->moveToThread(QCoreApplication::instance()->thread()); },
                              Qt::BlockingQueuedConnection);
    client.clear();
    DependencyManager::destroy<EntityScriptClient>();
    // Deleted now, so removing the node below cannot fail a pending request into the finished script
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    clientThread.quit();
    clientThread.wait();
    nodeList->killNodeWithUUID(server->getUUID());

    // A send was attempted, so the request was pending rather than failed at once for want of a server. The socket only
    // sees connection handshakes: the request itself waits for a handshake reply that never comes.
    QVERIFY(silentServer.hasPendingDatagrams());
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "status callback false false error_loading_script [no reply from the entity script server]" }));
}
