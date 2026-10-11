//
//  ScriptModuleEntityTests.cpp
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptModuleEntityTests.h"

#include <memory>

#include <QtCore/QRegularExpression>

#include "AccountManager.h"
#include "AddressManager.h"
#include "DomainAccountManager.h"
#include "NodeList.h"
#include "ScriptModuleTestRunner.h"

QTEST_MAIN(ScriptModuleEntityTests)

using Options = ModuleTestOptions;
using Files = QList<QPair<QString, QString>>;

static const QString ENTITY_A = "{0f6c1fa8-55a3-4a63-9b7b-2f8f9b0e4c11}";
static const QString ENTITY_B = "{5d2a7c31-8e44-4b0f-a1c9-3e6f20b7d952}";
static const QString CALLER = "{9b1e4f20-3c7d-4a85-b6e2-71d0c5a8f3e4}";
// Prepended to each test's main.mjs
static const QString TICK = "const tick = () => new Promise(function(resolve) { Script.setTimeout(resolve, 0); });\n";

ScriptValue EntityModuleProbe::load(const QString& entityID, const QString& url, bool reload) {
    ScriptManager* manager = _manager;
    EntityItemID id { QUuid(entityID) };
    ScriptPromise promise = manager->engine()->newPromise();
    auto resolver = promise.resolver;
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(manager, &ScriptManager::entityScriptDetailsUpdated, this, [manager, id, url, resolver, connection] {
        EntityScriptDetails details;
        if (!manager->getEntityScriptDetails(id, url, details) || details.status == EntityScriptStatus::PENDING
            || details.status == EntityScriptStatus::LOADING) {
            return;
        }
        QObject::disconnect(*connection);
        QString status = EntityScriptStatus_::valueToKey(details.status).toLower();
        QVariant result(details.errorInfo.isEmpty() ? status : status + ": " + details.errorInfo);
        // Settled from the event loop: settling it here would run the test script's continuation in the middle of the
        // load, before preload, when the load was not itself called from script
        QMetaObject::invokeMethod(manager, [resolver, result] { resolver->resolve(result); }, Qt::QueuedConnection);
    });
    manager->loadEntityScript(id, url, reload);
    DependencyManager::get<ScriptCache>()->getScriptContents(url,
        [this, manager, id, entityID, url](const QString&, const QString& contents, bool isURL, bool success, const QString& status) {
            QMetaObject::invokeMethod(manager, [this, manager, id, entityID, url, contents, isURL, success, status] {
                _handedOver.insert(entityID);
                QMetaObject::invokeMethod(manager, "entityScriptContentAvailable", Qt::DirectConnection,
                    Q_ARG(EntityItemID, id), Q_ARG(QString, url), Q_ARG(QString, contents), Q_ARG(bool, isURL),
                    Q_ARG(bool, success), Q_ARG(QString, status));
            }, Qt::QueuedConnection);
        }, reload);
    return promise.promise;
}

ScriptValue EntityModuleProbe::call(const QString& entityID, const QString& method, const QStringList& params,
                                    const QString& remoteCallerID) {
    ScriptManager* manager = _manager;
    ScriptPromise promise = manager->engine()->newPromise();
    auto resolver = promise.resolver;
    EntityItemID id { QUuid(entityID) };
    QUuid caller { remoteCallerID };
    QMetaObject::invokeMethod(manager, [manager, id, method, params, caller, resolver] {
        manager->callEntityScriptMethod(id, method, params, caller);
        resolver->resolve(QVariant(true));
    }, Qt::QueuedConnection);
    return promise.promise;
}

void EntityModuleProbe::writeFile(const QString& url, const QString& contents) {
    QFile file(QUrl(url).toLocalFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qFatal("cannot write %s", qUtf8Printable(url));
    }
    file.write(contents.toUtf8());
}

// "@A", "@B" and "@CALLER" stand for the IDs, "@/path" for the URL of a file in the test directory
static QString fill(QString text, const QTemporaryDir& dir) {
    text.replace("@A", ENTITY_A).replace("@B", ENTITY_B).replace("@CALLER", CALLER);
    static const QRegularExpression path("@/([\\w./-]+)");
    QRegularExpressionMatch match;
    while ((match = path.match(text)).hasMatch()) {
        text.replace(match.capturedStart(), match.capturedLength(), urlOf(dir, match.captured(1)));
    }
    return text;
}

