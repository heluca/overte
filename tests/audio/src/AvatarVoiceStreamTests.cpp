//
//  AvatarVoiceStreamTests.cpp
//  tests/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "AvatarVoiceStreamTests.h"

#include <memory>

#include <QtCore/QElapsedTimer>
#include <QtCore/QThread>
#include <QtNetwork/QTcpServer>

#include <AccountManager.h>
#include <AddressManager.h>
#include <AudioConstants.h>
#include <AvatarVoiceStream.h>
#include <DependencyManager.h>
#include <DomainAccountManager.h>
#include <NodeList.h>
#include <ResourceManager.h>
#include <StatTracker.h>

#include "PcmTestServer.h"

QTEST_MAIN(AvatarVoiceStreamTests)

using Frame = AvatarVoiceStream::Frame;

namespace {

using namespace pcmtest;

const int FRAME_SAMPLES = AudioConstants::NETWORK_FRAME_SAMPLES_PER_CHANNEL;

Plan planFor(const QByteArray& path) {
    Plan plan;
    if (path == "/prebuffer") {
        // A lone frame, then ten more a second later
        plan.steps << Step::audio(FRAME_SAMPLES) << Step::wait(1000) << Step::audio(10 * FRAME_SAMPLES) << Step::wait(300)
                   << Step::end();
    } else if (path == "/underrun") {
        // Ten frames, a gap, a lone frame, a gap, ten frames
        plan.steps << Step::audio(10 * FRAME_SAMPLES) << Step::wait(500) << Step::audio(FRAME_SAMPLES) << Step::wait(500)
                   << Step::audio(10 * FRAME_SAMPLES) << Step::wait(300) << Step::end();
    } else if (path == "/hold") {
        // Five frames, then the connection stays open with nothing on it
        plan.steps << Step::audio(5 * FRAME_SAMPLES) << Step::wait(60 * 1000) << Step::end();
    } else if (path == "/zeros") {
        // A producer with nothing to say, sending zeros in real time
        plan.steps << Step::zeros(5 * FRAME_SAMPLES) << Step::wait(50);
        plan.repeat = true;
    } else if (path == "/onoff") {
        // Speech, silence, speech, silence, all at once: 20 frames, the ring's whole capacity
        plan.steps << Step::audio(4 * FRAME_SAMPLES) << Step::zeros(6 * FRAME_SAMPLES) << Step::audio(4 * FRAME_SAMPLES)
                   << Step::zeros(6 * FRAME_SAMPLES) << Step::wait(100) << Step::end();
    } else if (path == "/denied") {
        plan.status = 401;
        plan.steps << Step::end();
    } else if (path == "/short") {
        plan.steps << Step::audio(3 * FRAME_SAMPLES) << Step::end();
    }
    return plan;
}

std::unique_ptr<PcmTestServer> server;

struct Pulled {
    QVector<Frame> frames;
    QVector<int16_t> samples;  // of the Audio frames
};

// Reads a frame every 10 ms, as the send loop does, until the stream ends or the timeout passes
Pulled pullFrames(AvatarVoiceStream& stream, int timeoutMs) {
    Pulled pulled;
    int16_t frame[FRAME_SAMPLES];
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeoutMs) {
        while (pulled.frames.size() <= clock.elapsed() / 10) {
            const Frame kind = stream.nextFrame(frame);
            pulled.frames.append(kind);
            if (kind == Frame::Ended) {
                return pulled;
            }
            if (kind == Frame::Audio) {
                for (int i = 0; i < FRAME_SAMPLES; i++) {
                    pulled.samples.append(frame[i]);
                }
            } else {
                for (int i = 0; i < FRAME_SAMPLES; i++) {
                    if (frame[i] != 0) {
                        qWarning() << "a" << (int)kind << "frame has sample" << i << "=" << frame[i];
                        pulled.frames.append(Frame::Audio);  // fails the counts below
                        return pulled;
                    }
                }
            }
        }
        QThread::msleep(1);
    }
    return pulled;
}

bool isSequenceFrom(const QVector<int16_t>& samples, int firstIndex) {
    for (int i = 0; i < samples.size(); i++) {
        if (samples[i] != sampleValue(firstIndex + i)) {
            qWarning() << "sample" << i << "is" << samples[i] << "not" << sampleValue(firstIndex + i);
            return false;
        }
    }
    return true;
}

// The lengths of the runs of Audio frames
QVector<int> audioRuns(const QVector<Frame>& frames) {
    QVector<int> runs;
    int run = 0;
    for (Frame frame : frames) {
        if (frame == Frame::Audio) {
            run++;
        } else if (run > 0) {
            runs << run;
            run = 0;
        }
    }
    if (run > 0) {
        runs << run;
    }
    return runs;
}

