//
//  ScriptCredentialsTests.h
//  tests/networking/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_ScriptCredentialsTests_h
#define overte_ScriptCredentialsTests_h

#include <QtTest/QtTest>

class ScriptCredentialsTests : public QObject {
    Q_OBJECT
private slots:
    void testUrlPrefix();
    void testSegmentBoundary();
    void testLoopbackHttpOnly();
    void testScriptScope();
    void testLongestMatch();
    void testIgnoredRows();
    void testReload();
};

#endif // overte_ScriptCredentialsTests_h
