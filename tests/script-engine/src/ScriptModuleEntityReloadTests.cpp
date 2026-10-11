//
//  ScriptModuleEntityReloadTests.cpp
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptModuleEntityReloadTests.h"

#include "ScriptModuleEntityProbe.h"

QTEST_MAIN(ScriptModuleEntityReloadTests)

void ScriptModuleEntityReloadTests::initTestCase() {
    initEntityTestDependencies();
}

void ScriptModuleEntityReloadTests::testReloadNewEpoch() {
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


void ScriptModuleEntityReloadTests::testUnloadReleasesEpochAndReportsStalledAwait() {
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

void ScriptModuleEntityReloadTests::testReloadedTogetherShareEpoch() {
    // As Reload All does: every entity unloaded, then all loaded again with forceRedownload before any content arrives.
    // They join one new epoch, so the body runs once more and the instance is shared again.
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "await Probe.load('@A', '@/counter.mjs', false);\n"
          "await Probe.load('@B', '@/counter.mjs', false);\n"
          "Probe.unload('@A', '@/counter.mjs');\n"
          "Probe.unload('@B', '@/counter.mjs');\n"
          "await Promise.all([Probe.load('@A', '@/counter.mjs', true), Probe.load('@B', '@/counter.mjs', true)]);\n"
          "await Probe.call('@A', 'report', [], '');\n"
          "await Probe.call('@B', 'report', [], '');\n"
          "Script.stop(true);\n" },
        { "counter.mjs",
          "let count = 0;\n"
          "print('module body');\n"
          "export default class {\n"
          "    constructor() { this.number = ++count; }\n"
          "    report(id) { print('report ' + id + ' #' + this.number + ' of ' + count); }\n"
          "}\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "module body", "module body", "report " + ENTITY_A + " #1 of 2",
                                        "report " + ENTITY_B + " #2 of 2" }));
}

void ScriptModuleEntityReloadTests::testJoiningReloadFetchesAnew() {
    // While A's reload fetches the entry, C is added with the same URL and its entry comes from ScriptCache, still the
    // old one, and is handed over first. C joins A's reload epoch, so the entry and its import are fetched anew.
    MutableScriptServer server;
    auto entity = [](int version) {
        return QString("import { version } from './dep.mjs';\n"
                       "let count = 0;\n"
                       "export default class {\n"
                       "    constructor() { this.number = ++count; }\n"
                       "    preload(id) { print('preload ' + id + ' entity %1 dep ' + version + ' #' + this.number); }\n"
                       "}\n").arg(version);
    };
    server.set("entity.mjs", entity(1));
    server.set("dep.mjs", "export const version = 1;\n");
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    QString script = "await Probe.load('@A', '@H/entity.mjs', false);\n"
                     "Probe.unload('@A', '@H/entity.mjs');\n"
                     "Server.set('entity.mjs', `" + entity(2) + "`);\n"
                     "Server.set('dep.mjs', 'export const version = 2;');\n"
                     "Probe.prepare('@A', '@H/entity.mjs', true);\n"
                     "Probe.prepare('@C', '@H/entity.mjs', false);\n"
                     "const loadedC = Probe.handOver('@C', '@H/entity.mjs', false);\n"
                     "const loadedA = Probe.handOver('@A', '@H/entity.mjs', true);\n"
                     "print('C ' + await loadedC + ', A ' + await loadedA);\n"
                     "Script.stop(true);\n";
    auto run = runEntities(dir, { { "main.mjs", script } }, probe, &server);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "preload " + ENTITY_A + " entity 1 dep 1 #1", "preload " + ENTITY_C + " entity 2 dep 2 #1",
                                        "preload " + ENTITY_A + " entity 2 dep 2 #2", "C running, A running" }));
}

void ScriptModuleEntityReloadTests::testCommonJsCacheIdentity() {
    // A first load takes a CommonJS file Script.require has already evaluated, as #26 does; a reload evaluates it again
    // and replaces the entry; so does a changed entry URL when the entry is another epoch's import. (A counter on
    // globalThis would not do: each CommonJS evaluation has a global object of its own.)
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "const first = Script.require('@/lib.js');\n"
          "await Probe.load('@A', '@/entity.mjs', false);\n"
          "print('first kept ' + (Script.require.cache['@/lib.js'].exports === first));\n"
          "Probe.unload('@A', '@/entity.mjs');\n"
          "await Probe.load('@A', '@/entity.mjs', true);\n"
          "print('first kept ' + (Script.require.cache['@/lib.js'].exports === first));\n"
          "const second = Script.require.cache['@/lib.js'].exports;\n"
          "await Probe.load('@B', '@/entity.mjs?v=2', false);\n"
          "print('second kept ' + (Script.require.cache['@/lib.js'].exports === second));\n"
          "Script.stop(true);\n" },
        { "entity.mjs",
          "import lib from './lib.js';\n"
          "export default class {\n"
          "    preload(id) { print('preload ' + id + ', required ' + (lib === Script.require('@/lib.js'))); }\n"
          "}\n" },
        { "lib.js", "print('lib body');\nmodule.exports = {};\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "lib body", "preload " + ENTITY_A + ", required true", "first kept true",
                                        "lib body", "preload " + ENTITY_A + ", required true", "first kept false",
                                        "lib body", "preload " + ENTITY_B + ", required true", "second kept false" }));
}

