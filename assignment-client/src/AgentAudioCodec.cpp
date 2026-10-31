//
//  AgentAudioCodec.cpp
//  assignment-client/src
//
//  Copyright 2013 High Fidelity, Inc.
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "AgentAudioCodec.h"

#include <NodeList.h>
#include <udt/PacketHeaders.h>

#include <plugins/PluginManager.h>

#include "Agent.h"
#include "AgentAudioSettings.h"

AgentAudioCodec::AgentAudioCodec(QObject* parent, Agent& agent, MixedAudioStream& receivedAudioStream) :
    QObject(parent),
    _agent(agent),
    _receivedAudioStream(receivedAudioStream),
    _negotiateAudioFormatRetryTimer(this)
{
    for (const auto& plugin : PluginManager::getInstance()->getCodecPlugins()) {
        _availableCodecs.append(plugin->getName());
    }
    _audioCodecs = _availableCodecs;

    DependencyManager::get<NodeList>()->getPacketReceiver().registerListener(PacketType::SelectedAudioFormat,
        PacketReceiver::makeUnsourcedListenerReference<AgentAudioCodec>(this, &AgentAudioCodec::handleSelectedAudioFormat));

    // A lost SelectedAudioFormat leaves the Agent without a codec while the mixer sends Opus (#57); adopt the
    // codec the mixer is using, as AudioClient does. Queued: the signal fires inside _receivedAudioStream.parseData,
    // and selectAudioFormat replaces that stream's decoder.
    connect(&_receivedAudioStream, &InboundAudioStream::mismatchedAudioCodec, this,
        [this](SharedNodePointer node, const QString& currentCodec, const QString& receivedCodec) {
            qDebug() << "Audio codec mismatch from" << node->getUUID() << "- selected" << currentCodec
                << "received" << receivedCodec;
            selectAudioFormat(receivedCodec);
        }, Qt::QueuedConnection);

    // The mixer can answer NegotiateAudioFormat before its own socket to this Agent is active, and then drops the
    // SelectedAudioFormat reply (#57). Ask again until a reply arrives.
    const int NEGOTIATE_AUDIO_FORMAT_RETRY_MSEC = 1000;
    _negotiateAudioFormatRetryTimer.setSingleShot(true);
    _negotiateAudioFormatRetryTimer.setInterval(NEGOTIATE_AUDIO_FORMAT_RETRY_MSEC);
    connect(&_negotiateAudioFormatRetryTimer, &QTimer::timeout, this, [this] {
        ++_negotiateAudioFormatRetries;
        const int RETRY_LOG_INTERVAL = 10;
        if (_negotiateAudioFormatRetries == 1 || _negotiateAudioFormatRetries % RETRY_LOG_INTERVAL == 0) {
            qDebug() << "No SelectedAudioFormat from the audio mixer - negotiating again, attempt"
                << _negotiateAudioFormatRetries;
        }
        negotiateAudioFormat();
    });
}

// Runs from ~AgentAvatarAudio, after its send timer has stopped. No timer may tick and no packet may arrive once the
// encoder is released (#50, #51, #53); a recording frame already encoding on the main thread holds the lock (#81).
AgentAudioCodec::~AgentAudioCodec() {
    _negotiateAudioFormatRetryTimer.stop();
    disconnect(&_receivedAudioStream, nullptr, this, nullptr);
    DependencyManager::get<NodeList>()->getPacketReceiver().unregisterListener(this);

    QMutexLocker codecLocker(&_codecMutex);
    if (_codec && _encoder) {
        _codec->releaseEncoder(_encoder);
        _encoder = nullptr;
    }
}

QStringList AgentAudioCodec::getAudioCodecs() const {
    QMutexLocker locker(&_audioCodecsMutex);
    return _audioCodecs;
}

void AgentAudioCodec::setAudioCodecs(const QStringList& audioCodecs) {
    QStringList dropped;
    {
        QMutexLocker locker(&_audioCodecsMutex);
        _audioCodecs = AgentAudioSettings::offeredCodecs(audioCodecs, _availableCodecs, dropped);
    }
    if (!dropped.isEmpty()) {
        qWarning() << "Agent.audioCodecs: dropped unknown codecs" << dropped << "- available:" << _availableCodecs;
    }
    QMetaObject::invokeMethod(this, [this] { negotiateAudioFormat(); });
}

void AgentAudioCodec::negotiateAudioFormat() {
    auto nodeList = DependencyManager::get<NodeList>();
    auto negotiateFormatPacket = NLPacket::create(PacketType::NegotiateAudioFormat);
    const QStringList codecs = getAudioCodecs();
    if (codecs != _lastOfferedCodecs) {
        qDebug() << "Offering audio codecs" << codecs;
        _lastOfferedCodecs = codecs;
    }
    negotiateFormatPacket->writePrimitive((quint8)codecs.size());
    for (const auto& codecName : codecs) {
        negotiateFormatPacket->writeString(codecName);
    }

    // grab our audio mixer from the NodeList, if it exists
    SharedNodePointer audioMixer = nodeList->soloNodeOfType(NodeType::AudioMixer);

    if (audioMixer && !_agent.isFinished()) {
        nodeList->sendPacket(std::move(negotiateFormatPacket), *audioMixer);
        _negotiateAudioFormatRetryTimer.start();
    }
}

void AgentAudioCodec::handleSelectedAudioFormat(QSharedPointer<ReceivedMessage> message) {
    _negotiateAudioFormatRetryTimer.stop();
    _negotiateAudioFormatRetries = 0;
    QString selectedCodecName = message->readString();
    selectAudioFormat(selectedCodecName);
}

void AgentAudioCodec::selectAudioFormat(const QString& selectedCodecName) {
    // both callers are queued and can land after aboutToFinish has destroyed the PluginManager
    if (_agent.isFinished() || _selectedCodecName == selectedCodecName) {
        return;
    }
    qDebug() << "Selected Codec:" << selectedCodecName;

    QMutexLocker codecLocker(&_codecMutex);
    _selectedCodecName = selectedCodecName;

    // release any old codec encoder/decoder first...
    if (_codec && _encoder) {
        _codec->releaseEncoder(_encoder);
        _encoder = nullptr;
        _codec = nullptr;
    }
    _receivedAudioStream.cleanupCodec();

    const auto& codecPlugins = PluginManager::getInstance()->getCodecPlugins();
    for (const auto& plugin : codecPlugins) {
        if (_selectedCodecName == plugin->getName()) {
            _codec = plugin;
            _receivedAudioStream.setupCodec(plugin, _selectedCodecName, AudioConstants::STEREO);
            _encoder = plugin->createEncoder(AudioConstants::SAMPLE_RATE, AudioConstants::MONO);
            qDebug() << "Selected Codec Plugin:" << _codec.get();
            break;
        }
    }
}

QString AgentAudioCodec::encode(const QByteArray& decodedBuffer, QByteArray& encodedBuffer) {
    QMutexLocker codecLocker(&_codecMutex);
    if (_encoder) {
        _encoder->encode(decodedBuffer, encodedBuffer);
    } else {
        encodedBuffer = decodedBuffer;
    }
    return _selectedCodecName;
}

void AgentAudioCodec::encodeFrameOfZeros(QByteArray& encodedZeros) {
    static const QByteArray zeros(AudioConstants::NETWORK_FRAME_BYTES_PER_CHANNEL, 0);
    encode(zeros, encodedZeros);
}

QString AgentAudioCodec::codecName() const {
    QMutexLocker codecLocker(&_codecMutex);
    return _selectedCodecName;
}
