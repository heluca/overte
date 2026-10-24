//
//  AudioStreamSinkTests.cpp
//  tests/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "AudioStreamSinkTests.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <QtCore/QElapsedTimer>
#include <QtCore/QPointer>
#include <QtCore/QThread>
#include <QtCore/QtEndian>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtWebSockets/QWebSocket>
#include <QtWebSockets/QWebSocketServer>

#include <AudioConstants.h>
#include <AudioStreamSink.h>

QTEST_MAIN(AudioStreamSinkTests)

namespace {

const int FRAME_SAMPLES = AudioConstants::NETWORK_FRAME_SAMPLES_STEREO;
const int FRAME_BYTES = AudioConstants::NETWORK_FRAME_BYTES_STEREO;

// Never zero, so a frame is told apart from silence
int16_t tagFor(int index) {
    return (int16_t)(index % 30000 + 1);
}

// A WebSocket server on 127.0.0.1 that keeps every binary message, and a plain TCP server that refuses the upgrade with
// 401. Both run on a thread of their own, so a stalled server never stalls the test.
class WsTestServer {
public:
    WsTestServer() {
        _context.moveToThread(&_thread);
        _thread.start();
        QMetaObject::invokeMethod(&_context, [this] {
            _server = new QWebSocketServer("AudioStreamSinkTests", QWebSocketServer::NonSecureMode);
            _server->listen(QHostAddress::LocalHost, 0);
            _port = _server->serverPort();
            QObject::connect(_server, &QWebSocketServer::newConnection, _server, [this] {
                while (QWebSocket* socket = _server->nextPendingConnection()) {
                    accept(socket);
                }
            });
            _refusing = new QTcpServer();
            _refusing->listen(QHostAddress::LocalHost, 0);
            _refusingPort = _refusing->serverPort();
            QObject::connect(_refusing, &QTcpServer::newConnection, _refusing, [this] {
                while (QTcpSocket* socket = _refusing->nextPendingConnection()) {
                    refuse(socket);
                }
            });
        }, Qt::BlockingQueuedConnection);
    }
    ~WsTestServer() {
        QMetaObject::invokeMethod(&_context, [this] {
            delete _server;
            delete _refusing;
        }, Qt::BlockingQueuedConnection);
        _thread.quit();
        _thread.wait();
    }

    QUrl url(const QString& path) const { return QUrl(QString("ws://127.0.0.1:%1%2").arg(_port).arg(path)); }
    QUrl refusingUrl() const { return QUrl(QString("ws://127.0.0.1:%1/ears").arg(_refusingPort)); }

    // Blocks the server's thread until release(), so it reads nothing
    void hold() {
        _held = true;
        QMetaObject::invokeMethod(&_context, [this] {
            while (_held) {
                QThread::msleep(1);
            }
        }, Qt::QueuedConnection);
    }
    void release() { _held = false; }
    void closeClients() {
        QMetaObject::invokeMethod(&_context, [this] {
            for (auto& socket : _sockets) {
                if (socket) {
                    socket->close();
                }
            }
        }, Qt::BlockingQueuedConnection);
    }

