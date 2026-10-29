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

// A script-owned object a native function returns, as AvatarList.getAvatar returns a ScriptAvatarData (#83)
class OwnershipProbe : public QObject {
    Q_OBJECT
public:
    OwnershipProbe() { ++liveCount; }
    ~OwnershipProbe() { --liveCount; }
    Q_INVOKABLE int value() const { return 1; }

    static int liveCount;
};

// Destroying a script engine that still holds objects its script created (#45), proxies collected after its script
// finished (#50), and script-owned objects freed while it runs (#83)
class ScriptTeardownTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testTeardownWithScriptOwnedObjects();
    void testMethodProxiesCollectedAfterScriptFinished();
    void testScriptOwnedObjectsFreedWhileRunning_data();
    void testScriptOwnedObjectsFreedWhileRunning();
};

#endif // overte_ScriptTeardownTests_h
