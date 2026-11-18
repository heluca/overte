//
//  HelperScriptEngineTests.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_HelperScriptEngineTests_h
#define overte_HelperScriptEngineTests_h

#include <QtTest/QtTest>

class HelperScriptEngineTests : public QObject {
    Q_OBJECT
private slots:
    void testNoEngineUntilFirstRun();
    void testRunsShareOneEngine();
};

#endif // overte_HelperScriptEngineTests_h
