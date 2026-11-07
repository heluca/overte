//
//  MixedAudioStreamTests.h
//  tests/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_MixedAudioStreamTests_h
#define overte_MixedAudioStreamTests_h

#include <QtTest/QtTest>

class MixedAudioStreamTests : public QObject {
    Q_OBJECT
private slots:
    void testMixedPacketIsOneFrame();
    void testSilentPacketLeavesTwentyMilliseconds();
    void testSequenceGapAddsConcealedFrames();
    void testReadIgnoresJitterBuffer();
};

#endif // overte_MixedAudioStreamTests_h