// Runs main.mjs, the first file, with Probe
static ModuleTestRun runEntities(const QTemporaryDir& dir, Files files, std::unique_ptr<EntityModuleProbe>& probe) {
    for (auto& file : files) {
        file.second = fill(file.second, dir);
    }
    files.first().second = TICK + files.first().second;
    Options options;
    options.beforeRun = [&probe](ScriptManager* manager) {
        // loadEntityScript() refuses to load, through a null pointer, without the ScriptEngines that made the manager
        QSharedPointer<ScriptEngines> scriptEngines = DependencyManager::get<ScriptEngines>();
        manager->setScriptEngines(scriptEngines);
        probe = std::make_unique<EntityModuleProbe>(manager);
        manager->engine()->globalObject().setProperty("Probe", manager->engine()->newQObject(probe.get()));
        QObject::connect(manager, &ScriptManager::errorEntityMessage, probe.get(),
            [probe = probe.get()](const QString&, const QString&, int, const EntityItemID& entityID, bool) {
                probe->errorEntities.append(entityID.isNull() ? QString() : entityID.toString());
            });
    };
    return runScript(dir, files, options);
}

void ScriptModuleEntityTests::initTestCase() {
    // Loading an entity script consults the domain, for the entity script allowlist
    DependencyManager::registerInheritance<LimitedNodeList, NodeList>();
    DependencyManager::set<AccountManager>(true);
    DependencyManager::set<DomainAccountManager>();
    DependencyManager::set<AddressManager>();
    DependencyManager::set<NodeList>(NodeType::Agent, 0);
    initModuleTestDependencies();
}

void ScriptModuleEntityTests::testDefaultExportClass() {
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "print('status ' + await Probe.load('@A', '@/entity.mjs', false));\n"
          "Script.stop(true);\n" },
        { "entity.mjs",
          "print('body ' + import.meta.url + ' [' + Probe.entityID() + ']');\n"
          "export default class Entity {\n"
          "    constructor() { print('construct ' + Probe.entityID()); }\n"
          "    preload(id) { print('preload ' + id + ' ' + Probe.entityID() + ' ' + Probe.sandboxURL()); }\n"
          "}\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QString url = urlOf(dir, "entity.mjs");
    // The module body runs in the environment of the entity whose load evaluated it
    QCOMPARE(run.printed, QStringList({ "body " + url + " [" + ENTITY_A + "]", "construct " + ENTITY_A,
                                        "preload " + ENTITY_A + " " + ENTITY_A + " " + url, "status running" }));
}

void ScriptModuleEntityTests::testDefaultExportNotConstructor() {
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "print(await Probe.load('@A', '@/number.mjs', false));\n"
          "print(await Probe.load('@B', '@/none.mjs', false));\n"
          "Script.stop(true);\n" },
        { "number.mjs", "export default 42;\n" },
        { "none.mjs", "export const notDefault = class {};\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed.size(), 2);
    for (const QString& file : { QString("number.mjs"), QString("none.mjs") }) {
        QString expected = "Could not find constructor (" + urlOf(dir, file) + " has no default export that is a class or function)";
        QVERIFY2(run.printed.contains("error_running_script: " + expected), qPrintable(run.printed.join("\n")));
        QVERIFY2(run.errors.contains(expected), qPrintable(run.errors.join("\n")));
    }
    QCOMPARE(probe->errorEntities, QStringList({ ENTITY_A, ENTITY_B }));
}

void ScriptModuleEntityTests::testLoadFailureReported() {
    // Each is reported once, for its entity, and the status carries the file and line; the other scripts keep running
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "print(await Probe.load('@A', '@/missing.mjs', false));\n"
          "print(await Probe.load('@A', '@/broken.mjs', false));\n"
          "print(await Probe.load('@A', '@/throws.mjs', false));\n"
          "print('still running');\n"
          "Script.stop(true);\n" },
        { "missing.mjs", "export default class {}\nimport './nowhere.mjs';\n" },
        { "broken.mjs", "export default class {\n    oops(\n}\n" },
        { "throws.mjs", "export default class {}\nthrow new Error('body failed');\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed.size(), 4);
    QVERIFY2(run.printed[0].startsWith("error_running_script: Cannot find module './nowhere.mjs' imported from "
                                       + urlOf(dir, "missing.mjs") + ":2"), qPrintable(run.printed[0]));
    QVERIFY2(run.printed[1].startsWith("error_running_script: Error while compiling module " + urlOf(dir, "broken.mjs") + ":"),
             qPrintable(run.printed[1]));
    QVERIFY2(run.printed[2].startsWith("error_running_script: Error: body failed")
             && run.printed[2].contains(urlOf(dir, "throws.mjs") + ":2"), qPrintable(run.printed[2]));
    QCOMPARE(run.printed[3], QString("still running"));
    QCOMPARE(run.errors.size(), 3);
    QCOMPARE(probe->errorEntities, QStringList({ ENTITY_A, ENTITY_A, ENTITY_A }));
}