    std::vector<QByteArray> takeMessages() {
        std::lock_guard<std::mutex> lock(_mutex);
        std::vector<QByteArray> messages;
        messages.swap(_messages);
        return messages;
    }
    // The Authorization header of each upgrade request, or "(none)"
    QList<QByteArray> authorizations() {
        std::lock_guard<std::mutex> lock(_mutex);
        return _authorizations;
    }
    int disconnections() const { return _disconnections; }

private:
    void accept(QWebSocket* socket) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            const QNetworkRequest request = socket->request();
            _authorizations.append(request.hasRawHeader("Authorization") ? request.rawHeader("Authorization")
                                                                         : QByteArray("(none)"));
        }
        _sockets.append(socket);
        QObject::connect(socket, &QWebSocket::binaryMessageReceived, socket, [this](const QByteArray& message) {
            std::lock_guard<std::mutex> lock(_mutex);
            _messages.push_back(message);
        });
        QObject::connect(socket, &QWebSocket::disconnected, socket, [this, socket] {
            _disconnections++;
            socket->deleteLater();
        });
    }
    void refuse(QTcpSocket* socket) {
        auto received = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, received] {
            received->append(socket->readAll());
            if (received->contains("\r\n\r\n")) {
                socket->write("HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
            }
        });
    }

    QThread _thread;
    QObject _context;
    QWebSocketServer* _server { nullptr };
    QTcpServer* _refusing { nullptr };
    quint16 _port { 0 };
    quint16 _refusingPort { 0 };
    QList<QPointer<QWebSocket>> _sockets;
    std::atomic<int> _disconnections { 0 };
    std::atomic<bool> _held { false };

    std::mutex _mutex;
    std::vector<QByteArray> _messages;
    QList<QByteArray> _authorizations;
};

std::unique_ptr<WsTestServer> server;

void writeTaggedFrame(AudioStreamSink& sink, int index) {
    std::vector<int16_t> frame(FRAME_SAMPLES, tagFor(index));
    sink.writeFrame(frame.data(), FRAME_SAMPLES);
}

// The frame indices in arrival order, or -1 for a frame whose samples differ, which is a torn or misaligned frame
QVector<int> frameIndices(const std::vector<QByteArray>& messages, int firstIndex = 0) {
    QVector<int> indices;
    int base = firstIndex - firstIndex % 30000;
    for (const QByteArray& message : messages) {
        for (int offset = 0; offset + FRAME_BYTES <= message.size(); offset += FRAME_BYTES) {
            const auto samples = reinterpret_cast<const int16_t*>(message.constData() + offset);
            const int16_t tag = qFromLittleEndian<int16_t>(samples[0]);
            bool same = true;
            for (int i = 1; i < FRAME_SAMPLES; i++) {
                same = same && qFromLittleEndian<int16_t>(samples[i]) == tag;
            }
            int index = base + tag - 1;
            if (!indices.isEmpty() && indices.last() >= 0 && index < indices.last() - 15000) {
                base += 30000;
                index += 30000;
            }
            indices.append(same ? index : -1);
        }
    }
    return indices;
}

struct Finished : public QObject {
    QStringList reasons;
    void watch(AudioStreamSink* sink) {
        connect(sink, &AudioStreamSink::finished, this, [this](const QString& reason) { reasons.append(reason); });
    }
};

void waitUntil(QElapsedTimer& clock, qint64 msec) {
    while (clock.elapsed() < msec) {
        QThread::usleep(200);
    }
}

}  // namespace

void AudioStreamSinkTests::initTestCase() {
    server = std::make_unique<WsTestServer>();
}

void AudioStreamSinkTests::cleanupTestCase() {
    server.reset();
}

void AudioStreamSinkTests::testRejectsBadArguments() {
    QVERIFY(!AudioStreamSink::create(QUrl("http://127.0.0.1:1/ears"), AudioConstants::STEREO));
    QVERIFY(!AudioStreamSink::create(QUrl("https://127.0.0.1:1/ears"), AudioConstants::STEREO));
    QVERIFY(!AudioStreamSink::create(QUrl("file:///etc/passwd"), AudioConstants::STEREO));
    QVERIFY(!AudioStreamSink::create(QUrl(), AudioConstants::STEREO));
    QVERIFY(!AudioStreamSink::create(server->url("/ears"), AudioConstants::STEREO, 0));
    QVERIFY(!AudioStreamSink::create(server->url("/ears"), AudioConstants::STEREO, AudioStreamSink::MAX_FRAMES_PER_MESSAGE + 1));
    QVERIFY(AudioStreamSink::create(server->url("/ears"), AudioConstants::STEREO, AudioStreamSink::MAX_FRAMES_PER_MESSAGE));
    QVERIFY(AudioStreamSink::create(QUrl("wss://127.0.0.1:1/ears"), AudioConstants::STEREO));
}

