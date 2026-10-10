//
//  ScriptModuleTests.cpp
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptModuleTests.h"

#include <QDir>
#include <QFile>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include "DependencyManager.h"
#include "ResourceManager.h"
#include "ResourceRequestObserver.h"
#include "ScriptCache.h"
#include "ScriptEngine.h"
#include "ScriptEngines.h"
#include "ScriptManager.h"
#include "ScriptModuleResolver.h"
#include "StatTracker.h"

QTEST_MAIN(ScriptModuleTests)

// Generous: a test only reaches this when a script never stops, and then fails on timedOut
static const int MODULE_TEST_TIMEOUT_MS = 10000;

// Serves one script at /held.js on 127.0.0.1, from a thread of its own. It answers a request only after the test thread,
// which is also the script thread, has worked through every event posted to it before the request arrived and every
// event those posted in turn: the response waits for two round trips through the test thread's event queue.
class HeldScriptServer {
public:
    explicit HeldScriptServer(const QByteArray& script) : _script(script) {
        _context.moveToThread(&_thread);
        _thread.start();
        QMetaObject::invokeMethod(&_context, [this] {
            _server = new QTcpServer();
            _server->listen(QHostAddress::LocalHost, 0);
            _port = _server->serverPort();
            QObject::connect(_server, &QTcpServer::newConnection, _server, [this] {
                while (QTcpSocket* socket = _server->nextPendingConnection()) {
                    serve(socket);
                }
            });
        }, Qt::BlockingQueuedConnection);
    }
    ~HeldScriptServer() {
        QMetaObject::invokeMethod(&_context, [this] { delete _server; }, Qt::BlockingQueuedConnection);
        _thread.quit();
        _thread.wait();
    }
    QString url() const { return QString("http://127.0.0.1:%1/held.js").arg(_port); }

private:
    void serve(QTcpSocket* socket) {
        auto received = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, received] {
            received->append(socket->readAll());
            if (!received->contains("\r\n\r\n")) {
                return;
            }
            QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/javascript\r\nContent-Length: " +
                QByteArray::number(_script.size()) + "\r\nConnection: close\r\n\r\n" + _script;
            QPointer<QTcpSocket> held(socket);
            QObject* context = &_context;
            QMetaObject::invokeMethod(qApp, [held, context, response] {
                QMetaObject::invokeMethod(qApp, [held, context, response] {
                    QMetaObject::invokeMethod(context, [held, response] {
                        if (held) {
                            held->write(response);
                            held->disconnectFromHost();
                        }
                    }, Qt::QueuedConnection);
                }, Qt::QueuedConnection);
            }, Qt::QueuedConnection);
        });
    }

    QByteArray _script;
    QThread _thread;
    QObject _context;
    QTcpServer* _server { nullptr };
    quint16 _port { 0 };
};

void ScriptModuleTests::initTestCase() {
    DependencyManager::set<ScriptEngines>(ScriptManager::NETWORKLESS_TEST_SCRIPT, QUrl(""));
    DependencyManager::set<ScriptCache>();
    // ScriptCache fetches file: URLs through ResourceManager, on its own thread, so the loader's async path is real
    DependencyManager::set<ResourceManager>(false);
    DependencyManager::set<ResourceRequestObserver>();
    DependencyManager::set<StatTracker>();
    DependencyManager::set<ScriptInitializers>();
}

QString ScriptModuleTests::urlOf(const QTemporaryDir& dir, const QString& path) {
    return QUrl::fromLocalFile(dir.filePath(path)).toString();
}