void ScriptModuleEntityTests::testTopLevelAwaitKeepsLoading() {
    // Both entities stay LOADING while the module is suspended in its top-level await, B having joined the evaluation
    // in progress, and construct and preload once it settles. The body runs once.
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "const loadedA = Probe.load('@A', '@/entity.mjs', false);\n"
          "while (typeof globalThis.releaseEntity !== 'function') { await tick(); }\n"
          "const loadedB = Probe.load('@B', '@/entity.mjs', false);\n"
          "while (!Probe.handedOver('@B')) { await tick(); }\n"
          "await tick();\n"
          "print('suspended ' + Probe.status('@A', '@/entity.mjs') + ' ' + Probe.status('@B', '@/entity.mjs'));\n"
          "print('released');\n"
          "globalThis.releaseEntity();\n"
          "print('A ' + await loadedA);\n"
          "print('B ' + await loadedB);\n"
          "Script.stop(true);\n" },
        { "entity.mjs",
          "print('body');\n"
          "await new Promise(function(resolve) { globalThis.releaseEntity = resolve; });\n"
          "export default class { preload(id) { print('preload ' + id); } }\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "body", "suspended loading loading", "released", "preload " + ENTITY_A,
                                        "preload " + ENTITY_B, "A running", "B running" }));
}

void ScriptModuleEntityTests::testSharedInstance() {
    // Q1: one module instance per URL and epoch; each entity constructs its own object from the shared default export
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "const before = Probe.moduleRecords();\n"
          "const statuses = await Promise.all([Probe.load('@A', '@/counter.mjs', false),\n"
          "                                    Probe.load('@B', '@/counter.mjs', false)]);\n"
          "print('statuses ' + statuses.join(' ') + ', records ' + (Probe.moduleRecords() - before));\n"
          "await Probe.call('@A', 'report', [], '');\n"
          "await Probe.call('@B', 'report', [], '');\n"
          "Script.stop(true);\n" },
        { "counter.mjs",
          "import { label } from './dep.mjs';\n"
          "let count = 0;\n"
          "print('module body');\n"
          "export default class Counter {\n"
          "    constructor() { this.number = ++count; }\n"
          "    preload(id) { print('preload ' + id + ' #' + this.number + ' ' + label); }\n"
          "    report(id) { print('report ' + id + ' #' + this.number + ' of ' + count); }\n"
          "}\n" },
        { "dep.mjs", "print('dep body');\nexport const label = 'dep';\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "dep body", "module body", "preload " + ENTITY_A + " #1 dep",
                                        "preload " + ENTITY_B + " #2 dep", "statuses running running, records 2",
                                        "report " + ENTITY_A + " #1 of 2", "report " + ENTITY_B + " #2 of 2" }));
}

