//
//  ScriptTeardownTests.cpp
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptTeardownTests.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QThread>

#include "DependencyManager.h"
#include "ScriptCache.h"
#include "ScriptEngine.h"
#include "ScriptEngines.h"
#include "ScriptManager.h"
#include "StatTracker.h"
#include "WebSocketClass.h"
#include "XMLHttpRequestClass.h"

QTEST_MAIN(ScriptTeardownTests)

// A teardown that deadlocks never returns, so it runs on its own thread and the test waits this long for it
static const unsigned long TEARDOWN_TIMEOUT_MS = 10000;

void ScriptTeardownTests::initTestCase() {
    DependencyManager::set<ScriptEngines>(ScriptManager::NETWORKLESS_TEST_SCRIPT, QUrl(""));
    DependencyManager::set<ScriptCache>();
    DependencyManager::set<StatTracker>();
    DependencyManager::set<ScriptInitializers>();
}

void ScriptTeardownTests::testTeardownWithScriptOwnedObjects() {
    // Nothing listens on port 1, so the socket only fails to connect. Both objects stay referenced from globals until the
    // engine is destroyed, which deletes them as their owner.
    static const QString SCRIPT =
        "var request = new XMLHttpRequest();\n"
        "var socket = new WebSocket('ws://127.0.0.1:1');\n"
        "print(typeof request.open + ' ' + typeof socket.send);\n"
        "Script.stop(true);\n";

    QStringList printed;
    bool engineDestroyed = false;
    QThread* thread = QThread::create([&printed, &engineDestroyed] {
        auto manager = newScriptManager(ScriptManager::NETWORKLESS_TEST_SCRIPT, SCRIPT, "testTeardown.js");
        QObject::connect(manager.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString&) {
            printed.append(message);
        });
        QObject::connect(manager->engine().get(), &QObject::destroyed, [&engineDestroyed] { engineDestroyed = true; });
        {
            auto engine = manager->engine();
            auto scopeGuard = engine->getScopeGuard();
            // The test context registers neither constructor
            engine->globalObject().setProperty("XMLHttpRequest", engine->newFunction(XMLHttpRequestClass::constructor));
            engine->globalObject().setProperty("WebSocket", engine->newFunction(WebSocketClass::constructor));
            manager->run();
        }
        manager.reset();
        // The manager is deleted later, and it holds the engine
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    thread->start();
    if (!thread->wait(TEARDOWN_TIMEOUT_MS)) {
        // A deadlocked thread can be neither stopped nor deleted, so it is left to the end of the process
        QFAIL("script engine teardown did not finish");
    }
    delete thread;

    QCOMPARE(printed, QStringList({ "function function" }));
    QVERIFY(engineDestroyed);
}