QString describe(const QVector<Frame>& frames) {
    static const char LETTERS[] = { 'A', '.', 'F', 'E' };
    QString text;
    for (Frame frame : frames) {
        text += LETTERS[(int)frame];
    }
    return text;
}

}  // namespace

void AvatarVoiceStreamTests::initTestCase() {
    DependencyManager::set<StatTracker>();
    DependencyManager::registerInheritance<LimitedNodeList, NodeList>();
    DependencyManager::set<AccountManager>(true);
    DependencyManager::set<DomainAccountManager>();
    DependencyManager::set<AddressManager>();
    DependencyManager::set<NodeList>(NodeType::Agent, 0);
    DependencyManager::set<ResourceManager>(false);
    server = std::make_unique<PcmTestServer>(planFor);
}

void AvatarVoiceStreamTests::cleanupTestCase() {
    server.reset();
    DependencyManager::get<ResourceManager>()->cleanup();
    DependencyManager::destroy<ResourceManager>();
}

void AvatarVoiceStreamTests::testRejectsNonHttpUrls() {
    QVERIFY(!AvatarVoiceStream::create(QUrl::fromLocalFile("/etc/hostname"), 2, 1000));
    QVERIFY(!AvatarVoiceStream::create(QUrl("ws://127.0.0.1:1/voice"), 2, 1000));
}

void AvatarVoiceStreamTests::testPrebufferHoldsBackFirstAudio() {
    auto stream = AvatarVoiceStream::create(server->url("/prebuffer"), 2, 0);
    QVERIFY(stream);
    const Pulled pulled = pullFrames(*stream, 4000);
    const int firstAudio = pulled.frames.indexOf(Frame::Audio);
    qDebug() << "first audio at frame" << firstAudio << describe(pulled.frames).left(140);

    // The lone frame waits for the next ones a second later, then all eleven play in order
    QVERIFY2(firstAudio >= 90, qPrintable(QString::number(firstAudio)));
    QCOMPARE(pulled.frames.count(Frame::Audio), 11);
    QCOMPARE(audioRuns(pulled.frames), QVector<int>({ 11 }));
    QVERIFY(isSequenceFrom(pulled.samples, 0));
    QCOMPARE(pulled.frames.last(), Frame::Ended);
    QCOMPARE(stream->getEndReason(), QString("ended"));
}

void AvatarVoiceStreamTests::testUnderrunPrimesAgain() {
    auto stream = AvatarVoiceStream::create(server->url("/underrun"), 2, 0);
    const Pulled pulled = pullFrames(*stream, 4000);
    const QVector<int> runs = audioRuns(pulled.frames);
    qDebug() << "audio runs" << runs << describe(pulled.frames);

    // After the first underrun the lone frame is held until the next ten arrive, so it never plays alone
    QCOMPARE(pulled.frames.count(Frame::Audio), 21);
    QCOMPARE(runs, QVector<int>({ 10, 11 }));
    QVERIFY(isSequenceFrom(pulled.samples, 0));
    QCOMPARE(pulled.frames.last(), Frame::Ended);
}

void AvatarVoiceStreamTests::testIdleTimeoutEnds() {
    auto stream = AvatarVoiceStream::create(server->url("/hold"), 2, 50);
    const Pulled pulled = pullFrames(*stream, 5000);
    const int lastAudio = pulled.frames.lastIndexOf(Frame::Audio);
    qDebug() << "ended" << pulled.frames.size() - 1 - lastAudio << "frames after the last audio:" << describe(pulled.frames);

    QCOMPARE(pulled.frames.count(Frame::Audio), 5);
    QCOMPARE(pulled.frames.last(), Frame::Ended);
    QCOMPARE(stream->getEndReason(), QString("idle"));
    // 50 underruns in a row: the first is the Flush and the 50th ends the stream
    QCOMPARE(pulled.frames.size() - 1 - lastAudio, 50);
    int16_t frame[FRAME_SAMPLES];
    QCOMPARE(stream->nextFrame(frame), Frame::Ended);

    // An idle timeout of 0 never ends the stream
    stream = AvatarVoiceStream::create(server->url("/hold"), 2, 0);
    const Pulled neverIdle = pullFrames(*stream, 1500);
    QCOMPARE(neverIdle.frames.count(Frame::Ended), 0);
    QCOMPARE(neverIdle.frames.count(Frame::Audio), 5);
}

