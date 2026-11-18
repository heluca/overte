//
//  RecordingFrameHandlerTests.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_RecordingFrameHandlerTests_h
#define overte_RecordingFrameHandlerTests_h

#include <QtTest/QtTest>

// An Agent clears its recording playback frame handlers on its own thread when its script ends, while the Deck may be
// calling one on the main thread (#81). Here because tests/recording is not yet a testcase and this directory links
// the recording library.
class RecordingFrameHandlerTests : public QObject {
    Q_OBJECT
private slots:
    void testClearWaitsForRunningCall();
    void testHandlerClearsItsOwnType();
};

#endif // overte_RecordingFrameHandlerTests_h
