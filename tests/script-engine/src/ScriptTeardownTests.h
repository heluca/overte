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

// A native object whose signal a script connects to, and that the script makes emit it
class SignalSource : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE void fire(int value) { emit ping(value); }
signals:
    void ping(int value);
};

// Destroying a script engine that still holds objects its script created (#45), proxies collected after its script
// finished (#50), script-owned objects freed while it runs (#83), and a signal proxy collected between a disconnect and a
// reconnect (#84)
class ScriptTeardownTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testTeardownWithScriptOwnedObjects();
    void testMethodProxiesCollectedAfterScriptFinished();
    void testScriptOwnedObjectsFreedWhileRunning_data();
    void testScriptOwnedObjectsFreedWhileRunning();
    void testSignalReconnectAfterCollection();
};

#endif // overte_ScriptTeardownTests_h
