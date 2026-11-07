//
//  ScriptModuleAsyncTests.cpp
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptModuleAsyncTests.h"

#include "HeldScriptServer.h"
#include "ScriptModuleTestRunner.h"

QTEST_MAIN(ScriptModuleAsyncTests)

using Options = ModuleTestOptions;

void ScriptModuleAsyncTests::initTestCase() {
    initModuleTestDependencies();
}

void ScriptModuleAsyncTests::testTopLevelAwait() {
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

void ScriptModuleAsyncTests::testTopLevelAwaitRejection() {
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

void ScriptModuleAsyncTests::testTopLevelAwaitRejectionWithoutAbort() {
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

void ScriptModuleAsyncTests::testStalledTopLevelAwait() {
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
    // A warning, not an error: stopping mid-await is routine (reload, a user stop during a fetch)
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.warnings.filter("top-level await"),
             QStringList({ "Script stopped while a top-level await was still pending at " + urlOf(dir, "late.mjs") +
                           ":2, " + urlOf(dir, "main.mjs") + ":3" }));
}

void ScriptModuleAsyncTests::testTopLevelAwaitRejectionWithoutLocation() {
    // A non-Error value has no stack: the report says so rather than pointing at a line of the entry
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "await new Promise(function(resolve) { Script.setTimeout(resolve, 10); });\n"
          "throw 'plain value';\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList({ "Error while evaluating module " + urlOf(dir, "main.mjs") +
                                       ": plain value (no location: rejected with a non-Error value)" }));
}

void ScriptModuleAsyncTests::testDynamicImportFromModule() {
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

void ScriptModuleAsyncTests::testDynamicImportFromClassicScript() {
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

void ScriptModuleAsyncTests::testDynamicImportOfLoadedModule() {
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

void ScriptModuleAsyncTests::testDynamicImportMissing() {
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

void ScriptModuleAsyncTests::testDynamicImportBareSpecifier() {
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

void ScriptModuleAsyncTests::testDynamicImportThrowingBody() {
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

void ScriptModuleAsyncTests::testDynamicImportTopLevelAwaitRejection() {
    // Only the import() promise rejects: nothing is reported and the script keeps running (abort on; off below)
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

void ScriptModuleAsyncTests::testDynamicImportTopLevelAwaitRejectionWithoutAbort() {
    QTemporaryDir dir;
    Options options;
    options.abortOnUncaughtException = false;
    auto run = runScript(dir, {
        { "main.mjs",
          "import('./late.mjs').catch(function(error) {\n"
          "    print('caught: ' + error.message);\n"
          "    Script.setTimeout(function() { print('still running'); Script.stop(true); }, 20);\n"
          "});\n" },
        { "late.mjs",
          "await new Promise(function(resolve) { Script.setTimeout(resolve, 10); });\n"
          "throw new Error('late failure');\n" },
    }, options);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "caught: late failure", "still running" }));
    QVERIFY(!run.uncaughtException);
}

void ScriptModuleAsyncTests::testDynamicImportRefusals() {
    // The checks static imports make, through the same resolveImport: attributes and extensions
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "import('./lib.mjs', { with: { type: 'json' } }).then(function() { print('must not resolve'); }, function(error) {\n"
          "    print(error.name + ': ' + error.message);\n"
          "    return import('./notes.txt');\n"
          "}).then(function() { print('must not resolve'); }, function(error) {\n"
          "    print(error.name + ': ' + error.message);\n"
          "    Script.stop(true);\n"
          "});\n" },
        { "lib.mjs", "export const value = 1;\n" },
        { "notes.txt", "text\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QString main = urlOf(dir, "main.mjs");
    QCOMPARE(run.printed, QStringList({
        "TypeError: Cannot import './lib.mjs' from " + main + ":1: with { type: 'json' } is only for .json files",
        "TypeError: Cannot import './notes.txt' from " + main + ":3: only .mjs (ES module), .js (CommonJS) and .json "
        "files can be imported" }));
}

void ScriptModuleAsyncTests::testDynamicImportRemoteEntryRefusesLocal() {
    // import() takes its sandbox from the manager's script URL, not from a loader request: check it separately from
    // the static case, for a remote module entry and a remote classic script
    for (const QString& entry : { QString("main.mjs"), QString("classic.js") }) {
        QTemporaryDir dir;
        Options options;
        options.entryURL = "http://127.0.0.1:1/remote/" + entry;
        QString local = urlOf(dir, "local.mjs");
        auto run = runScript(dir, {
            { entry,
              "import('" + local + "').then(function(ns) { print('must not load: ' + ns.secret); Script.stop(true); },\n"
              "    function(error) { print(error.name + ': ' + error.message); Script.stop(true); });\n" },
            { "local.mjs", "export const secret = 'local';\n" },
        }, options);
        QVERIFY(!run.timedOut);
        QCOMPARE(run.errors, QStringList());
        QCOMPARE(run.printed.length(), 1);
        QVERIFY2(run.printed[0].startsWith("TypeError: Cannot find module '" + local + "' imported from " +
                                           options.entryURL + ":1 (path "),
                 qUtf8Printable(run.printed[0]));
        QVERIFY2(run.printed[0].contains("outside of origin script"), qUtf8Printable(run.printed[0]));
    }
}

void ScriptModuleAsyncTests::testDynamicImportFromRequireModule() {
    // The referrer is the CommonJS module containing the import() call, not the script that called into it
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "classic.js",
          "var lib = Script.require('./sub/lib.js');\n"
          "lib.load().then(function(ns) {\n"
          "    print('required module imported ' + ns.where);\n"
          "    Script.stop(true);\n"
          "}, function(error) {\n"
          "    print('failed: ' + error);\n"
          "    Script.stop(true);\n"
          "});\n" },
        { "sub/lib.js", "module.exports = { load: function() { return import('./m.mjs'); } };\n" },
        { "sub/m.mjs", "export const where = import.meta.url;\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "required module imported " + urlOf(dir, "sub/m.mjs") }));
}

void ScriptModuleAsyncTests::testDeferredEvaluationInNestedLoop() {
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

void ScriptModuleAsyncTests::testDynamicImportMicrotaskOrdering() {
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
