//
//  ScriptModuleEntityTests.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_ScriptModuleEntityTests_h
#define overte_ScriptModuleEntityTests_h

#include <QtCore/QObject>
#include <QtTest/QtTest>

// ES module entity scripts and serverScripts (#11): loading, construction, shared instances and entity methods. Epochs,
// reload and unload are in ScriptModuleEntityReloadTests.
class ScriptModuleEntityTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testDefaultExportClass();
    void testDefaultExportNotConstructor();
    void testLoadFailureReported();
    void testTopLevelAwaitKeepsLoading();
    void testSharedInstance();
    void testRemotelyCallableClassField();
    void testDynamicImportAfterAwait();
    void testRejectedAsyncMethodReported();
    void testTimerAfterAwaitClearedOnUnload();
    void testClassicEntityUnchanged();
    void testFailedModuleLeavesClassicEntityRunning();
    void testConstructorThrows();
    void testPreloadThrows();
};

#endif // overte_ScriptModuleEntityTests_h
