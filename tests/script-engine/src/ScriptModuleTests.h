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

#include <QtTest/QtTest>

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
    void testLoadFailureStopsWithoutAbort();
    void testEvaluationErrorKeepsRunningWithoutAbort();
    void testStopDuringFetch();
    void testRemoteEntryRefusesLocalImport();
    void testClassicScriptUnchanged();
    void testMicrotaskOrdering();
};

#endif // overte_ScriptModuleTests_h
