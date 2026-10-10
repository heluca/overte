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