ScriptModuleTests::Run ScriptModuleTests::runScript(const QTemporaryDir& dir, const QList<QPair<QString, QString>>& files,
                                                   const Options& options) {
    for (const auto& file : files) {
        QString path = dir.filePath(file.first);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly)) {
            qFatal("cannot write %s", qUtf8Printable(path));
        }
        out.write(file.second.toUtf8());
    }

    Run run;
    for (const QString& path : options.cachedFiles) {
        auto cached = std::make_shared<int>(0);
        DependencyManager::get<ScriptCache>()->getScriptContents(urlOf(dir, path),
            [cached](const QString&, const QString&, bool, bool success, const QString&) { *cached = success ? 1 : -1; });
        if (!QTest::qWaitFor([cached] { return *cached != 0; }, MODULE_TEST_TIMEOUT_MS) || *cached < 0) {
            run.errors.append("Test setup: could not cache " + path);
            return run;
        }
    }
    QString entryURL = options.entryURL.isEmpty() ? urlOf(dir, files.first().first) : options.entryURL;
    ScriptManagerPointer sm = newScriptManager(ScriptManager::NETWORKLESS_TEST_SCRIPT, files.first().second, entryURL);
    sm->setAbortOnUncaughtException(options.abortOnUncaughtException);
    auto scopeGuard = sm->engine()->getScopeGuard();
    connect(sm.get(), &ScriptManager::printedMessage, [&run](const QString& message, const QString&) {
        run.printed.append(message);
    });
    connect(sm.get(), &ScriptManager::errorMessage, [&run](const QString& message, const QString&) {
        qWarning() << "Error from engine:" << message;
        run.errors.append(message);
    });

    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, sm.get(), [manager = sm.get(), &run] {
        run.timedOut = true;
        manager->stop();
    });
    timeout.start(MODULE_TEST_TIMEOUT_MS);
    QTimer stopper;
    stopper.setSingleShot(true);
    connect(&stopper, &QTimer::timeout, sm.get(), [manager = sm.get()] { manager->stop(); });
    if (options.stopAfterMs >= 0) {
        stopper.start(options.stopAfterMs);
    }

    sm->run();
    if (options.waitAfterRunMs > 0) {
        QTest::qWait(options.waitAfterRunMs);
    }
    run.uncaughtException = sm->getUncaughtException();
    return run;
}

void ScriptModuleTests::testResolveModuleSpecifier() {
    QUrl referrer("http://example.invalid/scripts/app/main.mjs");
    QString error;
    QCOMPARE(resolveModuleSpecifier("./b.mjs", referrer, referrer, &error),
             QUrl("http://example.invalid/scripts/app/b.mjs"));
    QCOMPARE(resolveModuleSpecifier("../lib/c.mjs", referrer, referrer, &error),
             QUrl("http://example.invalid/scripts/lib/c.mjs"));
    QCOMPARE(resolveModuleSpecifier("/root.mjs", referrer, referrer, &error), QUrl("http://example.invalid/root.mjs"));
    QCOMPARE(resolveModuleSpecifier("https://other.invalid/x.mjs", referrer, referrer, &error),
             QUrl("https://other.invalid/x.mjs"));
    QCOMPARE(resolveModuleSpecifier("atp:/models/x.mjs", referrer, referrer, &error), QUrl("atp:/models/x.mjs"));

    error.clear();
    QVERIFY(resolveModuleSpecifier("lodash", referrer, referrer, &error).isEmpty());
    QVERIFY2(error.contains("bare specifier 'lodash'"), qUtf8Printable(error));

    error.clear();
    QVERIFY(resolveModuleSpecifier("gopher://example.invalid/x.mjs", referrer, referrer, &error).isEmpty());
    QVERIFY2(error.contains("could not resolve"), qUtf8Printable(error));

    error.clear();
    QVERIFY(resolveModuleSpecifier("./b.mjs", QUrl("atp:0123abcd.mjs"), QUrl(), &error).isEmpty());
    QVERIFY2(error.contains("asset hash"), qUtf8Printable(error));

    // A remote script may not import local files outside the default scripts
    QTemporaryDir dir;
    QFile local(dir.filePath("local.mjs"));
    QVERIFY(local.open(QIODevice::WriteOnly));
    local.close();
    QUrl localURL = QUrl::fromLocalFile(dir.filePath("local.mjs"));
    error.clear();
    QVERIFY(resolveModuleSpecifier(localURL.toString(), referrer, referrer, &error).isEmpty());
    QVERIFY2(error.contains("outside of origin script"), qUtf8Printable(error));
    // ... but a local one may, and gets a precise error for a file that is not there
    QCOMPARE(resolveModuleSpecifier("./local.mjs", QUrl(urlOf(dir, "main.mjs")), QUrl(urlOf(dir, "main.mjs")), &error), localURL);
    error.clear();
    QVERIFY(resolveModuleSpecifier("./gone.mjs", QUrl(urlOf(dir, "main.mjs")), QUrl(urlOf(dir, "main.mjs")), &error).isEmpty());
    QVERIFY2(error.contains("path does not exist"), qUtf8Printable(error));

    // "/" from a local Windows referrer stays on its drive
    error.clear();
    QUrl windowsReferrer("file:///C:/a/m.mjs");
    QVERIFY(resolveModuleSpecifier("/lib/x.mjs", windowsReferrer, windowsReferrer, &error).isEmpty());
    QVERIFY2(error.contains("C:/lib/x.mjs"), qUtf8Printable(error));

    QVERIFY(isModuleURL(QUrl("http://example.invalid/a.mjs?v=2#top")));
    QVERIFY(!isModuleURL(QUrl("http://example.invalid/a.js")));
    QVERIFY(!isModuleURL(QUrl("http://example.invalid/a.js?x=.mjs")));
}

