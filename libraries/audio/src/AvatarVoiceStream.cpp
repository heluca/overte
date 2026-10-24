//
//  AvatarVoiceStream.cpp
//  libraries/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "AvatarVoiceStream.h"

#include <algorithm>
#include <cstring>

#include <QtCore/QStringList>

#include "AudioConstants.h"
#include "AudioLogging.h"

// The mixer's jitter buffer takes a short burst; more than this per 10 ms tick would read as a glitch of its own
const int AvatarVoiceStream::MAX_FRAMES_PER_TICK = 4;

static const int FRAMES_PER_SECOND = 100;
static const int STATS_INTERVAL_SECONDS = 10;

std::unique_ptr<AvatarVoiceStream> AvatarVoiceStream::create(const QUrl& url, int prebufferFrames, int idleTimeoutFrames,
                                                             const AudioStreamCredential& credential) {
    auto source = AudioStreamSource::create(url, AudioConstants::MONO, credential);
    if (!source) {
        return nullptr;
    }
    source->setPrebufferFrames(prebufferFrames);
    source->start(false, true);
    return std::unique_ptr<AvatarVoiceStream>(new AvatarVoiceStream(url, source, std::max(0, idleTimeoutFrames)));
}

AvatarVoiceStream::AvatarVoiceStream(const QUrl& url, AudioStreamSourcePointer source, int idleTimeoutFrames) :
    _url(url),
    _source(source),
    _idleTimeoutFrames(idleTimeoutFrames)
{
}

AvatarVoiceStream::~AvatarVoiceStream() {
    _source->stop();
}

AvatarVoiceStream::Frame AvatarVoiceStream::nextFrame(int16_t* samples) {
    const int frameSamples = AudioConstants::NETWORK_FRAME_SAMPLES_PER_CHANNEL;
    if (_ended) {
        memset(samples, 0, frameSamples * sizeof(int16_t));
        return Frame::Ended;
    }

    ++_framesRead;
    if (_framesRead % FRAMES_PER_SECOND == 0) {
        _bufferedPerSecond.append(_source->getBufferedFrames(AudioStreamSource::Network));
        if (_bufferedPerSecond.size() == STATS_INTERVAL_SECONDS) {
            logStats();
        }
    }

    const auto read = _source->readFrame(AudioStreamSource::Network, reinterpret_cast<char*>(samples));
    if (read == AudioStreamSource::Frame::Ended) {
        _ended = true;
        _endReason = _source->getEndReason();
        logStats();
        return Frame::Ended;
    }

    if (read == AudioStreamSource::Frame::Silence) {
        ++_underruns;
    }
    const bool audible = read == AudioStreamSource::Frame::Audio &&
        std::any_of(samples, samples + frameSamples, [](int16_t sample) { return sample != 0; });
    if (audible) {
        ++_audioFrames;
        _underrunsInARow = 0;
        _audible = true;
        return Frame::Audio;
    }

    // A producer that streams zeros while it has nothing to say is not idle
    if (read == AudioStreamSource::Frame::Silence) {
        ++_underrunsInARow;
    } else {
        _underrunsInARow = 0;
    }
    if (_idleTimeoutFrames > 0 && _underrunsInARow >= _idleTimeoutFrames) {
        _ended = true;
        _endReason = "idle";
        _source->stop();
        logStats();
        return Frame::Ended;
    }
    if (_audible) {
        _audible = false;
        return Frame::Flush;
    }
    return Frame::Silence;
}

int AvatarVoiceStream::framesDue(quint64 nowUsecs) {
    const quint64 FRAME_USECS = AudioConstants::NETWORK_FRAME_USECS;
    if (_nextFrameUsecs == 0) {
        _nextFrameUsecs = nowUsecs;
    }
    const quint64 maxLagUsecs = AudioStreamSource::MAX_BUFFERED_FRAMES * FRAME_USECS;
    if (nowUsecs > _nextFrameUsecs + maxLagUsecs) {
        _nextFrameUsecs = nowUsecs - maxLagUsecs;
    }
    // A tick a little early still sends its frame, so timer jitter does not alternate zero and two frames
    int frames = 0;
    while (frames < MAX_FRAMES_PER_TICK && _nextFrameUsecs <= nowUsecs + FRAME_USECS / 2) {
        _nextFrameUsecs += FRAME_USECS;
        ++frames;
    }
    if (frames > 1) {
        _catchUpFrames += frames - 1;
    }
    return frames;
}

void AvatarVoiceStream::logStats() {
    QStringList buffered;
    for (int frames : _bufferedPerSecond) {
        buffered << QString::number(frames);
    }
    _bufferedPerSecond.clear();
    qCDebug(audio).noquote().nospace() << "Avatar voice stream " << _url.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemoveQuery)
                                       << (_ended ? " ended (" + _endReason + ")" : QString(" running"))
                                       << ": framesRead " << _framesRead << ", audioFrames " << _audioFrames
                                       << ", underruns " << _underruns << ", catchUpFrames " << _catchUpFrames
                                       << ", buffered per second [" << buffered.join(" ") << "]";
}
