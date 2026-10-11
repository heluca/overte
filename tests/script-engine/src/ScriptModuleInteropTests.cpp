//
//  ScriptModuleInteropTests.cpp
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptModuleInteropTests.h"

#include <algorithm>
#include <memory>

#include "HeldScriptServer.h"
#include "ScriptEnvironment.h"
#include "ScriptModuleTestRunner.h"

QTEST_MAIN(ScriptModuleInteropTests)

using Options = ModuleTestOptions;

QStringList InteropProbe::loadedURLs() const {
    auto environment = _manager->engine()->currentEnvironment();
    QStringList urls = environment ? QStringList(environment->loadedURLs().values()) : QStringList();
    std::sort(urls.begin(), urls.end());
    return urls;
}

QString InteropProbe::sandboxURL() const {
    return _manager->currentSandboxURL().toString();
}

static Options withProbe(std::unique_ptr<InteropProbe>& probe, Options options = Options()) {
    options.beforeRun = [&probe](ScriptManager* manager) {
        probe = std::make_unique<InteropProbe>(manager);
        manager->engine()->globalObject().setProperty("Probe", manager->engine()->newQObject(probe.get()));
    };
    return options;
}

void ScriptModuleInteropTests::initTestCase() {
    initModuleTestDependencies();
}

void ScriptModuleInteropTests::testStaticCommonJsImport() {
    // The CommonJS file evaluates in graph order, before the importer's body, and is the namespace's only export
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "import lib from './lib.js';\n"
          "import * as ns from './lib.js';\n"
          "print('main ' + lib.greet('module') + ' exports=' + Object.keys(ns).join(',') + ' same=' + (ns.default === lib));\n"
          "Script.stop(true);\n" },
        { "lib.js",
          "print('lib evaluating ' + __filename.endsWith('/lib.js'));\n"
          "module.exports = { greet: function(name) { return 'hello ' + name; } };\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "lib evaluating true", "main hello module exports=default same=true" }));
}

void ScriptModuleInteropTests::testNamedImportFromCommonJs() {
    // No named exports: linking fails before anything runs ("const { greet } = lib;" is the way)
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs", "import { greet } from './lib.js';\nprint('must not run');\n" },
        { "lib.js", "print('must not run either');\nmodule.exports = { greet: function() {} };\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList());
    QCOMPARE(run.errors.length(), 1);
    QVERIFY2(run.errors[0].startsWith("Error while linking module " + urlOf(dir, "main.mjs") + ":1: SyntaxError: "),
             qUtf8Printable(run.errors[0]));
    QVERIFY2(run.errors[0].contains("does not provide an export named 'greet'"), qUtf8Printable(run.errors[0]));
}

void ScriptModuleInteropTests::testDynamicCommonJsImport() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "const ns = await import('./sub/lib.js');\n"
          "const again = await import('./sub/lib.js');\n"
          "print(typeof ns.default + ' ' + ns.default() + ' same=' + (again === ns));\n"
          "Script.stop(true);\n" },
        { "sub/lib.js", "print('lib evaluating');\nmodule.exports = function() { return 'called'; };\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "lib evaluating", "function called same=true" }));
}

void ScriptModuleInteropTests::testRequireCacheIdentity() {
    // Script.require and import share Script.require.cache: the same exports whichever comes first, evaluated once
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "import first from './first.js';\n"
          "print('import then require ' + (Script.require('./first.js') === first));\n"
          "const required = Script.require('./second.js');\n"
          "const ns = await import('./second.js');\n"
          "print('require then import ' + (ns.default === required));\n"
          "const entry = Script.require.cache[import.meta.url.replace('main.mjs', 'first.js')];\n"
          "print('cache entry loaded=' + entry.loaded + ' exports=' + (entry.exports === first));\n"
          "Script.stop(true);\n" },
        { "first.js", "print('first evaluating');\nmodule.exports = { name: 'first' };\n" },
        { "second.js", "print('second evaluating');\nmodule.exports = { name: 'second' };\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "first evaluating", "import then require true", "second evaluating",
                                        "require then import true", "cache entry loaded=true exports=true" }));
}

void ScriptModuleInteropTests::testCommonJsRequiresSibling() {
    // require() inside an imported CommonJS file resolves against that file, as for Script.require
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "import lib from './sub/lib.js';\n"
          "print(lib.text);\n"
          "Script.stop(true);\n" },
        { "sub/lib.js", "var helper = require('./helper.js');\nmodule.exports = { text: 'lib with ' + helper.name };\n" },
        { "sub/helper.js", "module.exports = { name: 'helper' };\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "lib with helper" }));
}

