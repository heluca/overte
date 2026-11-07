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

class QEventLoop;
class QWebSocket;

// Sends audio frames to a WebSocket as binary messages of framesPerMessage network frames of interleaved s16le, with no
// header. The socket lives on a thread of its own; writers never wait on the network. Frames wait in a ring of 200 ms
// while Qt's write buffer holds more than 4 frames, and the oldest are dropped when it is full. The operating system's
// socket buffers come in front of that limit: a reader that stalls is sent seconds of audio before the ring drops any.
class AudioStreamSink : public QObject {
    Q_OBJECT
public:
    static const int MAX_FRAMES_PER_MESSAGE;

    // Null unless the URL is ws or wss and framesPerMessage is 1 to MAX_FRAMES_PER_MESSAGE. Only the credential's
    // authorization is used: a WebSocket handshake does not follow redirects.
    static std::unique_ptr<AudioStreamSink> create(const QUrl& url, int numChannels, int framesPerMessage = 2,
                                                   const AudioStreamCredential& credential = {});
    // Aborts the socket and joins the thread, without emitting finished. Never call it on the sink's own thread. After
    // closed, nothing is left to abort and it returns at once.
    ~AudioStreamSink();

    int getFrameSamples() const { return _frameSamples; }

    // Thread-safe. Opens the socket; frames are taken from the first one written after it connects.
    void start();
    // Thread-safe and never blocks. Closes the socket on the sink's thread, waiting up to 500 ms there for the close
    // handshake, emits finished("stopped") unless the sink has already finished, and then closed.
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
    // Emitted from the sink's thread after every stop(), once the socket is closed
    void closed();

private:
    AudioStreamSink(const QUrl& url, int numChannels, int framesPerMessage, const AudioStreamCredential& credential);

    void enqueue(const int16_t* samples, int numSamples);

    // Sink thread only
    void openSocket();
    // Waits briefly for the close handshake unless abort is set
    void closeSocket(bool abort = false);
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
    // while closeSocket waits for the close handshake
    QWebSocket* _closingSocket { nullptr };
    QEventLoop* _closeWait { nullptr };
    QTimer* _statsTimer { nullptr };
    QString _errorReason;
    bool _closed { false };
    bool _finished { false };

    std::mutex _mutex;
    AudioRingBuffer _ring;
    std::atomic<bool> _open { false };
    std::atomic<bool> _destroying { false };
    std::atomic<bool> _drainPending { false };
    std::atomic<quint64> _framesSent { 0 };
    std::atomic<quint64> _framesDropped { 0 };
};

#endif  // overte_AudioStreamSink_h