void ScriptModuleTests::testStaticImport() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "import greet, { name, shout as loud } from './lib.mjs';\n"
          "import * as lib from './lib.mjs';\n"
          "print(greet(name) + ' ' + loud('x') + ' ' + Object.keys(lib).sort().join('|'));\n"
          "Script.stop(true);\n" },
        { "lib.mjs",
          "export const name = 'module';\n"
          "export function shout(s) { return s.toUpperCase(); }\n"
          "export default function greet(who) { return 'hello ' + who; }\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "hello module X default|name|shout" }));
}

void ScriptModuleTests::testImportChain() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs", "import { a } from './a.mjs';\nprint(a);\nScript.stop(true);\n" },
        { "a.mjs", "import { b } from './lib/b.mjs';\nprint('eval a');\nexport const a = 'a' + b;\n" },
        { "lib/b.mjs", "import { c } from '../c.mjs';\nprint('eval b');\nexport const b = 'b' + c;\n" },
        { "c.mjs", "print('eval c');\nexport const c = 'c';\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "eval c", "eval b", "eval a", "abc" }));
}

void ScriptModuleTests::testDiamond() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "import { leftToken } from './left.mjs';\n"
          "import { rightToken } from './right.mjs';\n"
          "print(globalThis.sharedEvaluations + ',' + (leftToken === rightToken));\n"
          "Script.stop(true);\n" },
        { "left.mjs", "import { token } from './shared.mjs';\nexport const leftToken = token;\n" },
        { "right.mjs", "import { token } from './shared.mjs';\nexport const rightToken = token;\n" },
        { "shared.mjs",
          "globalThis.sharedEvaluations = (globalThis.sharedEvaluations || 0) + 1;\n"
          "export const token = {};\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "1,true" }));
}

void ScriptModuleTests::testCycle() {
    // b runs first and calls main's fa(), which is legal because function declarations are initialised at link time
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "import { fb } from './b.mjs';\n"
          "print('eval main');\n"
          "export function fa() { return 'A'; }\n"
          "print(fb());\n"
          "Script.stop(true);\n" },
        { "b.mjs",
          "import { fa } from './main.mjs';\n"
          "print('eval b, fa() = ' + fa());\n"
          "export function fb() { return 'B' + fa(); }\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "eval b, fa() = A", "eval main", "BA" }));
}

void ScriptModuleTests::testImportMetaUrl() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "import { libUrl } from './sub/lib.mjs';\n"
          "print(import.meta.url);\n"
          "print(libUrl);\n"
          "Script.stop(true);\n" },
        { "sub/lib.mjs", "export const libUrl = import.meta.url;\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ urlOf(dir, "main.mjs"), urlOf(dir, "sub/lib.mjs") }));
}

void ScriptModuleTests::testMissingModule() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs", "print('must not run');\nimport { x } from './nope.mjs';\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList());
    QCOMPARE(run.errors.length(), 1);
    QVERIFY2(run.errors[0].startsWith("Cannot find module './nope.mjs' imported from " + urlOf(dir, "main.mjs") + ":2 "),
             qUtf8Printable(run.errors[0]));
    QVERIFY(run.uncaughtException);
}

