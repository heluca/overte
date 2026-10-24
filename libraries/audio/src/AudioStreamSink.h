//
//  AudioStreamSink.h
//  libraries/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_AudioStreamSink_h
#define overte_AudioStreamSink_h

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include <QtCore/QObject>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QUrl>

#include "AudioRingBuffer.h"
#include "AudioStreamSource.h"

class QWebSocket;

// Sends audio frames to a WebSocket as binary messages of framesPerMessage network frames of interleaved s16le, with no
// header. The socket lives on a thread of its own; writers never wait on the network. Frames wait in a ring of 200 ms
// while the socket has more than a few frames queued, and the oldest are dropped when it is full.
class AudioStreamSink : public QObject {
    Q_OBJECT
public:
    static const int MAX_FRAMES_PER_MESSAGE;

    // Null unless the URL is ws or wss and framesPerMessage is 1 to MAX_FRAMES_PER_MESSAGE. Only the credential's
    // authorization is used: a WebSocket handshake does not follow redirects.
    static std::unique_ptr<AudioStreamSink> create(const QUrl& url, int numChannels, int framesPerMessage = 2,
                                                   const AudioStreamCredential& credential = {});
    // Closes the socket and joins the thread, without emitting finished. Never call it on the sink's own thread.
    ~AudioStreamSink();

    int getFrameSamples() const { return _frameSamples; }

    // Thread-safe. Opens the socket; frames are taken from the first one written after it connects.
    void start();
    // Thread-safe. Closes the socket and emits finished("stopped") unless the sink has already finished.
    void stop();

    // Thread-safe and never blocks beyond a short lock. Ignored unless the socket is open.
    void writeFrame(const int16_t* samples, int numSamples);
    void writeSilenceFrame();

    bool isOpen() const { return _open; }
    quint64 getFramesSent() const { return _framesSent; }
    quint64 getFramesDropped() const { return _framesDropped; }

signals:
    // Emitted once, from the sink's thread: "closed" when the server ends the connection, "http <status>" when it
    // refuses the handshake, "error: <description>" otherwise, "stopped" after stop().
    void finished(const QString& reason);

private:
    AudioStreamSink(const QUrl& url, int numChannels, int framesPerMessage, const AudioStreamCredential& credential);

    void enqueue(const int16_t* samples, int numSamples);

    // Sink thread only
    void openSocket();
    void closeSocket();
    void drain();
    void finish(const QString& reason);
    void logCounters(const QString& when);

    const QUrl _url;
    const int _frameSamples;
    const int _framesPerMessage;
    const QByteArray _authorization;

    QThread _thread;
    QObject _context;  // lives on _thread; owns the socket and the stats timer
    QWebSocket* _socket { nullptr };
    QTimer* _statsTimer { nullptr };
    QString _errorReason;
    bool _closed { false };
    bool _finished { false };

    std::mutex _mutex;
    AudioRingBuffer _ring;
    std::atomic<bool> _open { false };
    std::atomic<bool> _drainPending { false };
    std::atomic<quint64> _framesSent { 0 };
    std::atomic<quint64> _framesDropped { 0 };
};

#endif  // overte_AudioStreamSink_h
