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

#include "ScriptModuleEntityProbe.h"

QTEST_MAIN(ScriptModuleEntityTests)

void ScriptModuleEntityTests::initTestCase() {
    initEntityTestDependencies();
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
    // From a method, after an await on a promise the test script settles, import() resolves against the importing
    // module and loads under the entity's sandbox and epoch: the top-level script's instance of the file is not shared.
    // Only the environment the await carries says whose continuation it is.
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "let go;\n"
          "globalThis.go = new Promise(function(resolve) { go = resolve; });\n"
          "await import('./entity/dep.mjs');\n"
          "await Probe.load('@A', '@/entity/entity.mjs', false);\n"
          "go();\n" },
        { "entity/entity.mjs",
          "export default class {\n"
          "    async preload(id) {\n"
          "        await globalThis.go;\n"
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
    // Before and after its first await; once each, for the entity, and not again as an unhandled rejection. A promise
    // the method handles itself before returning it is not reported.
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "await Probe.load('@A', '@/entity.mjs', false);\n"
          "await Probe.call('@A', 'failLater', [], '');\n"
          "await Probe.call('@A', 'failEarly', [], '');\n"
          "await Probe.call('@A', 'handled', [], '');\n"
          "await tick();\n"
          "Script.stop(true);\n" },
        { "entity.mjs",
          "export default class {\n"
          "    async failLater(id) {\n"
          "        await null;\n"
          "        throw new Error('failed after await');\n"
          "    }\n"
          "    async failEarly(id) { throw new Error('failed before await'); }\n"
          "    handled(id) {\n"
          "        const p = Promise.reject(new Error('handled here'));\n"
          "        p.catch(function(error) { print('caught ' + error.message); });\n"
          "        return p;\n"
          "    }\n"
          "}\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList({ "caught handled here" }));
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
    // not, the timer would fire before the one that ends the script, which is set later. The await is on a promise the
    // test script settles, so only the environment the await carries says whose continuation it is.
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "let go;\n"
          "globalThis.go = new Promise(function(resolve) { go = resolve; });\n"
          "const timerSet = new Promise(function(resolve) { globalThis.timerSet = resolve; });\n"
          "await Probe.load('@A', '@/entity.mjs', false);\n"
          "go();\n"
          "await timerSet;\n"
          "Probe.unload('@A', '@/entity.mjs');\n"
          "Script.setTimeout(function() { print('end'); Script.stop(true); }, 0);\n" },
        { "entity.mjs",
          "export default class {\n"
          "    async preload(id) {\n"
          "        await globalThis.go;\n"
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

void ScriptModuleEntityTests::testFailedModuleLeavesClassicEntityRunning() {
    // A module entity script that fails to load leaves no uncaught exception behind: a classic entity script constructed
    // right after, before the event loop would clear one, still gets its object and preload
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "Probe.prepare('@B', '@/classic.js', false);\n"
          "const classic = Probe.settled('@B', '@/classic.js');\n"
          "await Probe.handOverOnError('@A', '@/broken.mjs', '@B', '@/classic.js');\n"
          "print('module ' + (await Probe.load('@A', '@/broken.mjs', false)).split(':')[0]);\n"
          "print('classic ' + await classic);\n"
          "await Probe.call('@B', 'ping', [], '');\n"
          "Script.stop(true);\n" },
        { "broken.mjs", "import './nowhere.mjs';\nexport default class {}\n" },
        { "classic.js",
          "(function() {\n"
          "    this.preload = function(id) { print('classic preload ' + id); };\n"
          "    this.ping = function(id) { print('classic ping ' + id); };\n"
          "})\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList({ "classic preload " + ENTITY_B, "module error_running_script", "classic running",
                                        "classic ping " + ENTITY_B }));
    QCOMPARE(run.errors.size(), 1);
}

void ScriptModuleEntityTests::testConstructorThrows() {
    // Synchronously and after a top-level await: the entity fails with the constructor's stack, reported once for it,
    // and nothing is left pending to surface as an unhandled rejection
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "print(await Probe.load('@A', '@/throws.mjs', false));\n"
          "print(await Probe.load('@B', '@/awaits.mjs', false));\n"
          "await tick();\n"
          "Script.stop(true);\n" },
        { "throws.mjs",
          "export default class {\n"
          "    constructor() { throw new Error('constructor failed'); }\n"
          "}\n" },
        { "awaits.mjs",
          "await null;\n"
          "export default class {\n"
          "    constructor() { throw new Error('constructor failed after await'); }\n"
          "}\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed.size(), 2);
    QVERIFY2(run.printed[0].startsWith("error_running_script: Error: constructor failed")
             && run.printed[0].contains(urlOf(dir, "throws.mjs") + ":2"), qPrintable(run.printed[0]));
    QVERIFY2(run.printed[1].startsWith("error_running_script: Error: constructor failed after await")
             && run.printed[1].contains(urlOf(dir, "awaits.mjs") + ":3"), qPrintable(run.printed[1]));
    QCOMPARE(run.errors.size(), 2);
    QVERIFY2(run.errors[0].startsWith("Error in the constructor of " + urlOf(dir, "throws.mjs") + ": Error: constructor failed"),
             qPrintable(run.errors[0]));
    QVERIFY2(run.errors[1].startsWith("Error in the constructor of " + urlOf(dir, "awaits.mjs")), qPrintable(run.errors[1]));
    QCOMPARE(probe->errorEntities, QStringList({ ENTITY_A, ENTITY_B }));
}

void ScriptModuleEntityTests::testPreloadThrows() {
    // As for a classic entity script: reported for the entity, which stays RUNNING and keeps its methods
    std::unique_ptr<EntityModuleProbe> probe;
    QTemporaryDir dir;
    auto run = runEntities(dir, {
        { "main.mjs",
          "print(await Probe.load('@A', '@/entity.mjs', false));\n"
          "await Probe.call('@A', 'ping', [], '');\n"
          "Script.stop(true);\n" },
        { "entity.mjs",
          "export default class {\n"
          "    preload(id) { throw new Error('preload failed'); }\n"
          "    ping(id) { print('ping ' + id); }\n"
          "}\n" },
    }, probe);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList({ "running", "ping " + ENTITY_A }));
    // Two reports, as for a classic entity script: the call's, with the stack, and a generic one after it
    QCOMPARE(run.errors.size(), 2);
    QVERIFY2(run.errors[0].contains("preload failed"), qPrintable(run.errors[0]));
    QCOMPARE(probe->errorEntities, QStringList({ ENTITY_A, ENTITY_A }));
}

#include "moc_ScriptModuleEntityProbe.cpp"
