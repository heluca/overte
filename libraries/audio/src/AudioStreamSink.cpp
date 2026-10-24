//
//  AudioStreamSink.cpp
//  libraries/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "AudioStreamSink.h"

#include <algorithm>

#include <QtCore/QEventLoop>
#include <QtCore/QRegularExpression>
#include <QtCore/QtEndian>
#include <QtNetwork/QNetworkRequest>
#include <QtWebSockets/QWebSocket>

#include <NetworkingConstants.h>

#include "AudioConstants.h"
#include "AudioLogging.h"

const int AudioStreamSink::MAX_FRAMES_PER_MESSAGE = 10;

static const int RING_FRAMES = 20;          // 200 ms
static const int MAX_QUEUED_FRAMES = 4;     // in the socket, before frames wait in the ring
static const int STATS_INTERVAL_MSEC = 10 * 1000;
static const int CLOSE_WAIT_MSEC = 500;

std::unique_ptr<AudioStreamSink> AudioStreamSink::create(const QUrl& url, int numChannels, int framesPerMessage,
                                                         const AudioStreamCredential& credential) {
    const QString scheme = url.scheme();
    if (scheme != "ws" && scheme != "wss") {
        qCWarning(audio) << "Audio sink URL must be ws or wss:"
                         << url.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemoveQuery);
        return nullptr;
    }
    if (framesPerMessage < 1 || framesPerMessage > MAX_FRAMES_PER_MESSAGE) {
        qCWarning(audio) << "Audio sink framesPerMessage must be 1 to" << MAX_FRAMES_PER_MESSAGE << "- got"
                         << framesPerMessage;
        return nullptr;
    }
    return std::unique_ptr<AudioStreamSink>(new AudioStreamSink(url, numChannels, framesPerMessage, credential));
}

AudioStreamSink::AudioStreamSink(const QUrl& url, int numChannels, int framesPerMessage,
                                 const AudioStreamCredential& credential) :
    _url(url),
    _frameSamples(AudioConstants::NETWORK_FRAME_SAMPLES_PER_CHANNEL * numChannels),
    _framesPerMessage(framesPerMessage),
    _authorization(credential.authorization),
    _ring(_frameSamples, RING_FRAMES)
{
    _thread.setObjectName("AudioStreamSink");
    _context.moveToThread(&_thread);
    _thread.start();
}

AudioStreamSink::~AudioStreamSink() {
    Q_ASSERT(QThread::currentThread() != &_thread);
    QMetaObject::invokeMethod(&_context, [this] { closeSocket(); }, Qt::BlockingQueuedConnection);
    _thread.quit();
    _thread.wait();
}

void AudioStreamSink::start() {
    QMetaObject::invokeMethod(&_context, [this] { openSocket(); }, Qt::QueuedConnection);
}

void AudioStreamSink::stop() {
    QMetaObject::invokeMethod(&_context, [this] {
        closeSocket();
        finish("stopped");
    }, Qt::QueuedConnection);
}

void AudioStreamSink::writeFrame(const int16_t* samples, int numSamples) {
    if (_open && numSamples > 0) {
        enqueue(samples, numSamples);
    }
}

void AudioStreamSink::writeSilenceFrame() {
    if (_open) {
        enqueue(nullptr, _frameSamples);
    }
}

void AudioStreamSink::enqueue(const int16_t* samples, int numSamples) {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const int capacity = _ring.getSampleCapacity();
        if (numSamples > capacity) {
            // Only the newest ring's worth can be kept
            const int skipped = numSamples - capacity;
            _framesDropped += (skipped + _frameSamples - 1) / _frameSamples;
            if (samples) {
                samples += skipped;
            }
            numSamples = capacity;
        }
        const int room = capacity - _ring.samplesAvailable();
        if (numSamples > room) {
            // Drop whole frames, oldest first; AudioRingBuffer would overwrite them too, but log each overflow
            const int framesToDrop = (numSamples - room + _frameSamples - 1) / _frameSamples;
            _ring.skipSamples(framesToDrop * _frameSamples);
            _framesDropped += framesToDrop;
        }
        if (samples) {
            _ring.writeData(reinterpret_cast<const char*>(samples), numSamples * AudioConstants::SAMPLE_SIZE);
        } else {
            _ring.addSilentSamples(numSamples);
        }
    }
    // At most one drain is queued at a time, however fast frames arrive
    if (!_drainPending.exchange(true)) {
        QMetaObject::invokeMethod(&_context, [this] { drain(); }, Qt::QueuedConnection);
    }
}

