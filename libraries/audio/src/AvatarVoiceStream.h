//
//  AvatarVoiceStream.h
//  libraries/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_AvatarVoiceStream_h
#define overte_AvatarVoiceStream_h

#include <cstdint>
#include <memory>

#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtCore/QVector>

#include "AudioStreamSource.h"

// A live mono PCM stream played as an avatar's own voice: the frames an Agent's audio send loop encodes, one network
// frame at a time, from an AudioStreamSource with a prebuffer. Tells the send loop when to flush its encoder, when the
// stream has gone idle, and how many frames are due so that it keeps the producer's clock after a stall. Used on one
// thread; the source behind it fills from the network thread.
class AvatarVoiceStream {
public:
    enum class Frame {
        Audio,    // samples holds audio with at least one sample that is not zero
        Silence,  // nothing to play: an underrun, the prebuffer filling, or a frame of zeros
        Flush,    // the first silent frame after audio; the encoder needs one frame of zeros before silent packets
        Ended     // the stream is over, see getEndReason(); every later call returns Ended too
    };

    static const int MAX_FRAMES_PER_TICK;

    // Null unless the URL is http or https. prebufferFrames is clamped to AudioStreamSource::MAX_BUFFERED_FRAMES; an
    // idleTimeoutFrames of 0 never ends the stream for being idle.
    static std::unique_ptr<AvatarVoiceStream> create(const QUrl& url, int prebufferFrames, int idleTimeoutFrames,
                                                     const AudioStreamCredential& credential = {});
    // Stops the source and aborts its request
    ~AvatarVoiceStream();

    const QUrl& getURL() const { return _url; }

    // Writes AudioConstants::NETWORK_FRAME_SAMPLES_PER_CHANNEL samples, zeros unless it returns Audio.
    Frame nextFrame(int16_t* samples);

    // The reason of an Ended frame: the source's, or "idle" after idleTimeoutFrames frames in a row without audio
    const QString& getEndReason() const { return _endReason; }

    // The frames to send on a timer tick at nowUsecs, 0 to MAX_FRAMES_PER_TICK, keeping one frame per 10 ms from the
    // first call. A timer tick that comes late, as a QTimer's does behind a stalled script, is made up on the next
    // ticks; a stall longer than the source's ring is not, since the ring has already dropped that audio.
    int framesDue(quint64 nowUsecs);

private:
    AvatarVoiceStream(const QUrl& url, AudioStreamSourcePointer source, int idleTimeoutFrames);

    void logStats();

    const QUrl _url;
    const AudioStreamSourcePointer _source;
    const int _idleTimeoutFrames;

    int _underrunsInARow { 0 };
    bool _audible { false };
    bool _ended { false };
    QString _endReason;
    quint64 _nextFrameUsecs { 0 };

    // Logged every 10 s: frame counts, frames sent late to catch up, and the ring's fill once a second
    quint64 _framesRead { 0 };
    quint64 _audioFrames { 0 };
    quint64 _underruns { 0 };
    quint64 _catchUpFrames { 0 };
    QVector<int> _bufferedPerSecond;
};

#endif  // overte_AvatarVoiceStream_h
