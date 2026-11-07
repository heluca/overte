//
//  AudioStreamTests.cpp
//  tests/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "AudioStreamTests.h"

#include <atomic>
#include <memory>

#include <QtCore/QElapsedTimer>
#include <QtCore/QThread>

#include <AccountManager.h>
#include <AddressManager.h>
#include <AudioConstants.h>
#include <AudioInjectorLocalBuffer.h>
#include <AudioInjectorManager.h>
#include <AudioStreamSource.h>
#include <DependencyManager.h>
#include <DomainAccountManager.h>
#include <NodeList.h>
#include <ResourceManager.h>
#include <StatTracker.h>

#include "PcmTestServer.h"

QTEST_MAIN(AudioStreamTests)

using Frame = AudioStreamSource::Frame;

namespace {

using namespace pcmtest;

const int SAMPLES_PER_SECOND = AudioConstants::SAMPLE_RATE;
const int FRAME_SAMPLES = AudioConstants::NETWORK_FRAME_SAMPLES_PER_CHANNEL;

Plan planFor(const QByteArray& path) {
    Plan plan;
    const int tenth = SAMPLES_PER_SECOND / 10;
    if (path == "/paced") {
        // A second in tenths, a half-second pause, another second, then the end
        for (int part = 0; part < 2; part++) {
            for (int i = 0; i < 10; i++) {
                plan.steps << Step::audio(tenth) << Step::wait(100);
            }
            plan.steps << (part == 0 ? Step::wait(400) : Step::end());
        }
    } else if (path == "/burst") {
        plan.steps << Step::audio(SAMPLES_PER_SECOND) << Step::wait(2000) << Step::end();
        plan.signalsFlush = true;
    } else if (path == "/stereo") {
        plan.channels = 2;
        plan.steps << Step::audio(tenth) << Step::wait(50) << Step::end();
    } else if (path == "/forever") {
        plan.steps << Step::audio(tenth) << Step::wait(100);
        plan.repeat = true;
    } else if (path == "/lone") {
        // One frame, then three more after a second
        plan.steps << Step::audio(FRAME_SAMPLES) << Step::wait(1000) << Step::audio(3 * FRAME_SAMPLES) << Step::wait(300)
                   << Step::end();
    }
    return plan;
}

std::unique_ptr<PcmTestServer> server;

struct Pulled {
    QVector<Frame> frames;
    QVector<int16_t> samples;
};

// Reads a frame every 10 ms, as the injector's frame timer does, until the stream ends or the timeout passes
Pulled pullFrames(AudioStreamSource& source, int timeoutMs) {
    Pulled pulled;
    QByteArray frame(source.getFrameBytes(), '\0');
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeoutMs) {
        while (pulled.frames.size() <= clock.elapsed() / 10) {
            const Frame kind = source.readFrame(AudioStreamSource::Local, frame.data());
            pulled.frames.append(kind);
            if (kind == Frame::Ended) {
                return pulled;
            }
            if (kind == Frame::Audio) {
                auto samples = reinterpret_cast<const int16_t*>(frame.constData());
                for (int i = 0; i < frame.size() / AudioConstants::SAMPLE_SIZE; i++) {
                    pulled.samples.append(samples[i]);
                }
            }
        }
        QThread::msleep(1);
    }
    return pulled;
}

