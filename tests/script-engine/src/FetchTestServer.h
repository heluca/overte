//
//  FetchTestServer.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_FetchTestServer_h
#define overte_FetchTestServer_h

#include <memory>

#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

// Answers one HTTP/1.1 request per connection on 127.0.0.1, from a thread of its own so it never waits on the script loop
class FetchTestServer {
public:
    FetchTestServer() {
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
    ~FetchTestServer() {
        QMetaObject::invokeMethod(&_context, [this] { delete _server; }, Qt::BlockingQueuedConnection);
        _thread.quit();
        _thread.wait();
    }
    QString base() const { return QString("http://127.0.0.1:%1").arg(_port); }

private:
    static void serve(QTcpSocket* socket) {
        auto received = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, received] {
            received->append(socket->readAll());
            const int headerEnd = received->indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
            }
            QList<QByteArray> lines = received->left(headerEnd).split('\n');
            const QList<QByteArray> requestLine = lines.takeFirst().trimmed().split(' ');
            QHash<QByteArray, QByteArray> headers;
            for (const QByteArray& line : lines) {
                const int colon = line.indexOf(':');
                headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
            }
            const int length = headers.value("content-length").toInt();
            if (received->size() < headerEnd + 4 + length) {
                return;
            }
            const QByteArray method = requestLine.value(0);
            const QByteArray path = requestLine.value(1);
            const QByteArray body = received->mid(headerEnd + 4, length);
            received->clear();

            QByteArray status = "200 OK";
            QByteArray extra;
            QByteArray content;
            if (path == "/text") {
                extra = "Content-Type: text/plain\r\n";
                content = "hello fetch";
            } else if (path == "/redirect") {
                status = "302 Found";
                extra = "Location: /text\r\n";
            } else if (path == "/json") {
                extra = "Content-Type: application/json\r\n";
                content = "{\"answer\": 42, \"list\": [1, 2, 3]}";
            } else if (path == "/badjson") {
                extra = "Content-Type: application/json\r\n";
                content = "{not json";
            } else if (path == "/echo") {
                extra = "X-Echo-Method: " + method + "\r\nX-Echo-Test: " + headers.value("x-test") +
                    "\r\nX-Echo-Type: " + headers.value("content-type") + "\r\nX-Echo-Agent: " +
                    headers.value("user-agent") + "\r\n";
                content = body;
            } else if (path == "/400") {
                status = "400 Bad Request";
                extra = "Content-Type: application/json\r\n";
                content = "{\"error\": \"bad\"}";
            } else if (path == "/badheader") {
                extra = "X(bad): 1\r\n";
            } else if (path == "/hang") {
                return;
            } else {
                status = "404 Not Found";
                content = "missing";
            }
            socket->write("HTTP/1.1 " + status + "\r\n" + extra + "Content-Length: " + QByteArray::number(content.size()) +
                "\r\nConnection: close\r\n\r\n" + content);
            socket->disconnectFromHost();
        });
    }

    QThread _thread;
    QObject _context;
    QTcpServer* _server { nullptr };
    quint16 _port { 0 };
};

#endif // overte_FetchTestServer_h
