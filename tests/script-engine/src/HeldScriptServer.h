//
//  HeldScriptServer.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_HeldScriptServer_h
#define overte_HeldScriptServer_h

#include <memory>

#include <QCoreApplication>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

// Serves one script at /held.js on 127.0.0.1, from a thread of its own. It answers a request only after the test thread,
// which is also the script thread, has worked through every event posted to it before the request arrived and every
// event those posted in turn: the response waits for two round trips through the test thread's event queue.
class HeldScriptServer {
public:
    explicit HeldScriptServer(const QByteArray& script) : _script(script) {
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
    ~HeldScriptServer() {
        QMetaObject::invokeMethod(&_context, [this] { delete _server; }, Qt::BlockingQueuedConnection);
        _thread.quit();
        _thread.wait();
    }
    QString url() const { return QString("http://127.0.0.1:%1/held.js").arg(_port); }

private:
    void serve(QTcpSocket* socket) {
        auto received = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, received] {
            received->append(socket->readAll());
            if (!received->contains("\r\n\r\n")) {
                return;
            }
            QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/javascript\r\nContent-Length: " +
                QByteArray::number(_script.size()) + "\r\nConnection: close\r\n\r\n" + _script;
            QPointer<QTcpSocket> held(socket);
            QObject* context = &_context;
            QMetaObject::invokeMethod(qApp, [held, context, response] {
                QMetaObject::invokeMethod(qApp, [held, context, response] {
                    QMetaObject::invokeMethod(context, [held, response] {
                        if (held) {
                            held->write(response);
                            held->disconnectFromHost();
                        }
                    }, Qt::QueuedConnection);
                }, Qt::QueuedConnection);
            }, Qt::QueuedConnection);
        });
    }

    QByteArray _script;
    QThread _thread;
    QObject _context;
    QTcpServer* _server { nullptr };
    quint16 _port { 0 };
};

#endif // overte_HeldScriptServer_h
