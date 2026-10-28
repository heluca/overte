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
#include "v8/ScriptEngineV8.h"

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

void ScriptTeardownTests::testMethodProxiesCollectedAfterScriptFinished() {
    // Like an Agent body script (#50): a socket, a timer and an update handler, and wrapped native objects it drops, whose
    // method proxies the garbage collector releases. They are made in a callback, as the ones made by top-level code stay
    // reachable. Nothing listens on port 1.
    static const QString SCRIPT =
        "var socket = new WebSocket('ws://127.0.0.1:1');\n"
        "Script.setInterval(function () {}, 10);\n"
        "Script.update.connect(function () {});\n"
        "Script.setTimeout(function () {\n"
        "    for (var i = 0; i < 100; i++) { new XMLHttpRequest(); }\n"
        "    print('created');\n"
        "    Script.stop(true);\n"
        "}, 0);\n";

    QStringList printed;
    bool engineDestroyed = false;
    QThread* thread = QThread::create([&printed, &engineDestroyed] {
        auto manager = newScriptManager(ScriptManager::NETWORKLESS_TEST_SCRIPT, SCRIPT, "testMethodProxies.js");
        QObject::connect(manager.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString&) {
            printed.append(message);
        });
        QObject::connect(manager->engine().get(), &QObject::destroyed, [&engineDestroyed] { engineDestroyed = true; });
        {
            auto engine = manager->engine();
            auto scopeGuard = engine->getScopeGuard();
            engine->globalObject().setProperty("XMLHttpRequest", engine->newFunction(XMLHttpRequestClass::constructor));
            engine->globalObject().setProperty("WebSocket", engine->newFunction(WebSocketClass::constructor));
            manager->run();
            // A collection like those the script's own callbacks trigger; its weak callbacks post deferred deletes
            std::dynamic_pointer_cast<ScriptEngineV8>(engine)->getIsolate()->LowMemoryNotification();
        }
        // As in an Agent after its script stopped: the thread's event loop delivers them while nothing holds the isolate
        // lock, before the manager and engine are deleted
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        manager.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    thread->start();
    if (!thread->wait(TEARDOWN_TIMEOUT_MS)) {
        QFAIL("script engine teardown did not finish");
    }
    delete thread;

    QCOMPARE(printed, QStringList({ "created" }));
    QVERIFY(engineDestroyed);
}

void ScriptTeardownTests::testSignalReconnectAfterCollection() {
    // Like an Agent that drops its scene handlers on scene_stop and connects them again on scene_start (#84): once the
    // last handler is disconnected nothing holds the signal object, and a collection before the reconnect releases it.
    // Each tick emits while disconnected, collects, reconnects and emits again.
    static const QString SCRIPT =
        "var received = [];\n"
        "function onPing(value) { received.push(value); }\n"
        "function attach() { source.ping.connect(onPing); }\n"
        "function detach() { source.ping.disconnect(onPing); }\n"
        "var cycle = 0;\n"
        "attach();\n"
        "source.fire(cycle);\n"
        "function step() {\n"
        "    detach();\n"
        "    source.fire(-1);\n"
        "    Script.requestGarbageCollection();\n"
        "    attach();\n"
        "    cycle++;\n"
        "    source.fire(cycle);\n"
        "    if (cycle < 5) {\n"
        "        Script.setTimeout(step, 0);\n"
        "    } else {\n"
        "        print(received.join(' '));\n"
        "        Script.stop(true);\n"
        "    }\n"
        "}\n"
        "Script.setTimeout(step, 0);\n";

    QStringList printed;
    QThread* thread = QThread::create([&printed] {
        SignalSource source;
        auto manager = newScriptManager(ScriptManager::NETWORKLESS_TEST_SCRIPT, SCRIPT, "testSignalReconnect.js");
        QObject::connect(manager.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString&) {
            printed.append(message);
        });
        {
            auto engine = manager->engine();
            auto scopeGuard = engine->getScopeGuard();
            engine->registerGlobalObject(scopeGuard.get(), "source", &source);
            manager->run();
        }
        manager.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    thread->start();
    if (!thread->wait(TEARDOWN_TIMEOUT_MS)) {
        QFAIL("script did not finish");
    }
    delete thread;

    QCOMPARE(printed, QStringList({ "0 1 2 3 4 5" }));
}