// The samples a sequence skips (audio dropped on an overrun), or -1 when it repeats or goes back
int droppedSamples(const QVector<int16_t>& samples, int firstIndex) {
    int expected = firstIndex;
    int dropped = 0;
    for (int16_t sample : samples) {
        const int skip = (sample - sampleValue(expected) + 30000) % 30000;
        if (skip > SAMPLES_PER_SECOND / 10) {
            qWarning() << "sample" << sample << "after index" << expected - 1;
            return -1;
        }
        dropped += skip;
        expected += skip + 1;
    }
    return dropped;
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

int longestSilenceBetweenAudio(const QVector<Frame>& frames) {
    const int first = frames.indexOf(Frame::Audio);
    const int last = frames.lastIndexOf(Frame::Audio);
    int longest = 0;
    int run = 0;
    for (int i = first; i >= 0 && i <= last; i++) {
        run = (frames[i] == Frame::Silence) ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    return longest;
}

AudioInjectorOptions positionedOptions() {
    AudioInjectorOptions options;
    options.position = glm::vec3(1.0f, 2.0f, 3.0f);
    return options;
}

}  // namespace

void AudioStreamTests::initTestCase() {
    DependencyManager::set<StatTracker>();
    DependencyManager::registerInheritance<LimitedNodeList, NodeList>();
    DependencyManager::set<AccountManager>(true);
    DependencyManager::set<DomainAccountManager>();
    DependencyManager::set<AddressManager>();
    DependencyManager::set<NodeList>(NodeType::Agent, 0);
    DependencyManager::set<ResourceManager>(false);
    DependencyManager::set<AudioInjectorManager>();
    server = std::make_unique<PcmTestServer>(planFor);
}

void AudioStreamTests::cleanupTestCase() {
    DependencyManager::destroy<AudioInjectorManager>();
    server.reset();
    DependencyManager::get<ResourceManager>()->cleanup();
    DependencyManager::destroy<ResourceManager>();
}

void AudioStreamTests::testPacedStreamWithPause() {
    auto source = AudioStreamSource::create(server->url("/paced"), AudioConstants::MONO);
    source->start(true, false);
    const Pulled pulled = pullFrames(*source, 8000);

    const int pause = longestSilenceBetweenAudio(pulled.frames);
    qDebug() << "frames" << pulled.frames.size() << "audio" << pulled.frames.count(Frame::Audio) << "silence"
             << pulled.frames.count(Frame::Silence) << "longest silence" << pause;

    // Every sample arrives once, in order, in whole frames. A stall of either thread can overrun the ring, which drops
    // the oldest audio, so a few frames may go missing but the newest audio is always kept.
    QCOMPARE(pulled.frames.last(), Frame::Ended);
    QVector<int16_t> audio = pulled.samples;
    while (!audio.isEmpty() && audio.last() == 0) {
        audio.removeLast();  // the silence that pads a last frame left partial by a drop
    }
    const int dropped = droppedSamples(audio, 0);
    qDebug() << "dropped samples" << dropped;
    QVERIFY2(dropped >= 0 && dropped <= 5 * FRAME_SAMPLES, qPrintable(QString::number(dropped)));
    QCOMPARE(audio.size() + dropped, 2 * SAMPLES_PER_SECOND);
    QCOMPARE(audio.last(), sampleValue(2 * SAMPLES_PER_SECOND - 1));
    // The half-second pause reads as silence and the stream carries on after it
    QVERIFY2(pause >= 30 && pause <= 70, qPrintable(QString::number(pause)));
}

void AudioStreamTests::testOverrunKeepsNewest() {
    auto source = AudioStreamSource::create(server->url("/burst"), AudioConstants::MONO);
    const int flushedBefore = server->burstsFlushed();
    source->start(true, false);
    // The whole second arrives while nothing reads; only the newest 200 ms is kept. Once the server has handed it to
    // the OS, loopback delivery to the reply is a matter of milliseconds.
    QTRY_VERIFY_WITH_TIMEOUT(server->burstsFlushed() > flushedBefore, 5000);
    QThread::msleep(200);
    const Pulled pulled = pullFrames(*source, 5000);

    qDebug() << "audio" << pulled.frames.count(Frame::Audio) << "silence" << pulled.frames.count(Frame::Silence);
    QCOMPARE(pulled.frames.count(Frame::Audio), 20);
    QVERIFY(isSequenceFrom(pulled.samples, SAMPLES_PER_SECOND - 20 * FRAME_SAMPLES));
    QVERIFY(pulled.frames.count(Frame::Silence) > 0);
    QCOMPARE(pulled.frames.last(), Frame::Ended);
}

void AudioStreamTests::testStereoFrames() {
    auto source = AudioStreamSource::create(server->url("/stereo"), AudioConstants::STEREO);
    QCOMPARE(source->getFrameBytes(), AudioConstants::NETWORK_FRAME_BYTES_STEREO);
    source->start(true, false);
    const Pulled pulled = pullFrames(*source, 3000);

    QCOMPARE(pulled.frames.count(Frame::Audio), 10);
    QCOMPARE(pulled.samples.size(), 2 * SAMPLES_PER_SECOND / 10);
    QVERIFY(isSequenceFrom(pulled.samples, 0));
    QCOMPARE(pulled.frames.last(), Frame::Ended);
}

void AudioStreamTests::testInjectorPlaysUntilEnd() {
    QElapsedTimer clock;
    clock.start();
    std::atomic<qint64> finishedAt { -1 };
    auto injector = DependencyManager::get<AudioInjectorManager>()->playStream(server->url("/paced"), positionedOptions());
    QVERIFY(injector);
    QObject guard;
    QObject::connect(injector.data(), &AudioInjector::finished, &guard, [&finishedAt, &clock] {
        finishedAt = clock.elapsed();
    }, Qt::DirectConnection);

    // The injector's loudness is that of the last frame it sent: audio, then the silence of the pause while it keeps
    // playing, then audio again
    bool heardAudio = false;
    qint64 silentSince = -1;
    qint64 longestSilence = 0;
    while (finishedAt < 0 && clock.elapsed() < 8000) {
        const bool playing = injector->isPlaying();
        if (injector->getLoudness() > 0.0f) {
            heardAudio = true;
            silentSince = -1;
        } else if (heardAudio && playing) {
            if (silentSince < 0) {
                silentSince = clock.elapsed();
            }
            longestSilence = std::max(longestSilence, clock.elapsed() - silentSince);
        }
        QThread::msleep(5);
    }

    qDebug() << "finished at" << finishedAt.load() << "ms, longest silence while playing" << longestSilence << "ms";
    QVERIFY(heardAudio);
    QVERIFY2(longestSilence >= 300, qPrintable(QString::number(longestSilence)));
    // Two seconds of audio and the half-second pause play in real time, then the end of the response finishes it
    QVERIFY2(finishedAt >= 2300 && finishedAt < 4500, qPrintable(QString::number(finishedAt)));
    QVERIFY(!injector->isPlaying());
}

void AudioStreamTests::testStopClosesConnection() {
    const int closedBefore = server->closedConnections();
    auto manager = DependencyManager::get<AudioInjectorManager>();
    auto injector = manager->playStream(server->url("/forever"), positionedOptions());
    QVERIFY(injector);
    std::atomic<int> finishes { 0 };
    QObject guard;
    QObject::connect(injector.data(), &AudioInjector::finished, &guard, [&finishes] { finishes++; }, Qt::DirectConnection);

    QTRY_VERIFY_WITH_TIMEOUT(injector->getLoudness() > 0.0f, 2000);
    QVERIFY(injector->isPlaying());

    manager->stop(injector);
    QTRY_COMPARE_WITH_TIMEOUT(finishes.load(), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(server->closedConnections(), closedBefore + 1, 2000);

    // Interface's AudioClient finishes the local injection again once the stop has cleared the local buffer; a script
    // that reconnects from finished must hear it once
    injector->finishLocalInjection();
    manager->stop(injector);
    QTest::qWait(300);
    QCOMPARE(finishes.load(), 1);
}

void AudioStreamTests::testRejectsNonHttpUrls() {
    QVERIFY(!AudioStreamSource::create(QUrl::fromLocalFile("/etc/hostname"), AudioConstants::MONO));
    QVERIFY(!AudioStreamSource::create(QUrl("/etc/hostname"), AudioConstants::MONO));
    QVERIFY(!AudioStreamSource::create(QUrl("qrc:///sounds/x.raw"), AudioConstants::MONO));
    QVERIFY(!DependencyManager::get<AudioInjectorManager>()->playStream(QUrl("file:///etc/hostname"), positionedOptions()));
}

void AudioStreamTests::testLocalBufferReadsWholeFrames() {
    auto source = AudioStreamSource::create(server->url("/stereo"), AudioConstants::STEREO);
    source->start(true, false);
    AudioInjectorLocalBuffer buffer(source);
    const qint64 twoFrames = 2 * source->getFrameBytes();
    QByteArray data(twoFrames + source->getFrameBytes() / 2, '\0');

    // Whole frames, silence while the stream has nothing, and nothing at all once it has ended
    QVector<int16_t> audio;
    QElapsedTimer clock;
    clock.start();
    qint64 bytesRead = -1;
    while (bytesRead != 0 && clock.elapsed() < 3000) {
        bytesRead = buffer.readData(data.data(), data.size());
        QVERIFY2(bytesRead == 0 || bytesRead == twoFrames, qPrintable(QString::number(bytesRead)));
        auto samples = reinterpret_cast<const int16_t*>(data.constData());
        for (int i = 0; i < bytesRead / AudioConstants::SAMPLE_SIZE; i++) {
            if (samples[i] != 0) {
                audio.append(samples[i]);
            }
        }
        QThread::msleep(20);
    }
    QCOMPARE(bytesRead, qint64(0));
    QCOMPARE(audio.size(), 2 * SAMPLES_PER_SECOND / 10);
    QVERIFY(isSequenceFrom(audio, 0));
}

void AudioStreamTests::testErrorResponseFinishes() {
    auto injector = DependencyManager::get<AudioInjectorManager>()->playStream(server->url("/missing"), positionedOptions());
    QVERIFY(injector);
    std::atomic<bool> finished { false };
    QObject guard;
    QObject::connect(injector.data(), &AudioInjector::finished, &guard, [&finished] { finished = true; }, Qt::DirectConnection);

    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 2000);
    // The error page is not played
    QCOMPARE(injector->getLoudness(), 0.0f);
}

void AudioStreamTests::testPrebufferDefaultsToZero() {
    // By default a lone frame plays as soon as it is whole, as Audio.playStream always has
    auto source = AudioStreamSource::create(server->url("/lone"), AudioConstants::MONO);
    source->start(true, false);
    Pulled pulled = pullFrames(*source, 3000);
    int firstAudio = pulled.frames.indexOf(Frame::Audio);
    qDebug() << "default: first audio at frame" << firstAudio << "audio" << pulled.frames.count(Frame::Audio);
    QCOMPARE(pulled.frames.count(Frame::Audio), 4);
    QVERIFY2(firstAudio >= 0 && firstAudio < 50, qPrintable(QString::number(firstAudio)));
    QVERIFY(isSequenceFrom(pulled.samples, 0));
    QCOMPARE(pulled.frames.last(), Frame::Ended);

    // With a prebuffer of two frames the lone frame waits for the next ones, and nothing is lost
    source = AudioStreamSource::create(server->url("/lone"), AudioConstants::MONO);
    source->setPrebufferFrames(2);
    source->start(true, false);
    pulled = pullFrames(*source, 3000);
    firstAudio = pulled.frames.indexOf(Frame::Audio);
    qDebug() << "prebuffer 2: first audio at frame" << firstAudio << "audio" << pulled.frames.count(Frame::Audio);
    QCOMPARE(pulled.frames.count(Frame::Audio), 4);
    QVERIFY2(firstAudio >= 90, qPrintable(QString::number(firstAudio)));
    QVERIFY(isSequenceFrom(pulled.samples, 0));
    QCOMPARE(pulled.frames.last(), Frame::Ended);
    QCOMPARE(source->getEndReason(), QString("ended"));
}