void AudioStreamSink::openSocket() {
    if (_socket || _closed) {
        return;
    }
    _socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, &_context);
    connect(_socket, &QWebSocket::connected, &_context, [this] {
        qCDebug(audio) << "Audio sink connected to" << _url.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemoveQuery);
        _open = true;
        _statsTimer = new QTimer(&_context);
        connect(_statsTimer, &QTimer::timeout, &_context, [this] { logCounters("running"); });
        _statsTimer->start(STATS_INTERVAL_MSEC);
    });
    connect(_socket, &QWebSocket::bytesWritten, &_context, [this] { drain(); });
    connect(_socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), &_context,
            [this](QAbstractSocket::SocketError error) {
        if (error == QAbstractSocket::RemoteHostClosedError) {
            return;  // disconnected follows and reports "closed"
        }
        const QString description = _socket->errorString();
        static const QRegularExpression HTTP_STATUS("http status code: (\\d+)");
        const auto match = HTTP_STATUS.match(description);
        _errorReason = match.hasMatch() ? "http " + match.captured(1) : "error: " + description;
        if (!_open) {
            // A failed connection attempt may end without a disconnected signal
            closeSocket();
            finish(_errorReason);
        }
    });
    connect(_socket, &QWebSocket::disconnected, &_context, [this] {
        QString reason = _errorReason;
        if (reason.isEmpty()) {
            reason = _open ? "closed" : "error: not connected";
        }
        closeSocket();
        finish(reason);
    });

    QNetworkRequest request(_url);
    request.setHeader(QNetworkRequest::UserAgentHeader, NetworkingConstants::OVERTE_USER_AGENT);
    if (!_authorization.isEmpty()) {
        request.setRawHeader("Authorization", _authorization);
    }
    _socket->open(request);
}

void AudioStreamSink::closeSocket() {
    _closed = true;
    _open = false;
    if (_statsTimer) {
        _statsTimer->stop();
        delete _statsTimer;
        _statsTimer = nullptr;
    }
    if (!_socket) {
        return;
    }
    QWebSocket* socket = _socket;
    _socket = nullptr;
    socket->disconnect(&_context);
    if (socket->state() == QAbstractSocket::ConnectedState) {
        // Give the close handshake a moment so the server sees a clean close, not a reset
        QEventLoop loop;
        connect(socket, &QWebSocket::disconnected, &loop, &QEventLoop::quit);
        QTimer::singleShot(CLOSE_WAIT_MSEC, &loop, &QEventLoop::quit);
        socket->close();
        if (socket->state() != QAbstractSocket::UnconnectedState) {
            loop.exec();
        }
    } else {
        socket->abort();
    }
    // Can be reached from one of the socket's own signals
    socket->deleteLater();
}

void AudioStreamSink::drain() {
    _drainPending = false;
    if (!_socket || !_open) {
        return;
    }
    const int messageSamples = _frameSamples * _framesPerMessage;
    const qint64 maxQueuedBytes = MAX_QUEUED_FRAMES * _frameSamples * AudioConstants::SAMPLE_SIZE;
    QByteArray message(messageSamples * AudioConstants::SAMPLE_SIZE, Qt::Uninitialized);
    auto samples = reinterpret_cast<int16_t*>(message.data());
    while (_socket->bytesToWrite() < maxQueuedBytes) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_ring.samplesAvailable() < messageSamples) {
                return;
            }
            _ring.readSamples(samples, messageSamples);
        }
        qToLittleEndian<int16_t>(samples, messageSamples, samples);
        _socket->sendBinaryMessage(message);
        _framesSent += _framesPerMessage;
    }
}

void AudioStreamSink::finish(const QString& reason) {
    if (_finished) {
        return;
    }
    _finished = true;
    logCounters("finished: " + reason);
    emit finished(reason);
}

void AudioStreamSink::logCounters(const QString& when) {
    qCDebug(audio).noquote().nospace() << "Audio sink " << _url.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemoveQuery)
                                       << " " << when << ": framesSent " << _framesSent.load() << ", framesDropped "
                                       << _framesDropped.load();
}