void ScriptModuleInteropTests::testCommonJsThrows() {
    // Reported once, at the CommonJS file's line, as a module body that throws; nothing after it in the graph runs
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs", "import './bad.js';\nprint('must not run');\n" },
        { "bad.js", "print('bad before');\nthrow new Error('cjs boom');\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList({ "bad before" }));
    QCOMPARE(run.errors, QStringList({ "Error while evaluating module " + urlOf(dir, "bad.js") + ":2: Error: cjs boom" }));
    QVERIFY(run.uncaughtException);
    QCOMPARE(run.uncaughtException->errorLine, 2);
}

void ScriptModuleInteropTests::testDynamicCommonJsFailures() {
    // import() rejects: with the thrown Error itself, or, for a thrown non-Error and a syntax error, with an error naming
    // the file and line. The failed file is not left in Script.require.cache.
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "for (const name of ['throws.js', 'value.js', 'syntax.js']) {\n"
          "    try {\n"
          "        await import('./' + name);\n"
          "        print('must not resolve');\n"
          "    } catch (error) {\n"
          "        print(name + ' ' + error.name + ': ' + error.message.replace(import.meta.url.replace('main.mjs', ''), '')\n"
          "              + (error.stack.includes('throws.js:2') ? ' (stack names throws.js:2)' : ''));\n"
          "    }\n"
          "}\n"
          "print('cache ' + Script.require.cache[import.meta.url.replace('main.mjs', 'throws.js')]);\n"
          "Script.stop(true);\n" },
        { "throws.js", "\nthrow new Error('thrown');\n" },
        { "value.js", "\n\nthrow 'plain';\n" },
        { "syntax.js", "module.exports = ;\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed.length(), 4);
    QCOMPARE(run.printed[0], QString("throws.js Error: thrown (stack names throws.js:2)"));
    QCOMPARE(run.printed[1], QString("value.js Error: CommonJS module value.js:3 threw plain"));
    QVERIFY2(run.printed[2].startsWith("syntax.js SyntaxError: Error while compiling CommonJS module syntax.js:1: "),
             qUtf8Printable(run.printed[2]));
    QCOMPARE(run.printed[3], QString("cache null"));
}

void ScriptModuleInteropTests::testJsonImport() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "import data from './data.json' with { type: 'json' };\n"
          "const ns = await import('./data.json', { with: { type: 'json' } });\n"
          "print(data.name + ' ' + data.list.length + ' same=' + (ns.default === data) + ' exports='\n"
          "      + Object.keys(ns).join(','));\n"
          "Script.stop(true);\n" },
        { "data.json", "{ \"name\": \"data\", \"list\": [1, 2, 3] }\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "data 3 same=true exports=default" }));
}

void ScriptModuleInteropTests::testJsonImportWithoutAttribute() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs", "import data from './data.json';\nprint('must not run');\n" },
        { "data.json", "{}\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList());
    QCOMPARE(run.errors, QStringList({ "Cannot import './data.json' from " + urlOf(dir, "main.mjs") +
                                       ":1: a .json file can only be imported with { type: 'json' }" }));
}

void ScriptModuleInteropTests::testInvalidJson() {
    // Like a syntax error in a module: the graph fails before anything runs, at the JSON file
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs", "print('must not run');\nimport data from './bad.json' with { type: 'json' };\n" },
        { "bad.json", "{ \"a\": }\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList());
    QCOMPARE(run.errors.length(), 1);
    QVERIFY2(run.errors[0].startsWith("Error while parsing JSON module " + urlOf(dir, "bad.json") + ": SyntaxError"),
             qUtf8Printable(run.errors[0]));
}

void ScriptModuleInteropTests::testJsonAttributeOnCommonJs() {
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs", "\nimport lib from './lib.js' with { type: 'json' };\nprint('must not run');\n" },
        { "lib.js", "module.exports = 1;\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList());
    QCOMPARE(run.errors, QStringList({ "Cannot import './lib.js' from " + urlOf(dir, "main.mjs") +
                                       ":2: with { type: 'json' } is only for .json files" }));
}

void ScriptModuleInteropTests::testUnsupportedAttributes() {
    // An unknown key or type is refused rather than ignored, statically and by import()
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs", "import data from './data.json' with { type: 'json', mode: 'strict' };\n" },
        { "data.json", "{}\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList({ "Cannot import './data.json' from " + urlOf(dir, "main.mjs") +
                                       ":1: unsupported import attribute 'mode'; only type: 'json' is supported" }));

    QTemporaryDir dynamicDir;
    auto dynamic = runScript(dynamicDir, {
        { "main.mjs",
          "import('./style.css', { with: { type: 'css' } }).catch(function(error) {\n"
          "    print(error.name + ': ' + error.message);\n"
          "    Script.stop(true);\n"
          "});\n" },
    });
    QVERIFY(!dynamic.timedOut);
    QCOMPARE(dynamic.errors, QStringList());
    QCOMPARE(dynamic.printed, QStringList({ "TypeError: Cannot import './style.css' from " + urlOf(dynamicDir, "main.mjs") +
                                            ":1: unsupported import type 'css'; only type: 'json' is supported" }));
}

