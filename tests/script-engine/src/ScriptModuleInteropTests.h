//
//  ScriptModuleInteropTests.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_ScriptModuleInteropTests_h
#define overte_ScriptModuleInteropTests_h

#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtTest/QtTest>

class ScriptManager;

// Exposed to test scripts as Probe
class InteropProbe : public QObject {
    Q_OBJECT
public:
    explicit InteropProbe(ScriptManager* manager) : _manager(manager) {}

    // Test-only: the current environment's ScriptEnvironment::loadedURLs(), sorted; empty without an environment
    Q_INVOKABLE QStringList loadedURLs() const;
    // ScriptManager::currentSandboxURL()
    Q_INVOKABLE QString sandboxURL() const;
    Q_INVOKABLE void emitPing() { emit ping(); }

signals:
    void ping();

private:
    ScriptManager* _manager;
};

// ES modules: CommonJS and JSON files imported as synthetic modules, and Script.include/Script.require of .mjs refused
class ScriptModuleInteropTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testStaticCommonJsImport();
    void testNamedImportFromCommonJs();
    void testDynamicCommonJsImport();
    void testRequireCacheIdentity();
    void testCommonJsRequiresSibling();
    void testCommonJsThrows();
    void testDynamicCommonJsFailures();
    void testStaticCommonJsSyntaxError();
    void testStaticCommonJsThrowsValue();
    void testThrowSiteInRequiredFile();
    void testFailedImportThenRequire();
    void testImportFromCommonJsBody();
    void testRequireSyntaxErrorKeepsUncaughtReporting();
    void testJsonImport();
    void testJsonImportWithoutAttribute();
    void testInvalidJson();
    void testJsonAttributeOnCommonJs();
    void testUnsupportedAttributes();
    void testIncludeOfModuleRefused();
    void testRequireOfModuleRefused();
    void testEnvironmentRecordsCommonJs();
    void testCommonJsImportDoesNotBlock();
    void testStopDuringCommonJsEvaluation();
};

#endif // overte_ScriptModuleInteropTests_h