void AvatarVoiceStreamTests::testZerosAreNotIdle() {
    // 20 frames of underruns would end it; frames of zeros never do
    auto stream = AvatarVoiceStream::create(server->url("/zeros"), 0, 20);
    const Pulled pulled = pullFrames(*stream, 1500);
    qDebug() << describe(pulled.frames);
    QVERIFY(pulled.frames.size() >= 140);
    QCOMPARE(pulled.frames.count(Frame::Ended), 0);
    QCOMPARE(pulled.frames.count(Frame::Audio), 0);
    QCOMPARE(pulled.frames.count(Frame::Flush), 0);
}

void AvatarVoiceStreamTests::testOneFlushPerAudibleToSilent() {
    auto stream = AvatarVoiceStream::create(server->url("/onoff"), 2, 0);
    const Pulled pulled = pullFrames(*stream, 3000);
    const QString text = describe(pulled.frames);
    qDebug() << text;

    // Exactly one Flush after each run of audio, whatever follows it, and none anywhere else
    QCOMPARE(pulled.frames.count(Frame::Audio), 8);
    QCOMPARE(audioRuns(pulled.frames), QVector<int>({ 4, 4 }));
    QCOMPARE(pulled.frames.count(Frame::Flush), 2);
    for (int i = 0; i < pulled.frames.size(); i++) {
        if (pulled.frames[i] == Frame::Flush) {
            QVERIFY2(i > 0 && pulled.frames[i - 1] == Frame::Audio, qPrintable(text));
        }
    }
    QVERIFY(isSequenceFrom(pulled.samples, 0));
    QCOMPARE(pulled.frames.last(), Frame::Ended);
    QCOMPARE(stream->getEndReason(), QString("ended"));
}

void AvatarVoiceStreamTests::testEndReasons() {
    auto denied = AvatarVoiceStream::create(server->url("/denied"), 2, 0);
    QCOMPARE(pullFrames(*denied, 3000).frames.last(), Frame::Ended);
    QCOMPARE(denied->getEndReason(), QString("http 401"));

    auto missing = AvatarVoiceStream::create(server->url("/missing"), 2, 0);
    QCOMPARE(pullFrames(*missing, 3000).frames.last(), Frame::Ended);
    QCOMPARE(missing->getEndReason(), QString("http 404"));

    // The rest of a stream shorter than the prebuffer still plays
    auto shortStream = AvatarVoiceStream::create(server->url("/short"), 5, 0);
    const Pulled pulled = pullFrames(*shortStream, 3000);
    QCOMPARE(pulled.frames.count(Frame::Audio), 3);
    QCOMPARE(shortStream->getEndReason(), QString("ended"));

    quint16 closedPort;
    {
        QTcpServer probe;
        probe.listen(QHostAddress::LocalHost, 0);
        closedPort = probe.serverPort();
    }
    auto refused = AvatarVoiceStream::create(QUrl(QString("http://127.0.0.1:%1/voice").arg(closedPort)), 2, 0);
    QCOMPARE(pullFrames(*refused, 3000).frames.last(), Frame::Ended);
    qDebug() << "refused:" << refused->getEndReason();
    QVERIFY2(refused->getEndReason().startsWith("error: "), qPrintable(refused->getEndReason()));
}

void AvatarVoiceStreamTests::testFramesDueCatchesUp() {
    auto stream = AvatarVoiceStream::create(server->url("/zeros"), 0, 0);
    const quint64 FRAME = AudioConstants::NETWORK_FRAME_USECS;
    const quint64 t0 = 1000 * 1000 * 1000;
    QCOMPARE(stream->framesDue(t0), 1);
    QCOMPARE(stream->framesDue(t0 + FRAME), 1);
    // A tick a little early still sends its frame, and the next on time sends one
    QCOMPARE(stream->framesDue(t0 + 2 * FRAME - 2000), 1);
    QCOMPARE(stream->framesDue(t0 + 3 * FRAME), 1);
    // A 50 ms stall: five frames due, four now and the last on the next tick with its own
    QCOMPARE(stream->framesDue(t0 + 8 * FRAME), 4);
    QCOMPARE(stream->framesDue(t0 + 9 * FRAME), 2);
    QCOMPARE(stream->framesDue(t0 + 10 * FRAME), 1);

    // A stall longer than the ring makes up only the ring's 200 ms, four frames a tick
    const quint64 t1 = t0 + 10 * 1000 * 1000;
    int sent = 0;
    for (int tick = 0; tick <= 10; tick++) {
        const int due = stream->framesDue(t1 + tick * FRAME);
        QVERIFY(due >= 0 && due <= AvatarVoiceStream::MAX_FRAMES_PER_TICK);
        sent += due;
    }
    QCOMPARE(sent, 20 + 11);
}