void ScriptModuleInteropTests::testIncludeOfModuleRefused() {
    // Both forms, with and without a callback, throw before fetching anything: a.js, listed first, is not evaluated
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "classic.js",
          "var attempts = [\n"
          "    function() { Script.include('./m.mjs'); },\n"
          "    function() { Script.include('./m.mjs?v=2', function() { print('must not call back'); }); },\n"
          "    function() { Script.include(['./a.js', './m.mjs']); },\n"
          "];\n"
          "attempts.forEach(function(attempt) {\n"
          "    try {\n"
          "        attempt();\n"
          "        print('must not include');\n"
          "    } catch (error) {\n"
          "        print(error.name + ': ' + error.message);\n"
          "    }\n"
          "});\n"
          "Script.setTimeout(function() { print('a.js evaluated: ' + (typeof aEvaluated)); Script.stop(true); }, 50);\n" },
        { "m.mjs", "print('must not run');\n" },
        { "a.js", "var aEvaluated = true;\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QString module = urlOf(dir, "m.mjs");
    QString refusal = "TypeError: Script.include cannot load an ES module (%1); use import()";
    QCOMPARE(run.printed, QStringList({ refusal.arg(module), refusal.arg(module + "?v=2"), refusal.arg(module),
                                        "a.js evaluated: undefined" }));
}

void ScriptModuleInteropTests::testRequireOfModuleRefused() {
    // From classic code and from module code (where import() is the way)
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "classic.js",
          "try {\n"
          "    Script.require('./m.mjs');\n"
          "} catch (error) {\n"
          "    print(error.name + ': ' + error.message);\n"
          "}\n"
          "import('./main.mjs');\n" },
        { "main.mjs",
          "try {\n"
          "    Script.require('./m.mjs');\n"
          "} catch (error) {\n"
          "    print('module ' + error.name);\n"
          "}\n"
          "Script.stop(true);\n" },
        { "m.mjs", "print('must not run');\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "TypeError: Script.require cannot load an ES module (" + urlOf(dir, "m.mjs") +
                                        "); use import()", "module TypeError" }));
}

void ScriptModuleInteropTests::testEnvironmentRecordsCommonJs() {
    // The CommonJS and JSON files a graph imports count towards its environment's loaded URLs, and the CommonJS file
    // runs in that environment. What it require()s does not count (design question Q3 of #10, off).
    std::unique_ptr<InteropProbe> probe;
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "import lib from './lib.js';\n"
          "import data from './data.json' with { type: 'json' };\n"
          "print('lib sandbox ' + lib.sandbox);\n"
          "print('loaded ' + Probe.loadedURLs().join(' '));\n"
          "Script.stop(true);\n" },
        { "lib.js", "var helper = require('./helper.js');\nmodule.exports = { sandbox: Probe.sandboxURL() };\n" },
        { "helper.js", "module.exports = {};\n" },
        { "data.json", "{}\n" },
    }, withProbe(probe));
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "lib sandbox " + urlOf(dir, "main.mjs"),
                                        "loaded " + QStringList({ urlOf(dir, "data.json"), urlOf(dir, "lib.js"),
                                                                  urlOf(dir, "main.mjs") }).join(' ') }));
}

void ScriptModuleInteropTests::testCommonJsImportDoesNotBlock() {
    // The CommonJS file is fetched with the graph, not by a nested event loop while module code is on the stack: the
    // script's timer fires while the server holds the response, and the microtask it queues runs straight after it,
    // before the file's body. Fetched from inside the evaluation, the microtask would wait until the body had run.
    HeldScriptServer server("print('cjs body');\nmodule.exports = { value: 'held' };\n");
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "const pending = import('" + server.url() + "');\n"
          "Script.setTimeout(function() {\n"
          "    print('timer while held');\n"
          "    Promise.resolve().then(function() { print('timer microtask'); });\n"
          "}, 0);\n"
          "const ns = await pending;\n"
          "print('imported ' + ns.default.value);\n"
          "Script.stop(true);\n" },
    });
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "timer while held", "timer microtask", "cjs body", "imported held" }));
}

void ScriptModuleInteropTests::testStopDuringCommonJsEvaluation() {
    // The script stops while the imported CommonJS file waits in a require() of an unreachable file. The require()
    // then throws, which fails the graph after the stop: nothing is reported, and the loader is gone.
    QTemporaryDir dir;
    Options options;
    options.stopAfterMs = 50;
    options.waitAfterRunMs = 200;
    int loadersLeft = -1;
    options.afterRun = [&loadersLeft](ScriptManager* manager) {
        auto children = manager->engine()->findChildren<QObject*>(QString(), Qt::FindDirectChildrenOnly);
        loadersLeft = static_cast<int>(std::count_if(children.begin(), children.end(), [](QObject* child) {
            return QString(child->metaObject()->className()) == "ScriptModuleLoaderV8";
        }));
    };
    auto run = runScript(dir, {
        { "main.mjs", "import lib from './outer.js';\nprint('must not run');\n" },
        { "outer.js", "print('outer before require');\nrequire('http://127.0.0.1:1/slow.js');\nprint('must not run');\n" },
    }, options);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.printed, QStringList({ "outer before require" }));
    QCOMPARE(run.errors, QStringList());
    QVERIFY(!run.uncaughtException);
    QCOMPARE(loadersLeft, 0);
}
