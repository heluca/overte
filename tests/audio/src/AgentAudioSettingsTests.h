//
//  AgentAudioSettingsTests.h
//  tests/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_AgentAudioSettingsTests_h
#define overte_AgentAudioSettingsTests_h

#include <QtTest/QtTest>

class AgentAudioSettingsTests : public QObject {
    Q_OBJECT
private slots:
    void testOfferedSubset();
    void testOfferedDropsUnknown();
    void testOfferedFallsBackToAvailable();
    void testOfferedDuplicates();
};

#endif // overte_AgentAudioSettingsTests_h