void ScriptModuleTests::testUnreachableModule() {
    // Fails in ScriptCache rather than in the resolver's local file check: the fetch error path
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs", "import { x } from 'http://127.0.0.1:1/missing.mjs';\nprint('must not run');\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList());
    QCOMPARE(run.errors.length(), 1);
    QVERIFY2(run.errors[0].startsWith("Cannot find module 'http://127.0.0.1:1/missing.mjs' imported from " +
                                      urlOf(dir, "main.mjs") + ":1 ("),
             qUtf8Printable(run.errors[0]));
}

void ScriptModuleTests::testBareSpecifier() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs", "import _ from 'lodash';\nprint('must not run');\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList());
    QCOMPARE(run.errors.length(), 1);
    QVERIFY2(run.errors[0].contains(urlOf(dir, "main.mjs") + ":1"), qUtf8Printable(run.errors[0]));
    QVERIFY2(run.errors[0].contains("bare specifier 'lodash' is not supported"), qUtf8Printable(run.errors[0]));
}

void ScriptModuleTests::testJsImport() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs", "\nimport lib from './lib.js';\nprint('must not run');\n" },
        { "lib.js", "module.exports = 1;\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList());
    QCOMPARE(run.errors.length(), 1);
    QVERIFY2(run.errors[0].startsWith("Cannot import './lib.js' from " + urlOf(dir, "main.mjs") + ":2: "),
             qUtf8Printable(run.errors[0]));
    QVERIFY2(run.errors[0].endsWith("importing a classic .js script from a module is not supported yet; use a .mjs module"),
             qUtf8Printable(run.errors[0]));
}

void ScriptModuleTests::testDependencySyntaxError() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs", "import { ok } from './dep.mjs';\nprint('must not run');\n" },
        { "dep.mjs", "export const ok = 1;\n\nexport const = 2;\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList());
    QCOMPARE(run.errors.length(), 1);
    QVERIFY2(run.errors[0].startsWith("Error while compiling module " + urlOf(dir, "dep.mjs") + ":3: SyntaxError"),
             qUtf8Printable(run.errors[0]));
}

void ScriptModuleTests::testUnresolvableCycle() {
    // Each module re-exports x from the other, so x resolves to nothing: a link error, before any module body runs
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs", "import { x } from './a.mjs';\nprint('must not run');\n" },
        { "a.mjs", "export { x } from './b.mjs';\n" },
        { "b.mjs", "export { x } from './a.mjs';\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList());
    QCOMPARE(run.errors.length(), 1);
    QVERIFY2(run.errors[0].startsWith("Error while linking module " + urlOf(dir, "a.mjs") +
                                      ":1: SyntaxError: Detected cycle while resolving name 'x'"),
             qUtf8Printable(run.errors[0]));
}

void ScriptModuleTests::testEvaluationError() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs", "import './lib.mjs';\nprint('main');\n" },
        { "lib.mjs", "print('before');\nthrow new Error('boom');\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList({ "before" }));
    // Reported once: not again as an unhandled rejection of the evaluation promise
    QCOMPARE(run.errors, QStringList({ "Error while evaluating module " + urlOf(dir, "lib.mjs") + ":2: Error: boom" }));
    QVERIFY(run.uncaughtException);
    QCOMPARE(run.uncaughtException->errorLine, 2);
}

void ScriptModuleTests::testLoadFailureStopsWithoutAbort() {
    QTemporaryDir dir;
    Options options;
    options.abortOnUncaughtException = false;
    auto run = runScript(dir, { { "main.mjs", "import { x } from './nope.mjs';\n" } }, options);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors.length(), 1);
    QVERIFY2(run.errors[0].startsWith("Cannot find module './nope.mjs'"), qUtf8Printable(run.errors[0]));
}

void ScriptModuleTests::testEvaluationErrorKeepsRunningWithoutAbort() {
    // Like a classic script: what the module set up before it threw keeps working
    QTemporaryDir dir;
    Options options;
    options.abortOnUncaughtException = false;
    auto run = runScript(dir, {
        { "main.mjs",
          "Script.setTimeout(function() { print('still running'); Script.stop(true); }, 50);\n"
          "throw new Error('boom');\n" },
    }, options);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList({ "Error while evaluating module " + urlOf(dir, "main.mjs") + ":2: Error: boom" }));
    QCOMPARE(run.printed, QStringList({ "still running" }));
}

