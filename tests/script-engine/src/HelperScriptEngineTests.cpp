//
//  HelperScriptEngineTests.cpp
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "HelperScriptEngineTests.h"

#include "HelperScriptEngine.h"
#include "ScriptValue.h"

QTEST_MAIN(HelperScriptEngineTests)

// Every engine keeps its V8 isolate until the process exits (overte-org/overte#687), so an owner that never converts
// anything must not create one: an Agent assignment builds two owners and the assignment-client runs them back to back.
void HelperScriptEngineTests::testNoEngineUntilFirstRun() {
    HelperScriptEngine helper;
    QCOMPARE(helper.get(), nullptr);
}

void HelperScriptEngineTests::testRunsShareOneEngine() {
    HelperScriptEngine helper;

    ScriptEngine* firstEngine = nullptr;
    helper.run([&] {
        firstEngine = helper.get();
        QVERIFY(firstEngine);
        ScriptValue object = firstEngine->newObject();
        object.setProperty("answer", 42);
        QCOMPARE(object.property("answer").toInt32(), 42);
    });

    ScriptEngine* secondEngine = helper.runWithResult<ScriptEngine*>([&] {
        return helper.get();
    });
    QCOMPARE(secondEngine, firstEngine);
}
