//
//  FetchTests.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_FetchTests_h
#define overte_FetchTests_h

#include <QtTest/QtTest>

#include "ScriptEngine.h"
#include "ScriptManager.h"

// Runs code the way an entity script runs: evaluateInClosure, in a v8::Context of its own with copies of the globals
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


class FetchTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testFetchHeaders();
    void testFetchText();
    void testFetchJson();
    void testFetchPostEcho();
    void testFetch404();
    void testFetch400();
    void testFetchBadJson();
    void testFetchBadResponseHeader();
    void testFetchNetworkError();
    void testFetchAbort();
    void testFetchFile();
    void testFetchFileRefused();
    void testFetchClosureArrayBuffer();

private:
    // Runs the script to its Script.stop(); false if it threw or never stopped. closureSource, if given, is what the
    // script's closure.run() evaluates in a closure.
    bool runFetchScript(const QString& source, const QString& filename, QString& printed, QStringList& errors,
                        bool allowLocalFiles = true, const QString& closureSource = QString());
};

#endif // overte_FetchTests_h