void ScriptModuleTests::testStopDuringFetch() {
    // The fetch fails after ScriptCache's retry, well after the stop and after run() has returned. The stopped script
    // must neither report it nor hear about it (the callback would set an uncaught exception and log).
    QTemporaryDir dir;
    Options options;
    options.stopAfterMs = 50;
    options.waitAfterRunMs = 2000;
    auto run = runScript(dir, {
        { "main.mjs", "import { x } from 'http://127.0.0.1:1/slow.mjs';\nprint('must not run');\n" },
    }, options);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList());
    QCOMPARE(run.errors, QStringList());
    QVERIFY(!run.uncaughtException);
}

void ScriptModuleTests::testRemoteEntryRefusesLocalImport() {
    QTemporaryDir dir;
    Options options;
    options.entryURL = "http://127.0.0.1:1/remote/main.mjs";
    auto run = runScript(dir, {
        { "main.mjs", "import { secret } from '" + urlOf(dir, "local.mjs") + "';\nprint(secret);\n" },
        { "local.mjs", "export const secret = 'local';\n" },
    }, options);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList());
    QCOMPARE(run.errors.length(), 1);
    QVERIFY2(run.errors[0].startsWith("Cannot find module '" + urlOf(dir, "local.mjs") +
                                      "' imported from http://127.0.0.1:1/remote/main.mjs:1 (path "),
             qUtf8Printable(run.errors[0]));
    QVERIFY2(run.errors[0].contains("outside of origin script"), qUtf8Printable(run.errors[0]));
}

void ScriptModuleTests::testClassicScriptUnchanged() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "classic.js",
          "var lib = Script.require('./lib.js');\n"
          "print('classic ' + lib.value + ' ' + (typeof module));\n"
          "Script.stop(true);\n" },
        { "lib.js", "module.exports = { value: 42 };\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "classic 42 undefined" }));
}

void ScriptModuleTests::testMicrotaskOrdering() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "import { order } from './order.mjs';\n"
          "Script.setTimeout(function() {\n"
          "    order.push('timeout');\n"
          "    print(order.join(','));\n"
          "    Script.stop(true);\n"
          "}, 0);\n"
          "Promise.resolve().then(function() { order.push('then'); })\n"
          "    .then(function() { order.push('then2'); });\n"
          "(async function() { await null; order.push('await'); })();\n"
          "order.push('sync');\n" },
        { "order.mjs", "export const order = [];\nPromise.resolve().then(function() { order.push('dependency then'); });\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "sync,dependency then,then,await,then2,timeout" }));
}

void ScriptModuleTests::testTopLevelAwait() {
    // dep's top-level await holds main back; main's own resumes from a timer and then runs microtasks and timers as usual
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "import { depValue } from './dep.mjs';\n"
          "print('main sees ' + depValue);\n"
          "const resumed = await new Promise(function(resolve) {\n"
          "    Script.setTimeout(function() { resolve('timer value'); }, 10);\n"
          "});\n"
          "print('resumed with ' + resumed);\n"
          "Promise.resolve().then(function() { print('microtask after resume'); });\n"
          "Script.setTimeout(function() { print('timer after resume'); Script.stop(true); }, 0);\n" },
        { "dep.mjs",
          "print('dep before await');\n"
          "export const depValue = await new Promise(function(resolve) {\n"
          "    Script.setTimeout(function() { resolve('dep value'); }, 10);\n"
          "});\n"
          "print('dep after await');\n" },
    });
    QVERIFY(!run.timedOut);
    // Also: an await that completed is not reported as stalled when the script stops
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "dep before await", "dep after await", "main sees dep value",
                                        "resumed with timer value", "microtask after resume", "timer after resume" }));
}

