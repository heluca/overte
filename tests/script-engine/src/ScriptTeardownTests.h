//
//  ScriptTeardownTests.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_ScriptTeardownTests_h
#define overte_ScriptTeardownTests_h

#include <QtTest/QtTest>

// Destroying a script engine that still holds objects its script created (#45), and proxies collected after its script
// finished (#50)
class ScriptTeardownTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testTeardownWithScriptOwnedObjects();
    void testMethodProxiesCollectedAfterScriptFinished();
};

#endif // overte_ScriptTeardownTests_h
