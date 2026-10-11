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
#include <QtCore/QTimer>
#include <QtCore/QtEndian>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#include <AccountManager.h>
#include <AddressManager.h>
#include <AudioConstants.h>
#include <AudioInjectorManager.h>
#include <AudioStreamSource.h>
#include <DependencyManager.h>
#include <DomainAccountManager.h>
#include <NodeList.h>
#include <ResourceManager.h>
#include <StatTracker.h>

QTEST_MAIN(AudioStreamTests)

using Frame = AudioStreamSource::Frame;

namespace {

const int SAMPLES_PER_SECOND = AudioConstants::SAMPLE_RATE;
const int FRAME_SAMPLES = AudioConstants::NETWORK_FRAME_SAMPLES_PER_CHANNEL;
// Odd, so the HTTP chunks split samples and the stereo channels of a sample
const int CHUNK_BYTES = 777;

// Never zero, so audio is told apart from the silence played on an underrun
int16_t sampleValue(int index) {
    return (int16_t)(index % 30000 + 1);
}

// Each step sends that many samples per channel, waits minus that many milliseconds, or ends the response at zero
struct Plan {
    int channels { 1 };
    QVector<int> steps;
    bool repeat { false };
};

Plan planFor(const QByteArray& path) {
    Plan plan;
    const int tenth = SAMPLES_PER_SECOND / 10;
    if (path == "/paced") {
        // A second in tenths, a half-second pause, another second, then the end
        for (int part = 0; part < 2; part++) {
            for (int i = 0; i < 10; i++) {
                plan.steps << tenth << -100;
            }
            plan.steps << (part == 0 ? -400 : 0);
        }
    } else if (path == "/burst") {
        plan.steps << SAMPLES_PER_SECOND << -1000 << 0;
    } else if (path == "/stereo") {
        plan.channels = 2;
        plan.steps << tenth << -50 << 0;
    } else if (path == "/forever") {
        plan.steps << tenth << -100;
        plan.repeat = true;
    }
    return plan;
}

struct Connection {
    Plan plan;
    int step { 0 };
    int nextSample { 0 };
};

void runPlan(QTcpSocket* socket, std::shared_ptr<Connection> connection) {
    while (true) {
        if (connection->step == connection->plan.steps.size()) {
            if (!connection->plan.repeat) {
                return;
            }
            connection->step = 0;
        }
        const int step = connection->plan.steps[connection->step++];
        if (step > 0) {
            const int count = step * connection->plan.channels;
            QByteArray pcm(count * AudioConstants::SAMPLE_SIZE, Qt::Uninitialized);
            for (int i = 0; i < count; i++) {
                qToLittleEndian<qint16>(sampleValue(connection->nextSample++), pcm.data() + i * AudioConstants::SAMPLE_SIZE);
            }
            for (int offset = 0; offset < pcm.size(); offset += CHUNK_BYTES) {
                const QByteArray chunk = pcm.mid(offset, CHUNK_BYTES);
                socket->write(QByteArray::number(chunk.size(), 16) + "\r\n" + chunk + "\r\n");
            }
        } else if (step < 0) {
            QTimer::singleShot(-step, socket, [socket, connection] { runPlan(socket, connection); });
            return;
        } else {
            socket->write("0\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
    }
}

// Serves PCM as chunked HTTP responses on 127.0.0.1, from a thread of its own so it never waits on the test
class PcmTestServer {
public:
    PcmTestServer() {
        _context.moveToThread(&_thread);
        _thread.start();
        QMetaObject::invokeMethod(&_context, [this] {
            _server = new QTcpServer();
            _server->listen(QHostAddress::LocalHost, 0);
            _port = _server->serverPort();
            QObject::connect(_server, &QTcpServer::newConnection, _server, [this] {
                while (QTcpSocket* socket = _server->nextPendingConnection()) {
                    serve(socket);
                }
            });
        }, Qt::BlockingQueuedConnection);
    }
    ~PcmTestServer() {
        QMetaObject::invokeMethod(&_context, [this] { delete _server; }, Qt::BlockingQueuedConnection);
        _thread.quit();
        _thread.wait();
    }
    QUrl url(const QString& path) const { return QUrl(QString("http://127.0.0.1:%1%2").arg(_port).arg(path)); }
    int closedConnections() const { return _closed; }

private:
    void serve(QTcpSocket* socket) {
        auto received = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, [this, socket] {
            _closed++;
            socket->deleteLater();
        });
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, received] {
            if (received->contains("\r\n\r\n")) {
                return;
            }
            received->append(socket->readAll());
            if (!received->contains("\r\n\r\n")) {
                return;
            }
            const QByteArray path = received->split(' ').value(1);
            auto connection = std::make_shared<Connection>();
            connection->plan = planFor(path);
            if (connection->plan.steps.isEmpty()) {
                socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 7\r\nConnection: close\r\n\r\nmissing");
                socket->disconnectFromHost();
                return;
            }
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nTransfer-Encoding: chunked\r\n\r\n");
            runPlan(socket, connection);
        });
    }

    QThread _thread;
    QObject _context;
    QTcpServer* _server { nullptr };
    quint16 _port { 0 };
    std::atomic<int> _closed { 0 };
};

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
    server = std::make_unique<PcmTestServer>();
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

    // Every sample arrives once, in order, in whole frames
    QCOMPARE(pulled.frames.last(), Frame::Ended);
    QCOMPARE(pulled.frames.count(Frame::Audio), 2 * SAMPLES_PER_SECOND / FRAME_SAMPLES);
    QCOMPARE(pulled.samples.size(), 2 * SAMPLES_PER_SECOND);
    QVERIFY(isSequenceFrom(pulled.samples, 0));
    // The half-second pause reads as silence and the stream carries on after it
    QVERIFY2(pause >= 30 && pause <= 70, qPrintable(QString::number(pause)));
}

void AudioStreamTests::testOverrunKeepsNewest() {
    auto source = AudioStreamSource::create(server->url("/burst"), AudioConstants::MONO);
    source->start(true, false);
    // The whole second arrives while nothing reads; only the newest 200 ms is kept
    QThread::msleep(300);
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
    std::atomic<bool> finished { false };
    QObject guard;
    QObject::connect(injector.data(), &AudioInjector::finished, &guard, [&finished] { finished = true; }, Qt::DirectConnection);

    QTRY_VERIFY_WITH_TIMEOUT(injector->getLoudness() > 0.0f, 2000);
    QVERIFY(injector->isPlaying());

    manager->stop(injector);
    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(server->closedConnections(), closedBefore + 1, 2000);
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