void ScriptModuleEntityReloadTests::testFailureKeepsSharedEpoch() {
    // A fails in its constructor while B runs on the same epoch: B is unaffected and the epoch's records stay
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "await Probe.load('@B', '@/entity.mjs', false);\n"
          "const records = Probe.moduleRecords();\n"
          "globalThis.failNext = true;\n"
          "print('A ' + (await Probe.load('@A', '@/entity.mjs', false)).split('\\n')[0]);\n"
          "print('records kept ' + (Probe.moduleRecords() === records));\n"
          "await Probe.call('@B', 'report', [], '');\n"
          "Script.stop(true);\n" },
        { "entity.mjs",
          "let count = 0;\n"
          "export default class {\n"
          "    constructor() {\n"
          "        if (globalThis.failNext) { globalThis.failNext = false; throw new Error('constructor failed'); }\n"
          "        this.number = ++count;\n"
          "    }\n"
          "    report(id) { print('report ' + id + ' #' + this.number + ' of ' + count); }\n"
          "}\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors.size(), 1);
    QCOMPARE(run.printed, QStringList({ "A error_running_script: Error: constructor failed", "records kept true",
                                        "report " + ENTITY_B + " #1 of 1" }));
}

void ScriptModuleEntityReloadTests::testUnloadWhileOtherWaitsInTopLevelAwait() {
    // A and B wait in the module's top-level await; unloading A keeps the epoch for B, which completes, and nothing is
    // reported as stalled
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "Probe.load('@A', '@/entity.mjs', false);\n"
          "const loadedB = Probe.load('@B', '@/entity.mjs', false);\n"
          "while (typeof globalThis.releaseEntity !== 'function' || !Probe.handedOver('@B')) { await tick(); }\n"
          "await tick();\n"
          "Probe.unload('@A', '@/entity.mjs');\n"
          "print('A [' + Probe.status('@A', '@/entity.mjs') + '] B ' + Probe.status('@B', '@/entity.mjs'));\n"
          "globalThis.releaseEntity();\n"
          "print('B ' + await loadedB);\n"
          "Script.stop(true);\n" },
        { "entity.mjs",
          "print('body');\n"
          "await new Promise(function(resolve) { globalThis.releaseEntity = resolve; });\n"
          "export default class { preload(id) { print('preload ' + id); } }\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.warnings, QStringList());
    QCOMPARE(run.printed, QStringList({ "body", "A [] B loading", "preload " + ENTITY_B, "B running" }));
}

void ScriptModuleEntityReloadTests::testSchemelessEntryURL() {
    // A path without a scheme is fetched as a file: URL, so it is a module with an epoch of its own: its import resolves
    // next to it, a reload fetches the import anew, and unloading releases its records
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "await Probe.load('@A', '@P/door.mjs', false);\n"
          "Probe.writeFile('@/dep.mjs', 'export const version = 2;');\n"
          "Probe.unload('@A', '@P/door.mjs');\n"
          "await Probe.load('@A', '@P/door.mjs', true);\n"
          "const records = Probe.moduleRecords();\n"
          "Probe.unload('@A', '@P/door.mjs');\n"
          "print('released ' + (records - Probe.moduleRecords()));\n"
          "Script.stop(true);\n" },
        { "door.mjs",
          "import { version } from './dep.mjs';\n"
          "export default class { preload(id) { print('preload ' + import.meta.url + ' dep ' + version); } }\n" },
        { "dep.mjs", "export const version = 1;\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QString url = urlOf(dir, "door.mjs");
    QCOMPARE(run.printed, QStringList({ "preload " + url + " dep 1", "preload " + url + " dep 2", "released 2" }));
}

void ScriptModuleEntityReloadTests::testCommonJsSharedAcrossEntryFiles() {
    // Entity scripts of different files importing one CommonJS file share it and Script.require.cache, whether or not
    // their graphs were fetched anew; the same file with another query replaces it
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    QString entity = "import util from './util.js';\n"
                     "export default class { preload(id) { globalThis.utils.push(util); "
                     "print('preload ' + id + ', required ' + (util === Script.require('@/util.js'))); } }\n";
    auto run = runEntities(dir, {
        { "main.mjs",
          "globalThis.utils = [];\n"
          "await Probe.load('@A', '@/door.mjs', false);\n"
          "await Probe.load('@B', '@/lamp.mjs', false);\n"
          "print('shared ' + (globalThis.utils[0] === globalThis.utils[1]));\n"
          "await Probe.load('@C', '@/door.mjs?v=2', false);\n"
          "print('replaced ' + (globalThis.utils[2] !== globalThis.utils[0]));\n"
          "Script.stop(true);\n" },
        { "door.mjs", entity },
        { "lamp.mjs", entity },
        { "util.js", "print('util body');\nmodule.exports = {};\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "util body", "preload " + ENTITY_A + ", required true",
                                        "preload " + ENTITY_B + ", required true", "shared true", "util body",
                                        "preload " + ENTITY_C + ", required true", "replaced true" }));
}

#include "moc_ScriptModuleEntityProbe.cpp"
