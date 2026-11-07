//
//  ScriptStringTests.cpp
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptStringTests.h"

#include <functional>

#include "DependencyManager.h"
#include "ScriptCache.h"
#include "ScriptEngine.h"
#include "ScriptEngineCast.h"
#include "ScriptEngines.h"
#include "ScriptException.h"
#include "ScriptManager.h"
#include "StatTracker.h"
#include "v8/FastScriptValueUtils.h"

QTEST_MAIN(ScriptStringTests)

using ScriptManagerPointer = std::shared_ptr<ScriptManager>;

static QString runAndPrint(StringProbe& probe, const QString& source, const QString& fileName,
                           std::function<void(ScriptEngine*)> beforeRun = nullptr) {
    ScriptManagerPointer sm = newScriptManager(ScriptManager::NETWORKLESS_TEST_SCRIPT, source, fileName);
    sm->setAbortOnUncaughtException(true);
    QString printed;
    QObject::connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString&) {
        printed.append(message);
    });
    auto scopeGuard = sm->engine()->getScopeGuard();
    sm->engine()->registerGlobalObject(scopeGuard.get(), "strings", &probe);
    sm->engine()->globalObject().setProperty("fromCpp", sm->engine()->newValue(probe.nulText));
    if (beforeRun) {
        beforeRun(sm->engine().get());
    }
    sm->run();
    if (sm->getUncaughtException()) {
        return "exception: " + sm->getUncaughtException()->errorMessage;
    }
    return printed;
}

void ScriptStringTests::initTestCase() {
    DependencyManager::set<ScriptEngines>(ScriptManager::NETWORKLESS_TEST_SCRIPT, QUrl(""));
    DependencyManager::set<ScriptCache>();
    DependencyManager::set<StatTracker>();
    DependencyManager::set<ScriptInitializers>();
}

void ScriptStringTests::testScriptStringWithNulRoundTrip() {
    QString script =
        "var text = 'a\\u0000b\\u0000c';\n"
        "var echoed = strings.echo(text);\n"
        "print([echoed.length, echoed === text, strings.variantLength(text), strings.valueLength(text)].join(','));\n"
        "Script.stop(true);\n";

    StringProbe probe;
    QCOMPARE(runAndPrint(probe, script, "testScriptStringWithNulRoundTrip.js"), QString("5,true,5,5"));
    QCOMPARE(probe.lengths, QList<int>({ 5 }));
}

void ScriptStringTests::testCppStringWithNulReachesScript() {
    QString script =
        "var returned = strings.withNul();\n"
        "print([fromCpp.length, fromCpp.charCodeAt(1), fromCpp.charAt(2), returned.length, returned === fromCpp].join(','));\n"
        "Script.stop(true);\n";

    StringProbe probe;
    probe.nulText = QString("x") + QChar(0) + QString("y");
    QCOMPARE(probe.nulText.length(), 3);
    QCOMPARE(runAndPrint(probe, script, "testCppStringWithNulReachesScript.js"), QString("3,0,y,3,true"));
}

void ScriptStringTests::testCppByteArrayWithNulReachesScript() {
    // Without a QByteArray marshaller a QByteArray reaches script as a string. This test links libentities, whose static
    // initializer registers the marshaller for every engine wherever the library is loaded (always on macOS, where
    // linked dylibs are kept), and then it arrives as an ArrayBuffer. Either way the NUL byte must survive.
    QString script =
        "var bytes = strings.bytesWithNul();\n"
        "if (bytes instanceof ArrayBuffer) { bytes = String.fromCharCode.apply(null, new Uint8Array(bytes)); }\n"
        "print([typeof bytes, bytes.length, bytes.charCodeAt(1), bytes.charAt(2)].join(','));\n"
        "Script.stop(true);\n";

    StringProbe probe;
    QCOMPARE(runAndPrint(probe, script, "testCppByteArrayWithNulReachesScript.js"), QString("string,3,0,y"));
}

void ScriptStringTests::testArrayBufferRoundTrip() {
    // An ArrayBuffer through the QByteArray marshaller, as registered for entity scripts, and back. A buffer over the
    // QByteArray limit (2 GiB) is rejected with a RangeError, which is not tested here for want of the memory.
    QString script =
        "var copy = strings.echoBytes(new Uint8Array([1, 0, 255, 7]).buffer);\n"
        "print([copy instanceof ArrayBuffer, copy.byteLength, Array.from(new Uint8Array(copy)).join(' ')].join(','));\n"
        "Script.stop(true);\n";

    StringProbe probe;
    QString printed = runAndPrint(probe, script, "testArrayBufferRoundTrip.js", [](ScriptEngine* engine) {
        scriptRegisterMetaType<QByteArray, qBytearrayToScriptValue, qBytearrayFromScriptValue>(engine, "QByteArray");
    });
    QCOMPARE(printed, QString("true,4,1 0 255 7"));
    QCOMPARE(probe.lengths, QList<int>({ 4 }));
}
