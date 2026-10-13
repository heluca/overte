//
//  AudioStreamSource.h
//  libraries/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_AudioStreamSource_h
#define overte_AudioStreamSource_h

#include <functional>
#include <memory>
#include <mutex>

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QSharedPointer>
#include <QtCore/QUrl>

#include "AudioRingBuffer.h"

class QNetworkReply;
class AudioStreamSource;
using AudioStreamSourcePointer = QSharedPointer<AudioStreamSource>;

// The Authorization header a script credential gives the stream's request, and whether a redirect target keeps it. The
// caller looks both up; the stream never sees the credential table.
struct AudioStreamCredential {
    QByteArray authorization;
    std::function<bool(const QUrl&)> appliesTo;
};

// Live audio for an AudioInjector: raw little-endian 16-bit PCM at AudioConstants::SAMPLE_RATE, read from an HTTP
// response as it arrives. The object lives on the ResourceManager thread, where the reply's signals are delivered.
// The network injection and the local playback each read from a ring of their own, so neither takes the other's frames.
class AudioStreamSource : public QObject {
    Q_OBJECT
public:
    enum Consumer { Network = 0, Local, NumConsumers };
    enum class Frame { Audio, Silence, Ended };

    // Null unless the URL is http or https. The credential was looked up for url, so it is dropped if url normalizes to
    // another URL.
    static AudioStreamSourcePointer create(const QUrl& url, int numChannels, const AudioStreamCredential& credential = {});
    ~AudioStreamSource();

    int getNumChannels() const { return _numChannels; }
    int getFrameBytes() const;

    // Thread-safe. Creates a ring per enabled consumer and sends the request.
    void start(bool local, bool network);
    // Thread-safe. Ends the stream for both consumers and aborts the request.
    void stop();

    // Thread-safe. Writes one network frame to destination: Audio when the stream had it, the last frame padded with
    // silence; Silence on an underrun, so the reader keeps its timing; Ended, writing nothing, once the request is over
    // and the consumer has read everything.
    Frame readFrame(Consumer consumer, char* destination);

private:
    AudioStreamSource(const QUrl& url, int numChannels, const AudioStreamCredential& credential);

    void sendRequest();
    void onRedirected(const QUrl& target);
    void onReadyRead();
    void onFinished();

    const QUrl _url;
    const int _numChannels;
    const AudioStreamCredential _credential;

    // Network thread only
    QPointer<QNetworkReply> _reply;
    QByteArray _partialFrame;

    std::mutex _mutex;
    std::unique_ptr<AudioRingBuffer> _rings[NumConsumers];
    bool _ended { false };
};

#endif // overte_AudioStreamSource_h
