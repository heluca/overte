//
//  RecordingFrameHandlerTests.cpp
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "RecordingFrameHandlerTests.h"

#include <atomic>
#include <memory>
#include <thread>

#include <QtCore/QSemaphore>
#include <QtCore/QThread>

#include <recording/Frame.h>

using namespace recording;

QTEST_MAIN(RecordingFrameHandlerTests)

void RecordingFrameHandlerTests::testClearWaitsForRunningCall() {
    const FrameType type = Frame::registerFrameType("com.overte.recording.test.ClearWaits");
    QSemaphore handlerEntered;
    QSemaphore releaseHandler;
    std::atomic<bool> handlerReturned { false };
    auto captured = std::make_shared<int>(0);
    std::weak_ptr<int> capturedWeak = captured;
    Frame::registerFrameHandler(type, [captured, &handlerEntered, &releaseHandler, &handlerReturned](Frame::ConstPointer) {
        handlerEntered.release();
        releaseHandler.acquire();
        handlerReturned = true;
    });
    captured.reset();

    std::thread playback([type] {
        Frame::handleFrame(std::make_shared<Frame>(type, 0.0f, QByteArray()));
    });
    handlerEntered.acquire();

    std::atomic<bool> cleared { false };
    bool handlerReturnedWhenCleared = false;
    bool captureReleasedWhenCleared = false;
    std::thread teardown([&] {
        Frame::clearFrameHandler(type);
        handlerReturnedWhenCleared = handlerReturned;
        captureReleasedWhenCleared = capturedWeak.expired();
        cleared = true;
    });

    // Without the wait, clearFrameHandler returns at once; with it, never before the handler is released
    QThread::msleep(100);
    const bool clearedWhileRunning = cleared;

    releaseHandler.release();
    teardown.join();
    playback.join();
    QVERIFY2(!clearedWhileRunning, "clearFrameHandler returned while the handler was still running");
    QVERIFY(handlerReturnedWhenCleared);
    QVERIFY(captureReleasedWhenCleared);
}

void RecordingFrameHandlerTests::testHandlerClearsItsOwnType() {
    const FrameType type = Frame::registerFrameType("com.overte.recording.test.ClearsItself");
    int calls = 0;
    Frame::registerFrameHandler(type, [type, &calls](Frame::ConstPointer) {
        ++calls;
        Frame::clearFrameHandler(type);
    });

    Frame::handleFrame(std::make_shared<Frame>(type, 0.0f, QByteArray()));
    Frame::handleFrame(std::make_shared<Frame>(type, 0.0f, QByteArray()));
    QCOMPARE(calls, 1);
}