void ScriptModuleEntityTests::testReloadNewEpoch() {
    // A reload of A loads the graph under a new epoch and fetches every module again, also its dependencies, and a
    // CommonJS one is evaluated again rather than taken from Script.require.cache. B, not reloaded, keeps the old
    // modules. Each epoch is released with its last entity, with the Script.require.cache entry it made.
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "await Probe.load('@A', '@/entity.mjs', false);\n"
          "await Probe.load('@B', '@/entity.mjs', false);\n"
          "print('cache ' + Script.require.cache['@/lib.js'].exports.version);\n"
          "Probe.writeFile('@/lib.js', 'print(\"lib body 2\"); module.exports = { version: 2 };');\n"
          "Probe.writeFile('@/dep.mjs', 'export const version = 2;');\n"
          "Probe.unload('@A', '@/entity.mjs');\n"
          "print('reload ' + await Probe.load('@A', '@/entity.mjs', true));\n"
          "await Probe.call('@A', 'report', [], '');\n"
          "await Probe.call('@B', 'report', [], '');\n"
          "print('cache ' + Script.require.cache['@/lib.js'].exports.version);\n"
          "const records = Probe.moduleRecords();\n"
          "Probe.unload('@B', '@/entity.mjs');\n"
          "print('released ' + (records - Probe.moduleRecords()));\n"
          "print('cache after B ' + Script.require.cache['@/lib.js'].exports.version);\n"
          "Probe.unload('@A', '@/entity.mjs');\n"
          "print('cache after A ' + Script.require.cache['@/lib.js']);\n"
          "Script.stop(true);\n" },
        { "entity.mjs",
          "import lib from './lib.js';\n"
          "import { version } from './dep.mjs';\n"
          "let count = 0;\n"
          "export default class {\n"
          "    constructor() { this.number = ++count; }\n"
          "    describe(id) { return id + ' lib ' + lib.version + ' dep ' + version + ' #' + this.number; }\n"
          "    preload(id) { print('preload ' + this.describe(id)); }\n"
          "    report(id) { print('report ' + this.describe(id)); }\n"
          "}\n" },
        { "lib.js", "print('lib body 1');\nmodule.exports = { version: 1 };\n" },
        { "dep.mjs", "export const version = 1;\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "lib body 1", "preload " + ENTITY_A + " lib 1 dep 1 #1",
                                        "preload " + ENTITY_B + " lib 1 dep 1 #2", "cache 1", "lib body 2",
                                        "preload " + ENTITY_A + " lib 2 dep 2 #1", "reload running",
                                        "report " + ENTITY_A + " lib 2 dep 2 #1", "report " + ENTITY_B + " lib 1 dep 1 #2",
                                        "cache 2", "released 3", "cache after B 2", "cache after A undefined" }));
}

void ScriptModuleEntityTests::testUnloadReleasesEpochAndReportsStalledAwait() {
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "Probe.load('@A', '@/entity.mjs', false);\n"
          "while (!globalThis.entityWaiting) { await tick(); }\n"
          "const records = Probe.moduleRecords();\n"
          "Probe.unload('@A', '@/entity.mjs');\n"
          "print('released ' + (records - Probe.moduleRecords()) + ', status [' + Probe.status('@A', '@/entity.mjs') + ']');\n"
          "Script.stop(true);\n" },
        { "entity.mjs",
          "globalThis.entityWaiting = true;\n"
          "await new Promise(function() {});\n"
          "export default class {}\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "released 1, status []" }));
    // Reported at the unload, and not again when the script stops
    QCOMPARE(run.warnings, QStringList({ "Entity script unloaded while a top-level await was still pending at "
                                         + urlOf(dir, "entity.mjs") + ":2" }));
}

void ScriptModuleEntityTests::testRemotelyCallableClassField() {
    // As a class field or assigned in the constructor; Script.remoteCallerID is the caller's while the method runs
    // synchronously
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "await Probe.load('@A', '@/field.mjs', false);\n"
          "await Probe.load('@B', '@/assigned.mjs', false);\n"
          "await Probe.call('@A', 'allowed', ['x', 'y'], '@CALLER');\n"
          "await Probe.call('@A', 'hidden', [], '@CALLER');\n"
          "await Probe.call('@A', 'later', [], '@CALLER');\n"
          "await Probe.call('@B', 'allowed', ['z'], '@CALLER');\n"
          "await Probe.call('@B', 'hidden', [], '@CALLER');\n"
          "Script.stop(true);\n" },
        { "field.mjs",
          "export default class {\n"
          "    remotelyCallable = ['allowed', 'later'];\n"
          "    allowed(id, params) { print('field allowed ' + params.join(',') + ' from ' + Script.remoteCallerID); }\n"
          "    hidden() { print('field hidden called'); }\n"
          "    async later(id) {\n"
          "        const caller = Script.remoteCallerID;\n"
          "        await null;\n"
          "        print('field later, read before await: ' + caller);\n"
          "    }\n"
          "}\n" },
        { "assigned.mjs",
          "export default class {\n"
          "    constructor() { this.remotelyCallable = ['allowed']; }\n"
          "    allowed(id, params) { print('assigned allowed ' + params.join(',') + ' from ' + Script.remoteCallerID); }\n"
          "    hidden() { print('assigned hidden called'); }\n"
          "}\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "field allowed x,y from " + CALLER, "field later, read before await: " + CALLER,
                                        "assigned allowed z from " + CALLER }));
}

