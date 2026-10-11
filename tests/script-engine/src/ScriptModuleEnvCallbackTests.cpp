//
//  ScriptModuleEnvCallbackTests.cpp
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptModuleEnvCallbackTests.h"

#include <memory>

#include <QPointer>
#include <QThread>
#include <QWebSocket>
#include <QWebSocketServer>

#include "AccountManager.h"
#include "AddressManager.h"
#include "DomainAccountManager.h"
#include "HeldScriptServer.h"
#include "NodeList.h"
#include "ScriptEnvironment.h"
#include "ScriptModuleTestRunner.h"
#include "ScriptPermissions.h"
#include "WebSocketClass.h"
#include "XMLHttpRequestClass.h"

QTEST_MAIN(ScriptModuleEnvCallbackTests)

using Options = ModuleTestOptions;

static const QString ENTITY = "{0f6c1fa8-55a3-4a63-9b7b-2f8f9b0e4c11}";
// Never fetched: the tests hand the source over. Not a file: URL, which ScriptManager would check for changes.
static const QString ENTITY_URL = "http://127.0.0.1:1/entity.js";

// Accepts WebSocket connections on 127.0.0.1, from a thread of its own, and sends each one "hello"
class HelloWebSocketServer {
public:
    HelloWebSocketServer() {
        _context.moveToThread(&_thread);
        _thread.start();
        QMetaObject::invokeMethod(&_context, [this] {
            _server = new QWebSocketServer("test", QWebSocketServer::NonSecureMode);
            _server->listen(QHostAddress::LocalHost, 0);
            _port = _server->serverPort();
            QObject::connect(_server, &QWebSocketServer::newConnection, _server, [this] {
                while (QWebSocket* socket = _server->nextPendingConnection()) {
                    socket->sendTextMessage("hello");
                }
            });
        }, Qt::BlockingQueuedConnection);
    }
    ~HelloWebSocketServer() {
        // The server owns the sockets it accepted
        QMetaObject::invokeMethod(&_context, [this] { delete _server; }, Qt::BlockingQueuedConnection);
        _thread.quit();
        _thread.wait();
    }
    QString url() const { return QString("ws://127.0.0.1:%1").arg(_port); }

private:
    QThread _thread;
    QObject _context;
    QWebSocketServer* _server { nullptr };
    quint16 _port { 0 };
};

QString CallbackProbe::entityID() const {
    EntityItemID id = _manager->currentEntityIdentifier();
    return id.isNull() ? QString() : id.toString();
}

bool CallbackProbe::allowed() const {
    return ScriptPermissions::isCurrentScriptAllowed(ScriptPermissions::Permission::SCRIPT_PERMISSION_GET_AVATAR_URL);
}

ScriptValue CallbackProbe::withModuleEnvironment(const QString& entityID, const QString& sandboxURL,
                                                 const ScriptValue& function) {
    auto environment = std::make_shared<ScriptEnvironment>(EntityItemID(QUuid(entityID)), QUrl(sandboxURL), 0);
    environment->noteLoaded(QUrl(sandboxURL));
    ScriptEnvironmentScope environmentScope(_manager->engine().get(), environment);
    return function.call();
}

ScriptValue CallbackProbe::loadClassicEntity(const QString& entityID, const QString& url, const QString& source) {
    ScriptPromise promise = _manager->engine()->newPromise();
    ScriptManager* manager = _manager;
    EntityItemID id { QUuid(entityID) };
    auto resolver = promise.resolver;
    QMetaObject::invokeMethod(manager, [manager, id, url, source, resolver] {
        bool invoked = QMetaObject::invokeMethod(manager, "entityScriptContentAvailable", Qt::DirectConnection,
            Q_ARG(EntityItemID, id), Q_ARG(QString, url), Q_ARG(QString, source), Q_ARG(bool, true), Q_ARG(bool, true),
            Q_ARG(QString, QString("OK")));
        if (invoked) {
            resolver->resolve(QVariant(true));
        } else {
            resolver->reject(QString("entityScriptContentAvailable could not be invoked"));
        }
    }, Qt::QueuedConnection);
    return promise.promise;
}