void ScriptModuleTests::testTopLevelAwaitRejection() {
    // Without the stop, the timer would print and the script would run until the test times out
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "Script.setTimeout(function() { print('must not run'); }, 200);\n"
          "print('before await');\n"
          "await new Promise(function(resolve) { Script.setTimeout(resolve, 10); });\n"
          "throw new Error('late');\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList({ "before await" }));
    // Reported once: not again as an unhandled rejection of the evaluation promise
    QCOMPARE(run.errors, QStringList({ "Error while evaluating module " + urlOf(dir, "main.mjs") + ":4: Error: late" }));
    QVERIFY(run.uncaughtException);
    QCOMPARE(run.uncaughtException->errorLine, 4);
}

void ScriptModuleTests::testTopLevelAwaitRejectionWithoutAbort() {
    QTemporaryDir dir;
    Options options;
    options.abortOnUncaughtException = false;
    auto run = runScript(dir, {
        { "main.mjs",
          "Script.setTimeout(function() { print('still running'); Script.stop(true); }, 200);\n"
          "await new Promise(function(resolve) { Script.setTimeout(resolve, 10); });\n"
          "throw new Error('late');\n" },
    }, options);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList({ "Error while evaluating module " + urlOf(dir, "main.mjs") + ":3: Error: late" }));
    QCOMPARE(run.printed, QStringList({ "still running" }));
}

void ScriptModuleTests::testStalledTopLevelAwait() {
    // Both the entry and a module it imported dynamically wait forever; late.mjs stops the script once it is suspended
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "import('./late.mjs');\n"
          "print('before await');\n"
          "await new Promise(function() {});\n"
          "print('must not run');\n" },
        { "late.mjs",
          "Script.setTimeout(function() { Script.stop(true); }, 0);\n"
          "await new Promise(function() {});\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList({ "before await" }));
    QCOMPARE(run.errors, QStringList({ "Script stopped while a top-level await was still pending at " +
                                       urlOf(dir, "late.mjs") + ":2, " + urlOf(dir, "main.mjs") + ":3" }));
}

void ScriptModuleTests::testDynamicImportFromModule() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "print('before import');\n"
          "const lib = await import('./sub/lib.mjs');\n"
          "print(lib.describe() + ', ' + lib.default);\n"
          "Script.stop(true);\n" },
        { "sub/lib.mjs",
          "import { helper } from './helper.mjs';\n"
          "export default 'default export';\n"
          "export function describe() { return 'lib with ' + helper + ' at ' + import.meta.url; }\n" },
        { "sub/helper.mjs", "export const helper = 'helper';\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "before import",
                                        "lib with helper at " + urlOf(dir, "sub/lib.mjs") + ", default export" }));
}

void ScriptModuleTests::testDynamicImportFromClassicScript() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "classic.js",
          "print('classic start');\n"
          "import('./lib.mjs').then(function(ns) {\n"
          "    print('classic got ' + ns.value + ' ' + Object.prototype.toString.call(ns));\n"
          "    Script.stop(true);\n"
          "}, function(error) {\n"
          "    print('failed: ' + error);\n"
          "    Script.stop(true);\n"
          "});\n"
          "print('classic end');\n" },
        { "lib.mjs", "export const value = 42;\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "classic start", "classic end", "classic got 42 [object Module]" }));
}

void ScriptModuleTests::testDynamicImportOfLoadedModule() {
    // The module map's record is reused: same namespace object, and the body runs once
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "import * as statically from './lib.mjs';\n"
          "const first = await import('./lib.mjs');\n"
          "const second = await import('./lib.mjs');\n"
          "print((first === statically) + ',' + (second === statically) + ',' + globalThis.libEvaluations);\n"
          "Script.stop(true);\n" },
        { "lib.mjs",
          "globalThis.libEvaluations = (globalThis.libEvaluations || 0) + 1;\n"
          "export const value = 1;\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "true,true,1" }));
}

void ScriptModuleTests::testDynamicImportMissing() {
    // A rejection, not a script stop: reported only if the script leaves it unhandled
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "import('./gone.mjs');\n"
          "import('./nope.mjs').then(function() { print('must not resolve'); }, function(error) {\n"
          "    print(error.name + ': ' + error.message);\n"
          "    Script.stop(true);\n"
          "});\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed.length(), 1);
    QVERIFY2(run.printed[0].startsWith("TypeError: Cannot find module './nope.mjs' imported from " +
                                       urlOf(dir, "main.mjs") + ":2 ("),
             qUtf8Printable(run.printed[0]));
    QCOMPARE(run.errors.length(), 1);
    QVERIFY2(run.errors[0].startsWith("Unhandled promise rejection: TypeError: Cannot find module './gone.mjs' imported from " +
                                      urlOf(dir, "main.mjs") + ":1 ("),
             qUtf8Printable(run.errors[0]));
    QVERIFY(!run.uncaughtException);
}