void ScriptModuleEntityTests::testDynamicImportAfterAwait() {
    // From a method, after an await on a timer, import() resolves against the importing module and loads under the
    // entity's sandbox and epoch: the top-level script's instance of the same file is not shared
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "await import('./entity/dep.mjs');\n"
          "await Probe.load('@A', '@/entity/entity.mjs', false);\n" },
        { "entity/entity.mjs",
          "export default class {\n"
          "    async preload(id) {\n"
          "        await new Promise(function(resolve) { Script.setTimeout(resolve, 0); });\n"
          "        const dep = await import('./dep.mjs');\n"
          "        print('imported ' + dep.where + ' in [' + Probe.entityID() + ']');\n"
          "        Script.stop(true);\n"
          "    }\n"
          "}\n" },
        { "entity/dep.mjs",
          "print('dep body [' + Probe.entityID() + ']');\n"
          "export const where = import.meta.url + ' ' + Probe.sandboxURL();\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "dep body []", "dep body [" + ENTITY_A + "]",
                                        "imported " + urlOf(dir, "entity/dep.mjs") + " " + urlOf(dir, "entity/entity.mjs")
                                            + " in [" + ENTITY_A + "]" }));
}

void ScriptModuleEntityTests::testRejectedAsyncMethodReported() {
    // Before and after its first await; once each, for the entity, and not again as an unhandled rejection
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "await Probe.load('@A', '@/entity.mjs', false);\n"
          "await Probe.call('@A', 'failLater', [], '');\n"
          "await Probe.call('@A', 'failEarly', [], '');\n"
          "await tick();\n"
          "Script.stop(true);\n" },
        { "entity.mjs",
          "export default class {\n"
          "    async failLater(id) {\n"
          "        await null;\n"
          "        throw new Error('failed after await');\n"
          "    }\n"
          "    async failEarly(id) { throw new Error('failed before await'); }\n"
          "}\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QString url = urlOf(dir, "entity.mjs");
    QCOMPARE(run.errors.size(), 2);
    QVERIFY2(run.errors[0].startsWith("Uncaught (in promise) in entity script method failLater of " + url
                                      + ": Error: failed after await") && run.errors[0].contains(url + ":4"),
             qPrintable(run.errors[0]));
    QVERIFY2(run.errors[1].startsWith("Uncaught (in promise) in entity script method failEarly of " + url
                                      + ": Error: failed before await") && run.errors[1].contains(url + ":6"),
             qPrintable(run.errors[1]));
    QCOMPARE(probe->errorEntities, QStringList({ ENTITY_A, ENTITY_A }));
}

void ScriptModuleEntityTests::testTimerAfterAwaitClearedOnUnload() {
    // A timer the entity's preload sets after an await carries the entity, so unloading it stops the timer. If it did
    // not, the timer would fire before the one that ends the script, which is set later.
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "const timerSet = new Promise(function(resolve) { globalThis.timerSet = resolve; });\n"
          "await Probe.load('@A', '@/entity.mjs', false);\n"
          "await timerSet;\n"
          "Probe.unload('@A', '@/entity.mjs');\n"
          "Script.setTimeout(function() { print('end'); Script.stop(true); }, 0);\n" },
        { "entity.mjs",
          "export default class {\n"
          "    async preload(id) {\n"
          "        await new Promise(function(resolve) { Script.setTimeout(resolve, 0); });\n"
          "        Script.setTimeout(function() { print('entity timer fired'); }, 0);\n"
          "        globalThis.timerSet();\n"
          "    }\n"
          "}\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "end" }));
}

void ScriptModuleEntityTests::testClassicEntityUnchanged() {
    // Regression: a classic entity script still loads, runs and unloads as before, with no module records, and one with
    // a syntax error is still refused by the syntax check
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "const records = Probe.moduleRecords();\n"
          "print('status ' + await Probe.load('@A', '@/entity.js', false));\n"
          "print('records ' + (Probe.moduleRecords() - records));\n"

          "Probe.unload('@A', '@/entity.js');\n"
          "print('broken ' + (await Probe.load('@B', '@/broken.js', false)).startsWith('error_running_script: Bad syntax'));\n"
          "Script.stop(true);\n" },
        { "entity.js",
          "(function() {\n"
          "    this.preload = function(id) { print('classic preload ' + id); };\n"
          "    this.unload = function(id) { print('classic unload ' + id); };\n"
          "})\n" },
        { "broken.js", "(function() { this.preload = function( { }; })\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "classic preload " + ENTITY_A, "status running", "records 0",
                                        "classic unload " + ENTITY_A, "broken true" }));
}
