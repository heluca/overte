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
#include <QtCore/QThread>

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

// A networkless test script has no Entities, so add it
static ModuleTestRun runCalls() {
    QTemporaryDir dir;
    ModuleTestOptions options;
    options.beforeRun = [](ScriptManager* manager) {
        auto entities = DependencyManager::get<EntityScriptingInterface>();
        manager->engine()->globalObject().setProperty("Entities", manager->engine()->newQObject(entities.data()));
    };
    return runScript(dir, { { "main.js", CALLS } }, options);
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
    auto run = runCalls();
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

    auto run = runCalls();

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