void CallbackProbe::unloadEntity(const QString& entityID, const QString& url) {
    _manager->unloadEntityScript(EntityItemID(QUuid(entityID)), url);
}

// An engine destroyed while it still owns a script-owned QObject deadlocks: ~ScriptEngineV8 clears its wrapper map
// under _qobjectWrapperMapProtect, and the object's destroyed handler takes the same lock. The tests delete the
// objects they made while the engine is still alive.
static QList<QPointer<QObject>> nativeObjects;

static ScriptValue newXMLHttpRequest(ScriptContext* context, ScriptEngine* engine) {
    ScriptValue value = XMLHttpRequestClass::constructor(context, engine);
    nativeObjects.append(value.toQObject());
    return value;
}

static ScriptValue newWebSocket(ScriptContext* context, ScriptEngine* engine) {
    ScriptValue value = WebSocketClass::constructor(context, engine);
    nativeObjects.append(value.toQObject());
    return value;
}

// The test context is NETWORKLESS_TEST_SCRIPT, where init() registers neither XMLHttpRequest nor WebSocket
static Options withProbe(std::unique_ptr<CallbackProbe>& probe, Options options = Options()) {
    options.beforeRun = [&probe](ScriptManager* manager) {
        auto engine = manager->engine();
        probe = std::make_unique<CallbackProbe>(manager);
        engine->globalObject().setProperty("Probe", engine->newQObject(probe.get()));
        engine->globalObject().setProperty("XMLHttpRequest", engine->newFunction(newXMLHttpRequest));
        engine->globalObject().setProperty("WebSocket", engine->newFunction(newWebSocket));
    };
    options.afterRun = [](ScriptManager*) {
        for (const QPointer<QObject>& object : nativeObjects) {
            delete object.data();
        }
        nativeObjects.clear();
    };
    return options;
}

void ScriptModuleEnvCallbackTests::initTestCase() {
    // Loading an entity script consults the domain, for the entity script allowlist
    DependencyManager::registerInheritance<LimitedNodeList, NodeList>();
    DependencyManager::set<AccountManager>(true);
    DependencyManager::set<DomainAccountManager>();
    DependencyManager::set<AddressManager>();
    DependencyManager::set<NodeList>(NodeType::Agent, 0);
    initModuleTestDependencies();
}

void ScriptModuleEnvCallbackTests::testXhrCallbackKeepsModuleEnvironment() {
    // onreadystatechange runs from the event loop in the environment it was assigned in: a local module's is trusted
    // until it loads a module over http:, and from then on a gated call in the callback is refused. Classic code that
    // import()s the same module keeps its frame rule.
    HeldScriptServer server("export const value = 'untrusted';\n");
    auto served = [&server](const QString& name) { return QString(server.url()).replace("held.js", name); };
    QString request =
        "const xhr = new XMLHttpRequest();\n"
        "xhr.onreadystatechange = function() {\n"
        "    if (xhr.readyState === xhr.DONE) {\n"
        "        print('xhr ' + xhr.status + ' ' + Probe.allowed());\n"
        "        Script.stop(true);\n"
        "    }\n"
        "};\n"
        "xhr.open('GET', '" + served("data.txt") + "');\n"
        "xhr.send();\n";
    std::unique_ptr<CallbackProbe> probe;

    QTemporaryDir trustedDir;
    auto trusted = runScript(trustedDir, { { "main.mjs", request } }, withProbe(probe));
    QVERIFY(!trusted.timedOut);
    QCOMPARE(trusted.errors, QStringList());
    QCOMPARE(trusted.printed, QStringList({ "xhr 200 true" }));

    QTemporaryDir untrustedDir;
    auto untrusted = runScript(untrustedDir, {
        { "main.mjs", "await import('" + served("untrusted.mjs") + "');\n" + request },
    }, withProbe(probe));
    QVERIFY(!untrusted.timedOut);
    QCOMPARE(untrusted.errors, QStringList());
    QCOMPARE(untrusted.printed, QStringList({ "xhr 200 false" }));

    QTemporaryDir classicDir;
    auto classic = runScript(classicDir, {
        { "classic.js", "import('" + served("classic.mjs") + "').then(function() {\n" + request + "});\n" },
    }, withProbe(probe));
    QVERIFY(!classic.timedOut);
    QCOMPARE(classic.errors, QStringList());
    QCOMPARE(classic.printed, QStringList({ "xhr 200 true" }));
}

