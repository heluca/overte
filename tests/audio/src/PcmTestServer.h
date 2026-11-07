//
//  PcmTestServer.h
//  tests/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_PcmTestServer_h
#define overte_PcmTestServer_h

// Header-only: SetupHifiTestCase builds each *Tests.cpp on its own and ignores any other .cpp file

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QVector>
#include <QtCore/QtEndian>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#include <AudioConstants.h>

namespace pcmtest {

// Odd, so the HTTP chunks split samples and the stereo channels of a sample
const int CHUNK_BYTES = 777;

// Never zero, so audio is told apart from silence
inline int16_t sampleValue(int index) {
    return (int16_t)(index % 30000 + 1);
}

struct Step {
    enum Kind { Audio, Zeros, Wait, End };
    Kind kind;
    int count;  // samples per channel for Audio and Zeros, milliseconds for Wait

    static Step audio(int samples) { return { Audio, samples }; }
    static Step zeros(int samples) { return { Zeros, samples }; }
    static Step wait(int msec) { return { Wait, msec }; }
    static Step end() { return { End, 0 }; }
};

// A response: audio counts up through sampleValue across the Audio steps of a connection
struct Plan {
    int status { 200 };  // anything else is answered with a short body and no audio
    int channels { 1 };
    QVector<Step> steps;
    bool repeat { false };
    // Counts in PcmTestServer::burstsFlushed when the socket has handed all of a step's audio to the OS
    bool signalsFlush { false };
};

// Serves the Plan for each request's path as a chunked HTTP response on 127.0.0.1, from a thread of its own so it never
// waits on the test. A path with no steps is answered 404.
class PcmTestServer {
public:
    using Planner = std::function<Plan(const QByteArray& path)>;

    explicit PcmTestServer(Planner planner) : _planner(planner) {
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
    int burstsFlushed() const { return _burstsFlushed; }

private:
    struct Connection {
        Plan plan;
        int step { 0 };
        int nextSample { 0 };
    };

    void serve(QTcpSocket* socket) {
        auto received = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, [this, socket] {
            _closed++;
            socket->deleteLater();
        });
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, received] {
            if (received->contains("\r\n\r\n")) {
                return;
            }
            received->append(socket->readAll());
            if (!received->contains("\r\n\r\n")) {
                return;
            }
            const QByteArray path = received->split(' ').value(1);
            auto connection = std::make_shared<Connection>();
            connection->plan = _planner(path);
            if (connection->plan.steps.isEmpty()) {
                socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 7\r\nConnection: close\r\n\r\nmissing");
                socket->disconnectFromHost();
                return;
            }
            if (connection->plan.status != 200) {
                // A real server's 401 names its scheme, and Qt's authentication handling reads it
                const QByteArray challenge = connection->plan.status == 401 ? "WWW-Authenticate: Bearer\r\n" : "";
                socket->write(QString("HTTP/1.1 %1 Refused\r\n").arg(connection->plan.status).toLatin1() + challenge +
                              "Content-Length: 7\r\nConnection: close\r\n\r\nrefused");
                socket->disconnectFromHost();
                return;
            }
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nTransfer-Encoding: chunked\r\n\r\n");
            run(socket, connection);
        });
    }

    void run(QTcpSocket* socket, std::shared_ptr<Connection> connection) {
        while (true) {
            if (connection->step == connection->plan.steps.size()) {
                if (!connection->plan.repeat) {
                    return;
                }
                connection->step = 0;
            }
            const Step step = connection->plan.steps[connection->step++];
            if (step.kind == Step::Audio || step.kind == Step::Zeros) {
                const int count = step.count * connection->plan.channels;
                QByteArray pcm(count * AudioConstants::SAMPLE_SIZE, '\0');
                if (step.kind == Step::Audio) {
                    for (int i = 0; i < count; i++) {
                        qToLittleEndian<qint16>(sampleValue(connection->nextSample++),
                                                pcm.data() + i * AudioConstants::SAMPLE_SIZE);
                    }
                }
                for (int offset = 0; offset < pcm.size(); offset += CHUNK_BYTES) {
                    const QByteArray chunk = pcm.mid(offset, CHUNK_BYTES);
                    socket->write(QByteArray::number(chunk.size(), 16) + "\r\n" + chunk + "\r\n");
                }
                if (connection->plan.signalsFlush) {
                    auto flushed = std::make_shared<QMetaObject::Connection>();
                    *flushed = QObject::connect(socket, &QTcpSocket::bytesWritten, socket, [this, socket, flushed] {
                        if (socket->bytesToWrite() == 0) {
                            _burstsFlushed++;
                            QObject::disconnect(*flushed);
                        }
                    });
                }
            } else if (step.kind == Step::Wait) {
                QTimer::singleShot(step.count, socket, [this, socket, connection] { run(socket, connection); });
                return;
            } else {
                socket->write("0\r\n\r\n");
                socket->disconnectFromHost();
                return;
            }
        }
    }

    const Planner _planner;
    QThread _thread;
    QObject _context;
    QTcpServer* _server { nullptr };
    quint16 _port { 0 };
    std::atomic<int> _closed { 0 };
    std::atomic<int> _burstsFlushed { 0 };
};

}  // namespace pcmtest

#endif  // overte_PcmTestServer_h
