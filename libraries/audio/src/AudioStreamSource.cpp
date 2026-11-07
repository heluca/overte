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
const int AudioStreamSource::MAX_BUFFERED_FRAMES = 20;  // 200 ms

AudioStreamSourcePointer AudioStreamSource::create(const QUrl& url, int numChannels, const AudioStreamCredential& credential) {
    auto resourceManager = DependencyManager::get<ResourceManager>();
    const QUrl normalizedURL = resourceManager->normalizeURL(url);
    // The network access manager would also read file:, qrc: and ftp: URLs, letting a script stream a host file into
    // the mixer
    const QString scheme = normalizedURL.scheme();
    if (scheme != HIFI_URL_SCHEME_HTTP && scheme != HIFI_URL_SCHEME_HTTPS) {
        qCWarning(audio) << "Audio stream URL must be http or https:"
                         << normalizedURL.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemoveQuery);
        return nullptr;
    }
    AudioStreamSourcePointer source(
        new AudioStreamSource(normalizedURL, numChannels, normalizedURL == url ? credential : AudioStreamCredential()),
        &QObject::deleteLater);
    source->moveToThread(resourceManager->getThread());
    return source;
}

AudioStreamSource::AudioStreamSource(const QUrl& url, int numChannels, const AudioStreamCredential& credential) :
    _url(url),
    _numChannels(numChannels),
    _credential(credential)
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
        if (_endReason.isEmpty()) {
            _endReason = "stopped";
        }
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
    if (!_credential.authorization.isEmpty()) {
        request.setRawHeader("Authorization", _credential.authorization);
        // Qt sends every request header on to a redirect target; onRedirected() decides instead
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    }

    _reply = NetworkAccessManager::getInstance().get(request);
    connect(_reply, &QNetworkReply::readyRead, this, &AudioStreamSource::onReadyRead);
    connect(_reply, &QNetworkReply::finished, this, &AudioStreamSource::onFinished);
    if (!_credential.authorization.isEmpty()) {
        connect(_reply, &QNetworkReply::redirected, this, &AudioStreamSource::onRedirected);
    }
}

void AudioStreamSource::onRedirected(const QUrl& target) {
    if (_credential.appliesTo(target)) {
        emit _reply->redirectAllowed();
        return;
    }
    qCWarning(audio) << "Audio stream" << _url.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemoveQuery)
                     << "was redirected outside the URL prefix of its script credential; stopping it";
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_endReason.isEmpty()) {
            _endReason = "error: redirected outside the URL prefix of the script credential";
        }
    }
    _reply->abort();
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
    const int status = _reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString url = _url.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemoveQuery);
    if (error != QNetworkReply::NoError && error != QNetworkReply::OperationCanceledError) {
        qCWarning(audio) << "Audio stream" << url << "ended with an error:" << _reply->errorString();
    } else {
        qCDebug(audio) << "Audio stream" << url << "ended";
    }

    QString reason;
    if (status >= 300) {
        reason = QString("http %1").arg(status);
    } else if (error == QNetworkReply::NoError) {
        reason = "ended";
    } else {
        reason = "error: " + _reply->errorString();
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _ended = true;
        // stop() and a refused redirect say why before the abort that finishes the reply
        if (_endReason.isEmpty()) {
            _endReason = reason;
        }
    }
    _reply->deleteLater();
    _reply = nullptr;
}

void AudioStreamSource::setPrebufferFrames(int frames) {
    std::lock_guard<std::mutex> lock(_mutex);
    _prebufferFrames = std::max(0, std::min(frames, MAX_BUFFERED_FRAMES));
}

AudioStreamSource::Frame AudioStreamSource::readFrame(Consumer consumer, char* destination) {
    const int frameBytes = getFrameBytes();

    std::lock_guard<std::mutex> lock(_mutex);
    auto& ring = _rings[consumer];
    if (!ring) {
        return Frame::Ended;
    }

    const int availableBytes = ring->samplesAvailable() * AudioConstants::SAMPLE_SIZE;
    // The rest of an ended stream plays whatever its length
    const bool primed = !_priming[consumer] || _ended || availableBytes >= _prebufferFrames * frameBytes;
    if (primed && (availableBytes >= frameBytes || (_ended && availableBytes > 0))) {
        _priming[consumer] = false;
        const int readBytes = ring->readData(destination, frameBytes);
        memset(destination + readBytes, 0, frameBytes - readBytes);
        return Frame::Audio;
    }
    if (_ended) {
        return Frame::Ended;
    }
    _priming[consumer] = true;
    memset(destination, 0, frameBytes);
    return Frame::Silence;
}

int AudioStreamSource::getBufferedFrames(Consumer consumer) {
    std::lock_guard<std::mutex> lock(_mutex);
    const auto& ring = _rings[consumer];
    return ring ? ring->samplesAvailable() / (AudioConstants::NETWORK_FRAME_SAMPLES_PER_CHANNEL * _numChannels) : 0;
}

QString AudioStreamSource::getEndReason() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _endReason;
}
