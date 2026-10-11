//
//  AudioStreamSource.cpp
//  libraries/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "AudioStreamSource.h"

#include <algorithm>
#include <cstring>

#include <QtCore/QtEndian>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <DependencyManager.h>
#include <NetworkAccessManager.h>
#include <NetworkingConstants.h>
#include <ResourceManager.h>

#include "AudioConstants.h"
#include "AudioLogging.h"

// The latency a stream may build up before its oldest audio is dropped, so a producer clock that runs ahead of the
// mixer's never grows the delay
static const int MAX_BUFFERED_FRAMES = 20;  // 200 ms

AudioStreamSourcePointer AudioStreamSource::create(const QUrl& url, int numChannels) {
    auto resourceManager = DependencyManager::get<ResourceManager>();
    AudioStreamSourcePointer source(new AudioStreamSource(resourceManager->normalizeURL(url), numChannels),
                                    &QObject::deleteLater);
    source->moveToThread(resourceManager->getThread());
    return source;
}

AudioStreamSource::AudioStreamSource(const QUrl& url, int numChannels) :
    _url(url),
    _numChannels(numChannels)
{
}

AudioStreamSource::~AudioStreamSource() {
    if (_reply) {
        _reply->disconnect(this);
        _reply->abort();
        _reply->deleteLater();
    }
}

int AudioStreamSource::getFrameBytes() const {
    return AudioConstants::NETWORK_FRAME_BYTES_PER_CHANNEL * _numChannels;
}

void AudioStreamSource::start(bool local, bool network) {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const int frameSamples = AudioConstants::NETWORK_FRAME_SAMPLES_PER_CHANNEL * _numChannels;
        if (local) {
            _rings[Local] = std::make_unique<AudioRingBuffer>(frameSamples, MAX_BUFFERED_FRAMES);
        }
        if (network) {
            _rings[Network] = std::make_unique<AudioRingBuffer>(frameSamples, MAX_BUFFERED_FRAMES);
        }
    }
    if (local || network) {
        QMetaObject::invokeMethod(this, [this] { sendRequest(); }, Qt::QueuedConnection);
    }
}

void AudioStreamSource::stop() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _ended = true;
        for (auto& ring : _rings) {
            ring.reset();
        }
    }
    QMetaObject::invokeMethod(this, [this] {
        if (_reply) {
            _reply->abort();
        }
    }, Qt::QueuedConnection);
}

void AudioStreamSource::sendRequest() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_ended) {
            return;
        }
    }

    QNetworkRequest request(_url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, NetworkingConstants::OVERTE_USER_AGENT);
    // A live stream has no end to cache
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);

    _reply = NetworkAccessManager::getInstance().get(request);
    connect(_reply, &QNetworkReply::readyRead, this, &AudioStreamSource::onReadyRead);
    connect(_reply, &QNetworkReply::finished, this, &AudioStreamSource::onFinished);
}

void AudioStreamSource::onReadyRead() {
    QByteArray bytes = _partialFrame + _reply->readAll();
    const int status = _reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300) {
        // An error page is not audio; onFinished reports the error
        _partialFrame.clear();
        return;
    }

    // A chunk can end inside a sample, or between the channels of a stereo sample; keep that part for the next chunk
    const int sampleFrameBytes = _numChannels * AudioConstants::SAMPLE_SIZE;
    const int usableBytes = bytes.size() - bytes.size() % sampleFrameBytes;
    _partialFrame = bytes.mid(usableBytes);
    if (usableBytes == 0) {
        return;
    }

    QByteArray samples(usableBytes, Qt::Uninitialized);
    qFromLittleEndian<qint16>(bytes.constData(), usableBytes / AudioConstants::SAMPLE_SIZE, samples.data());

    std::lock_guard<std::mutex> lock(_mutex);
    for (auto& ring : _rings) {
        if (!ring) {
            continue;
        }
        // A write longer than the ring keeps its own oldest samples, so hand over only the newest ring's worth
        const int capacityBytes = ring->getSampleCapacity() * AudioConstants::SAMPLE_SIZE;
        const int offset = std::max(0, usableBytes - capacityBytes);
        ring->writeData(samples.constData() + offset, usableBytes - offset);
    }
}

void AudioStreamSource::onFinished() {
    if (_reply->bytesAvailable() > 0) {
        onReadyRead();
    }

    const auto error = _reply->error();
    const QString url = _url.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemoveQuery);
    if (error != QNetworkReply::NoError && error != QNetworkReply::OperationCanceledError) {
        qCWarning(audio) << "Audio stream" << url << "ended with an error:" << _reply->errorString();
    } else {
        qCDebug(audio) << "Audio stream" << url << "ended";
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _ended = true;
    }
    _reply->deleteLater();
    _reply = nullptr;
}

AudioStreamSource::Frame AudioStreamSource::readFrame(Consumer consumer, char* destination) {
    const int frameBytes = getFrameBytes();

    std::lock_guard<std::mutex> lock(_mutex);
    auto& ring = _rings[consumer];
    if (!ring) {
        return Frame::Ended;
    }

    const int availableBytes = ring->samplesAvailable() * AudioConstants::SAMPLE_SIZE;
    if (availableBytes >= frameBytes || (_ended && availableBytes > 0)) {
        const int readBytes = ring->readData(destination, frameBytes);
        memset(destination + readBytes, 0, frameBytes - readBytes);
        return Frame::Audio;
    }
    if (_ended) {
        return Frame::Ended;
    }
    memset(destination, 0, frameBytes);
    return Frame::Silence;
}
