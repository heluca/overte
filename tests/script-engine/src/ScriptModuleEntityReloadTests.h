//
//  ScriptModuleEntityReloadTests.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_ScriptModuleEntityReloadTests_h
#define overte_ScriptModuleEntityReloadTests_h

#include <QtCore/QObject>
#include <QtTest/QtTest>

// ES module entity scripts (#11): module map epochs, reload, CommonJS across reloads, and unload
class ScriptModuleEntityReloadTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testReloadNewEpoch();
    void testUnloadReleasesEpochAndReportsStalledAwait();
    void testReloadedTogetherShareEpoch();
    void testJoiningReloadFetchesAnew();
    void testCommonJsCacheIdentity();
    void testFailureKeepsSharedEpoch();
    void testUnloadWhileOtherWaitsInTopLevelAwait();
};

#endif // overte_ScriptModuleEntityReloadTests_h
