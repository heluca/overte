//
//  AvatarVoiceStreamTests.h
//  tests/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_AvatarVoiceStreamTests_h
#define overte_AvatarVoiceStreamTests_h

#include <QtTest/QtTest>

class AvatarVoiceStreamTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void testRejectsNonHttpUrls();
    void testPrebufferHoldsBackFirstAudio();
    void testUnderrunPrimesAgain();
    void testIdleTimeoutEnds();
    void testZerosAreNotIdle();
    void testOneFlushPerAudibleToSilent();
    void testEndReasons();
    void testFramesDueCatchesUp();
};

#endif  // overte_AvatarVoiceStreamTests_h