void AudioStreamSinkTests::testFramesArriveInOrder() {
    auto sink = AudioStreamSink::create(server->url("/ears"), AudioConstants::STEREO, 2);
    QCOMPARE(sink->getFrameSamples(), FRAME_SAMPLES);
    sink->start();
    QTRY_VERIFY_WITH_TIMEOUT(sink->isOpen(), 5000);
    server->takeMessages();

    // Paced like the mixer, one frame every 10 ms, with a silent frame in the middle; 200 frames make 100 messages
    const int FRAMES = 199;
    const int SILENT_AT = 100;
    QElapsedTimer clock;
    clock.start();
    for (int i = 0; i < FRAMES; i++) {
        if (i == SILENT_AT) {
            sink->writeSilenceFrame();
        }
        writeTaggedFrame(*sink, i);
        waitUntil(clock, (i + 1) * 10);
    }

    std::vector<QByteArray> messages;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (auto& message : server->takeMessages()) {
            messages.push_back(message);
        }
        return (int)messages.size() * 2 >= FRAMES + 1;
    }(), 3000);
    QTest::qWait(100);
    for (auto& message : server->takeMessages()) {
        messages.push_back(message);
    }

    QCOMPARE((int)messages.size(), (FRAMES + 1) / 2);
    for (const auto& message : messages) {
        QCOMPARE(message.size(), 2 * FRAME_BYTES);
    }
    QCOMPARE(messages[SILENT_AT / 2].mid(SILENT_AT % 2 * FRAME_BYTES, FRAME_BYTES), QByteArray(FRAME_BYTES, '\0'));
    QVector<int> indices = frameIndices(messages);
    QCOMPARE(indices.size(), FRAMES + 1);
    indices.removeAt(SILENT_AT);  // all zeros, read as index -1
    for (int i = 0; i < FRAMES; i++) {
        QCOMPARE(indices.at(i), i);
    }
    QCOMPARE(sink->getFramesSent(), (quint64)FRAMES + 1);
    QCOMPARE(sink->getFramesDropped(), (quint64)0);
}

void AudioStreamSinkTests::testStalledServerDropsOldest() {
    auto sink = AudioStreamSink::create(server->url("/ears"), AudioConstants::STEREO, 2);
    sink->start();
    QTRY_VERIFY_WITH_TIMEOUT(sink->isOpen(), 5000);
    server->takeMessages();

    // Ten times real time while the server reads nothing, until the kernel's socket buffers, however large this host
    // makes them, are full and the ring overflows
    server->hold();
    QElapsedTimer clock;
    clock.start();
    int written = 0;
    while (sink->getFramesDropped() == 0 && clock.elapsed() < 60 * 1000) {
        writeTaggedFrame(*sink, written++);
        QThread::usleep(1000);
    }
    // The writer never waits on the network, however far behind it is
    QElapsedTimer burst;
    burst.start();
    for (int i = 0; i < 1000; i++) {
        writeTaggedFrame(*sink, written++);
    }
    const qint64 burstNsecs = burst.nsecsElapsed();
    const quint64 droppedDuringStall = sink->getFramesDropped();
    qDebug() << "the ring overflowed after" << clock.elapsed() << "ms of writing to a held server";
    server->release();

    // Back to real time: the newest frames arrive promptly once the server reads again
    for (int i = 0; i < 50; i++) {
        writeTaggedFrame(*sink, written++);
        QThread::msleep(10);
    }
    const int lastIndex = written - 1;
    QElapsedTimer latency;
    latency.start();
    QVector<int> indices;
    std::vector<QByteArray> messages;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (auto& message : server->takeMessages()) {
            messages.push_back(message);
        }
        indices = frameIndices(messages);
        return !indices.isEmpty() && indices.last() >= lastIndex - 1;
    }(), 5000);
    const qint64 latencyMsec = latency.elapsed();

    qDebug() << "written" << written << "sent" << sink->getFramesSent() << "dropped" << sink->getFramesDropped()
             << "dropped during the stall" << droppedDuringStall << "received" << indices.size() << "1000 writes took"
             << burstNsecs / 1000 << "us; newest frame arrived" << latencyMsec << "ms after the last write";

    QVERIFY2(burstNsecs >= 0 && burstNsecs < 5 * 1000 * 1000, qPrintable(QString::number(burstNsecs)));
    QVERIFY(droppedDuringStall > 0);
    QVERIFY(latencyMsec < 500);
    // In order, no duplicates, no torn frames; only the oldest frames are ever dropped
    for (int i = 0; i < indices.size(); i++) {
        QVERIFY(indices.at(i) >= 0);
        if (i > 0) {
            QVERIFY2(indices.at(i) > indices.at(i - 1), qPrintable(QString("%1 after %2").arg(indices.at(i)).arg(indices.at(i - 1))));
        }
    }
    // Every frame was sent or dropped, but for an odd one still waiting for the second frame of its message
    const quint64 accounted = sink->getFramesSent() + sink->getFramesDropped();
    QVERIFY2(accounted == (quint64)written || accounted + 1 == (quint64)written, qPrintable(QString::number(accounted)));
    QCOMPARE((quint64)indices.size(), sink->getFramesSent());
}