void ScriptModuleEnvCallbackTests::testXhrCallbackTimerCarriesEntity() {
    // In an entity's module environment, the callback has the entity, and so do timers it sets: the second one is
    // cleared by unloadEntityScript, which the first calls. If it were not, it would fire before the timer that ends
    // the script.
    HeldScriptServer server("data\n");
    std::unique_ptr<CallbackProbe> probe;
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "await Probe.loadClassicEntity('" + ENTITY + "', '" + ENTITY_URL + "',\n"
          "    \"(function() { this.preload = function() { print('preload ' + Probe.entityID()); }; })\");\n"
          "await new Promise(function(resolve) {\n"
          "    Probe.withModuleEnvironment('" + ENTITY + "', '" + ENTITY_URL + "', function() {\n"
          "        const xhr = new XMLHttpRequest();\n"
          "        xhr.onreadystatechange = function() {\n"
          "            if (xhr.readyState !== xhr.DONE) {\n"
          "                return;\n"
          "            }\n"
          "            print('callback ' + Probe.entityID());\n"
          "            Script.setTimeout(function() {\n"
          "                print('timer ' + Probe.entityID());\n"
          "                Probe.unloadEntity('" + ENTITY + "', '" + ENTITY_URL + "');\n"
          "                resolve();\n"
          "            }, 0);\n"
          "            Script.setTimeout(function() { print('second timer after unload'); }, 0);\n"
          "        };\n"
          "        xhr.open('GET', '" + server.url() + "');\n"
          "        xhr.send();\n"
          "    });\n"
          "});\n"
          "Script.setTimeout(function() { print('end'); Script.stop(true); }, 0);\n" },
    }, withProbe(probe));
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "preload " + ENTITY, "callback " + ENTITY, "timer " + ENTITY, "end" }));
}

void ScriptModuleEnvCallbackTests::testWebSocketCallbacksKeepModuleEnvironment() {
    // onopen and onmessage run from the event loop in the environment they were assigned in, as for XMLHttpRequest
    HelloWebSocketServer webSocketServer;
    HeldScriptServer server("export const value = 'untrusted';\n");
    QString untrustedURL = QString(server.url()).replace("held.js", "untrusted.mjs");
    QString connect =
        "const ws = new WebSocket('" + webSocketServer.url() + "');\n"
        "ws.onopen = function() { print('open ' + Probe.allowed()); };\n"
        "ws.onmessage = function(message) {\n"
        "    print('message ' + message.data + ' ' + Probe.allowed() + ' [' + Probe.entityID() + ']');\n"
        "    Script.stop(true);\n"
        "};\n";
    std::unique_ptr<CallbackProbe> probe;

    QTemporaryDir trustedDir;
    auto trusted = runScript(trustedDir, { { "main.mjs", connect } }, withProbe(probe));
    QVERIFY(!trusted.timedOut);
    QCOMPARE(trusted.errors, QStringList());
    QCOMPARE(trusted.printed, QStringList({ "open true", "message hello true []" }));

    QTemporaryDir untrustedDir;
    auto untrusted = runScript(untrustedDir, {
        { "main.mjs", "await import('" + untrustedURL + "');\n" + connect },
    }, withProbe(probe));
    QVERIFY(!untrusted.timedOut);
    QCOMPARE(untrusted.errors, QStringList());
    QCOMPARE(untrusted.printed, QStringList({ "open false", "message hello false []" }));

    QTemporaryDir entityDir;
    auto entity = runScript(entityDir, {
        { "main.mjs",
          "Probe.withModuleEnvironment('" + ENTITY + "', '" + ENTITY_URL + "', function() {\n" + connect + "});\n" },
    }, withProbe(probe));
    QVERIFY(!entity.timedOut);
    QCOMPARE(entity.errors, QStringList());
    QCOMPARE(entity.printed, QStringList({ "open false", "message hello false [" + ENTITY + "]" }));
}
