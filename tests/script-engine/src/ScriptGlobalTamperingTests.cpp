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
