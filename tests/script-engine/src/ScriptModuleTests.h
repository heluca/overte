//
//  ScriptModuleTests.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_ScriptModuleTests_h
#define overte_ScriptModuleTests_h

#include <memory>

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "ScriptException.h"

class ScriptModuleTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testResolveModuleSpecifier();
    void testStaticImport();
    void testImportChain();
    void testDiamond();
    void testCycle();
    void testImportMetaUrl();
    void testMissingModule();
    void testUnreachableModule();
    void testBareSpecifier();
    void testJsImport();
    void testDependencySyntaxError();
    void testUnresolvableCycle();
    void testEvaluationError();
    void testClassicScriptUnchanged();
    void testMicrotaskOrdering();

private:
    struct Run {
        QStringList printed;
        QStringList errors;
        bool timedOut { false };
        std::shared_ptr<ScriptException> uncaughtException;
    };

    // Writes the files (path relative to the directory -> contents) and runs the first one as a top-level script
    // until it stops. Module scripts are served from the directory through ScriptCache as file: URLs.
    Run runScript(const QTemporaryDir& dir, const QList<QPair<QString, QString>>& files);
    static QString urlOf(const QTemporaryDir& dir, const QString& path);
};

#endif // overte_ScriptModuleTests_h