void AudioStreamSinkTests::testAuthorizationOnlyWithCredential() {
    AudioStreamCredential credential;
    credential.authorization = "Bearer sink-test-token";
    auto withCredential = AudioStreamSink::create(server->url("/ears"), AudioConstants::STEREO, 2, credential);
    withCredential->start();
    QTRY_VERIFY_WITH_TIMEOUT(withCredential->isOpen(), 5000);
    QCOMPARE(server->authorizations().last(), QByteArray("Bearer sink-test-token"));

    auto without = AudioStreamSink::create(server->url("/ears"), AudioConstants::STEREO);
    without->start();
    QTRY_VERIFY_WITH_TIMEOUT(without->isOpen(), 5000);
    QCOMPARE(server->authorizations().last(), QByteArray("(none)"));
}

void AudioStreamSinkTests::testRefusedHandshakeReportsStatus() {
    auto sink = AudioStreamSink::create(server->refusingUrl(), AudioConstants::STEREO);
    Finished finished;
    finished.watch(sink.get());
    sink->start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.reasons, QStringList({ "http 401" }), 5000);
    QVERIFY(!sink->isOpen());
    QTest::qWait(200);
    QCOMPARE(finished.reasons.size(), 1);
}

void AudioStreamSinkTests::testStopClosesConnection() {
    auto sink = AudioStreamSink::create(server->url("/ears"), AudioConstants::STEREO);
    Finished finished;
    finished.watch(sink.get());
    sink->start();
    QTRY_VERIFY_WITH_TIMEOUT(sink->isOpen(), 5000);
    const int disconnections = server->disconnections();

    sink->stop();
    QTRY_COMPARE_WITH_TIMEOUT(finished.reasons, QStringList({ "stopped" }), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(server->disconnections(), disconnections + 1, 5000);
    QVERIFY(!sink->isOpen());
    writeTaggedFrame(*sink, 0);
    sink->stop();
    QTest::qWait(200);
    QCOMPARE(finished.reasons.size(), 1);
}

void AudioStreamSinkTests::testStopDuringStallDoesNotBlock() {
    auto sink = AudioStreamSink::create(server->url("/ears"), AudioConstants::STEREO);
    Finished finished;
    finished.watch(sink.get());
    std::atomic<int> closed { 0 };
    QObject::connect(sink.get(), &AudioStreamSink::closed, &finished, [&closed] { closed++; });
    sink->start();
    QTRY_VERIFY_WITH_TIMEOUT(sink->isOpen(), 5000);

    // A backlog in Qt's buffer as well as the kernel's, so the close frame waits behind it
    server->hold();
    QElapsedTimer clock;
    clock.start();
    int written = 0;
    while (sink->getFramesDropped() == 0 && clock.elapsed() < 60 * 1000) {
        writeTaggedFrame(*sink, written++);
        QThread::usleep(1000);
    }
    QVERIFY(sink->getFramesDropped() > 0);

    QElapsedTimer stopping;
    stopping.start();
    sink->stop();
    const qint64 stopNsecs = stopping.nsecsElapsed();
    QTRY_COMPARE_WITH_TIMEOUT(closed.load(), 1, 5000);
    const qint64 closedMsec = stopping.elapsed();
    QElapsedTimer destroying;
    destroying.start();
    sink.reset();
    const qint64 destroyMsec = destroying.elapsed();
    server->release();
    qDebug() << "stop() took" << stopNsecs / 1000 << "us, closed after" << closedMsec << "ms, destroying took"
             << destroyMsec << "ms";

    QVERIFY2(stopNsecs < 5 * 1000 * 1000, qPrintable(QString::number(stopNsecs)));
    QCOMPARE(finished.reasons, QStringList({ "stopped" }));
    // Destroying a closed sink has nothing to wait for
    QVERIFY2(destroyMsec < 100, qPrintable(QString::number(destroyMsec)));
    QTest::qWait(200);
}

void AudioStreamSinkTests::testDestroyDuringCloseAborts() {
    auto sink = AudioStreamSink::create(server->url("/ears"), AudioConstants::STEREO);
    sink->start();
    QTRY_VERIFY_WITH_TIMEOUT(sink->isOpen(), 5000);
    server->hold();
    QElapsedTimer clock;
    clock.start();
    int written = 0;
    while (sink->getFramesDropped() == 0 && clock.elapsed() < 60 * 1000) {
        writeTaggedFrame(*sink, written++);
        QThread::usleep(1000);
    }

    // The close handshake cannot finish while the server is held; destroying the sink cuts its wait short
    sink->stop();
    QThread::msleep(50);
    QElapsedTimer destroying;
    destroying.start();
    sink.reset();
    const qint64 destroyMsec = destroying.elapsed();
    server->release();
    qDebug() << "destroying a closing sink took" << destroyMsec << "ms";
    QVERIFY2(destroyMsec < 300, qPrintable(QString::number(destroyMsec)));
    QTest::qWait(200);
}

void AudioStreamSinkTests::testServerCloseFinishesOnce() {
    auto sink = AudioStreamSink::create(server->url("/ears"), AudioConstants::STEREO);
    Finished finished;
    finished.watch(sink.get());
    sink->start();
    QTRY_VERIFY_WITH_TIMEOUT(sink->isOpen(), 5000);

    server->closeClients();
    QTRY_COMPARE_WITH_TIMEOUT(finished.reasons, QStringList({ "closed" }), 5000);
    QVERIFY(!sink->isOpen());
    writeTaggedFrame(*sink, 0);
    sink->stop();
    QTest::qWait(300);
    QCOMPARE(finished.reasons, QStringList({ "closed" }));
}

void AudioStreamSinkTests::testDestroyClosesWithoutFinished() {
    auto sink = AudioStreamSink::create(server->url("/ears"), AudioConstants::STEREO);
    Finished finished;
    finished.watch(sink.get());
    sink->start();
    QTRY_VERIFY_WITH_TIMEOUT(sink->isOpen(), 5000);
    const int disconnections = server->disconnections();

    QElapsedTimer clock;
    clock.start();
    sink.reset();
    qDebug() << "destroying an open sink took" << clock.elapsed() << "ms";
    QTRY_COMPARE_WITH_TIMEOUT(server->disconnections(), disconnections + 1, 5000);
    QTest::qWait(200);
    QVERIFY(finished.reasons.isEmpty());
}
