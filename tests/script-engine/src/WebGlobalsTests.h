//
//  WebGlobalsTests.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_WebGlobalsTests_h
#define overte_WebGlobalsTests_h

#include <QtTest/QtTest>

#include "ScriptEngine.h"
#include "ScriptManager.h"

// Runs code the way Script.require() runs a module: evaluateInClosure, in a v8::Context of its own with copies of the
// globals. Also the registered QObject that structuredClone must refuse.
class ClosureRunner : public QObject {
    Q_OBJECT

    public:
        ClosureRunner(ScriptEngine* engine, const QString& source) : _engine(engine), _source(source) {}

        Q_INVOKABLE void run() {
            _engine->evaluateInClosure(_engine->newObject(), _engine->newProgram(_source, "closure.js"));
        }

    private:
        ScriptEngine* _engine;
        QString _source;
};


class WebGlobalsTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testPresentEverywhere();
    void testTextEncoder();
    void testTextDecoder();
    void testBase64();
    void testRandom();
    void testDigest();
    void testStructuredClone();
    void testStructuredCloneOverteObjects();
    void testPerformanceNow();
    void testDomException();
    void testUrlSearchParams();
    void testUrlTable();
    void testUrlParts();
    void testReplacedBuiltins();
    void testClosureRealm();

private:
    // Runs the script to its Script.stop(); false if it threw or never stopped. closureSource is what the script's
    // closure.run() evaluates in a closure.
    bool runScript(const QString& source, QStringList& printed, const QString& closureSource = QString());
};

#endif // overte_WebGlobalsTests_h
