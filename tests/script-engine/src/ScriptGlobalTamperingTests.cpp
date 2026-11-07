//
//  ScriptGlobalTamperingTests.cpp
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptGlobalTamperingTests.h"

#include "ScriptModuleTestRunner.h"

QTEST_MAIN(ScriptGlobalTamperingTests)

void ScriptGlobalTamperingTests::initTestCase() {
    initModuleTestDependencies();
}

void ScriptGlobalTamperingTests::testThrowingGlobalGetterBeforeRequire() {
    // The first Script.require copies the enumerable globals for the module's context
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.js",
          "Object.defineProperty(globalThis, 'trap', {\n"
          "    enumerable: true, configurable: true,\n"
          "    get: function() { throw new Error('trap getter'); }\n"
          "});\n"
          "var lib = Script.require('./lib.js');\n"
          "print('loaded ' + lib.value);\n"
          "Script.stop(true);\n" },
        { "lib.js", "module.exports = { value: 42 };\n" },
    });
    QVERIFY(!run.timedOut);
    QVERIFY(!run.uncaughtException);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "loaded 42" }));
}

void ScriptGlobalTamperingTests::testReplacedTypeErrorConstructor() {
    // The engine throws a TypeError of its own constructor, not of whatever the global TypeError has become
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.js",
          "var NativeTypeError = TypeError;\n"
          "globalThis.TypeError = function() { throw new Error('replaced TypeError ran'); };\n"
          "try {\n"
          "    setTimeout(42);\n"
          "    print('no error');\n"
          "} catch (e) {\n"
          "    print([e instanceof NativeTypeError, e.message].join(','));\n"
          "}\n"
          "Script.stop(true);\n" },
    });
    QVERIFY(!run.timedOut);
    QVERIFY(!run.uncaughtException);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "true,setTimeout: callback is not a function" }));
}

void ScriptGlobalTamperingTests::testObjectPrototypeSetterBeforeRequire() {
    // Storing the globals for the module's context must neither hand them to a setter on Object.prototype nor lose them
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.js",
          "var secret = { value: 'mine' };\n"
          "var intercepted = 0;\n"
          "Object.defineProperty(Object.prototype, 'secret', {\n"
          "    configurable: true,\n"
          "    get: function() { intercepted++; return undefined; },\n"
          "    set: function(value) { intercepted++; }\n"
          "});\n"
          "var lib = Script.require('./lib.js');\n"
          "delete Object.prototype.secret;\n"
          "print(lib.value + ' intercepted:' + intercepted);\n"
          "Script.stop(true);\n" },
        { "lib.js", "module.exports = { value: typeof secret === 'object' ? secret.value : 'missing' };\n" },
    });
    QVERIFY(!run.timedOut);
    QVERIFY(!run.uncaughtException);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "mine intercepted:0" }));
}

void ScriptGlobalTamperingTests::testObjectPrototypeThrowingGetterBeforeRequire() {
    // The module's closure object and Script.require inherit the enumerable getter, which the copy must survive
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.js",
          "Object.defineProperty(Object.prototype, 'trap', {\n"
          "    enumerable: true, configurable: true,\n"
          "    get: function() { throw new Error('trap getter'); }\n"
          "});\n"
          "var lib = Script.require('./lib.js');\n"
          "delete Object.prototype.trap;\n"
          "print('loaded ' + lib.value);\n"
          "Script.stop(true);\n" },
        { "lib.js", "module.exports = { value: 42 };\n" },
    });
    QVERIFY(!run.timedOut);
    QVERIFY(!run.uncaughtException);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "loaded 42" }));
}

void ScriptGlobalTamperingTests::testReplacedErrorConstructorIsError() {
    // isError() tests against the context's own Error, not whatever the global Error has become
    auto engine = newScriptEngine();
    auto scopeGuard = engine->getScopeGuard();
    ScriptValue values = engine->evaluate(
        "var values = [new Error('a'), new RangeError('b'), {}];\n"
        "globalThis.Error = function() {};\n"
        "values;\n");
    QVERIFY(!engine->hasUncaughtException());
    QVERIFY(values.property(0u).isError());
    QVERIFY(values.property(1u).isError());
    QVERIFY(!values.property(2u).isError());

    engine->evaluate("delete globalThis.Error;\n");
    QVERIFY(values.property(0u).isError());
    QVERIFY(!engine->hasUncaughtException());
}
