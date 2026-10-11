//
//  ScriptGlobalTamperingTests.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_ScriptGlobalTamperingTests_h
#define overte_ScriptGlobalTamperingTests_h

#include <QtTest/QtTest>

// The engine's native paths with globals a script has redefined
class ScriptGlobalTamperingTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testThrowingGlobalGetterBeforeRequire();
};

#endif // overte_ScriptGlobalTamperingTests_h
