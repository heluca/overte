//
//  AudioStreamTests.h
//  tests/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_AudioStreamTests_h
#define overte_AudioStreamTests_h

#include <QtTest/QtTest>

class AudioStreamTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void testPacedStreamWithPause();
    void testOverrunKeepsNewest();
    void testStereoFrames();
    void testInjectorPlaysUntilEnd();
    void testStopClosesConnection();
    void testErrorResponseFinishes();
};

#endif // overte_AudioStreamTests_h
