//
//  AudioStreamSinkTests.h
//  tests/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_AudioStreamSinkTests_h
#define overte_AudioStreamSinkTests_h

#include <QtTest/QtTest>

class AudioStreamSinkTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void testRejectsBadArguments();
    void testFramesArriveInOrder();
    void testStalledServerDropsOldest();
    void testAuthorizationOnlyWithCredential();
    void testRefusedHandshakeReportsStatus();
    void testStopClosesConnection();
    void testStopDuringStallDoesNotBlock();
    void testDestroyDuringCloseAborts();
    void testServerCloseFinishesOnce();
    void testDestroyClosesWithoutFinished();
};

#endif // overte_AudioStreamSinkTests_h
