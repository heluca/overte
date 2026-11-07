//
//  ScriptModuleAsyncTests.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_ScriptModuleAsyncTests_h
#define overte_ScriptModuleAsyncTests_h

#include <QtTest/QtTest>

// ES modules: top-level await, dynamic import() and evaluation deferred out of nested event loops
class ScriptModuleAsyncTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testTopLevelAwait();
    void testTopLevelAwaitRejection();
    void testTopLevelAwaitRejectionWithoutAbort();
    void testStalledTopLevelAwait();
    void testTopLevelAwaitRejectionWithoutLocation();
    void testDynamicImportFromModule();
    void testDynamicImportFromClassicScript();
    void testDynamicImportOfLoadedModule();
    void testDynamicImportMissing();
    void testDynamicImportBareSpecifier();
    void testDynamicImportThrowingBody();
    void testDynamicImportTopLevelAwaitRejection();
    void testDynamicImportTopLevelAwaitRejectionWithoutAbort();
    void testDynamicImportRefusals();
    void testDynamicImportRemoteEntryRefusesLocal();
    void testDynamicImportFromRequireModule();
    void testDeferredEvaluationInNestedLoop();
    void testDynamicImportMicrotaskOrdering();
};

#endif // overte_ScriptModuleAsyncTests_h