void ScriptModuleTests::testDynamicImportBareSpecifier() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "\n"
          "import('lodash').catch(function(error) { print(error.name + ': ' + error.message); Script.stop(true); });\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed.length(), 1);
    QVERIFY2(run.printed[0].startsWith("TypeError: Cannot find module 'lodash' imported from " + urlOf(dir, "main.mjs") + ":2 ("),
             qUtf8Printable(run.printed[0]));
    QVERIFY2(run.printed[0].contains("bare specifier 'lodash' is not supported"), qUtf8Printable(run.printed[0]));
}

void ScriptModuleTests::testDynamicImportThrowingBody() {
    // The module is left errored: a second import() rejects with the same error instead of evaluating it again
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "let first;\n"
          "try {\n"
          "    await import('./bad.mjs');\n"
          "} catch (error) {\n"
          "    first = error;\n"
          "    print('first: ' + error.message + ', stack names bad.mjs:2: ' + error.stack.includes('bad.mjs:2'));\n"
          "}\n"
          "try {\n"
          "    await import('./bad.mjs');\n"
          "} catch (error) {\n"
          "    print('second: ' + error.message + ', same error: ' + (error === first));\n"
          "}\n"
          "print('evaluations: ' + globalThis.badEvaluations);\n"
          "Script.stop(true);\n" },
        { "bad.mjs",
          "globalThis.badEvaluations = (globalThis.badEvaluations || 0) + 1;\n"
          "throw new Error('bad body');\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "first: bad body, stack names bad.mjs:2: true",
                                        "second: bad body, same error: true", "evaluations: 1" }));
    QVERIFY(!run.uncaughtException);
}

void ScriptModuleTests::testDynamicImportTopLevelAwaitRejection() {
    // Only the import() promise rejects: nothing is reported and the script keeps running, abort on uncaught or not
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "import('./late.mjs').catch(function(error) {\n"
          "    print('caught: ' + error.message);\n"
          "    Script.setTimeout(function() { print('still running'); Script.stop(true); }, 20);\n"
          "});\n" },
        { "late.mjs",
          "await new Promise(function(resolve) { Script.setTimeout(resolve, 10); });\n"
          "throw new Error('late failure');\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "caught: late failure", "still running" }));
    QVERIFY(!run.uncaughtException);
}

void ScriptModuleTests::testDeferredEvaluationInNestedLoop() {
    // m.mjs is cached, so its whole load is two events queued on the script thread ahead of the include's response
    // (see HeldScriptServer): the nested event loop of the synchronous Script.include delivers the completed graph
    // while classic.js is still on the stack. The module body must still wait until classic.js has returned.
    HeldScriptServer server("print('included');\n");
    QTemporaryDir dir;
    Options options;
    options.cachedFiles = QStringList({ "m.mjs" });
    auto run = runScript(dir, {
        { "classic.js",
          "import('./m.mjs').then(function() { print('import resolved'); Script.stop(true); });\n"
          "print('before include');\n"
          "Script.include('" + server.url() + "');\n"
          "print('after include');\n" },
        { "m.mjs", "print('module body');\n" },
    }, options);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "before include", "included", "after include", "module body", "import resolved" }));
}

void ScriptModuleTests::testDynamicImportMicrotaskOrdering() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "globalThis.order = [];\n"
          "Promise.resolve().then(function() { order.push('then'); });\n"
          "const pending = import('./lib.mjs');\n"
          "order.push('sync');\n"
          "await pending;\n"
          "order.push('after await');\n"
          "print(order.join(','));\n"
          "Script.stop(true);\n" },
        { "lib.mjs",
          "order.push('lib');\n"
          "Promise.resolve().then(function() { order.push('lib then'); });\n"
          "export const x = 1;\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "sync,then,lib,lib then,after await" }));
}
