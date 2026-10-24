//
//  AgentAvatarAudio.cpp
//  assignment-client/src
//
//  Copyright 2013 High Fidelity, Inc.
//  Copyright 2023 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "AgentAvatarAudio.h"

#include <NodeList.h>
#include <SharedUtil.h>
#include <udt/PacketHeaders.h>

#include <plugins/PluginManager.h>
#include <recording/RecordingScriptingInterface.h>

#include "AbstractAudioInterface.h"
#include "Agent.h"
#include "AgentAudioSettings.h"
#include "avatars/ScriptableAvatar.h"

AgentAvatarAudio::AgentAvatarAudio(Agent& agent, MixedAudioStream& receivedAudioStream) :
    QObject(&agent),
    _agent(agent),
    _receivedAudioStream(receivedAudioStream),
    _audioGate(AudioConstants::SAMPLE_RATE, AudioConstants::MONO),
    _avatarAudioTimer(this),
    _negotiateAudioFormatRetryTimer(this)
{
    for (const auto& plugin : PluginManager::getInstance()->getCodecPlugins()) {
        _availableCodecs.append(plugin->getName());
    }
    _audioCodecs = _availableCodecs;

    DependencyManager::get<NodeList>()->getPacketReceiver().registerListener(PacketType::SelectedAudioFormat,
        PacketReceiver::makeUnsourcedListenerReference<AgentAvatarAudio>(this, &AgentAvatarAudio::handleSelectedAudioFormat));

    // A lost SelectedAudioFormat leaves the Agent without a codec while the mixer sends Opus (#57); adopt the
    // codec the mixer is using, as AudioClient does. Queued: the signal fires inside _receivedAudioStream.parseData,
    // and selectAudioFormat replaces that stream's decoder.
    connect(&_receivedAudioStream, &InboundAudioStream::mismatchedAudioCodec, this,
        [this](SharedNodePointer node, const QString& currentCodec, const QString& receivedCodec) {
            qDebug() << "Audio codec mismatch from" << node->getUUID() << "- selected" << currentCodec
                << "received" << receivedCodec;
            selectAudioFormat(receivedCodec);
        }, Qt::QueuedConnection);

    // 100Hz timer for audio
    const int TARGET_INTERVAL_MSEC = 10; // 10ms
    connect(&_avatarAudioTimer, &QTimer::timeout, this, &AgentAvatarAudio::processAgentAvatarAudio);
    _avatarAudioTimer.setSingleShot(false);
    _avatarAudioTimer.setInterval(TARGET_INTERVAL_MSEC);
    _avatarAudioTimer.setTimerType(Qt::PreciseTimer);

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

// Runs in Agent::aboutToFinish on the Agent's thread, after the recording frame handlers are unregistered: no new
// recording frame call starts, but Frame::handleFrame calls a copied handler outside its lock, so a call already running
// on the main thread is not waited for (#81). No timer may tick and no packet may arrive once the encoder is
// released (#50, #51, #53).
AgentAvatarAudio::~AgentAvatarAudio() {
    _avatarAudioTimer.stop();
    _negotiateAudioFormatRetryTimer.stop();
    disconnect(&_receivedAudioStream, nullptr, this, nullptr);
    DependencyManager::get<NodeList>()->getPacketReceiver().unregisterListener(this);

    QMutexLocker codecLocker(&_codecMutex);
    if (_codec && _encoder) {
        _codec->releaseEncoder(_encoder);
        _encoder = nullptr;
    }
}

void AgentAvatarAudio::setAvatarSound(SharedSoundPointer avatarSound) {
    if (avatarSound) {
        stopVoiceStream();
    }
    _avatarSound = avatarSound;
}

void AgentAvatarAudio::playVoiceStream(std::unique_ptr<AvatarVoiceStream> voiceStream) {
    const bool replacesStream = (bool)_voiceStream;
    if (_avatarSound || replacesStream) {
        _avatarSound.clear();
        _numAvatarSoundSentBytes = 0;
        _flushEncoder = true;
    }
    _shouldMuteRecordingAudio = false;
    _voiceStream = std::move(voiceStream);
    // After the swap, so a script that starts another stream from the signal is not undone
    if (replacesStream) {
        emit voiceStreamFinished("stopped");
    }
}

void AgentAvatarAudio::stopVoiceStream() {
    if (_voiceStream) {
        resetVoiceStream();
        _flushEncoder = true;
        emit voiceStreamFinished("stopped");
    }
}

void AgentAvatarAudio::resetVoiceStream() {
    if (_voiceStream) {
        _voiceStream.reset();
        _shouldMuteRecordingAudio = false;
    }
}

void AgentAvatarAudio::startSending() {
    QMetaObject::invokeMethod(&_avatarAudioTimer, "start");
}

void AgentAvatarAudio::stopSending() {
    QMetaObject::invokeMethod(&_avatarAudioTimer, "stop");
}

QStringList AgentAvatarAudio::getAudioCodecs() const {
    QMutexLocker locker(&_audioSettingsMutex);
    return _audioCodecs;
}

void AgentAvatarAudio::setAudioCodecs(const QStringList& audioCodecs) {
    QStringList dropped;
    {
        QMutexLocker locker(&_audioSettingsMutex);
        _audioCodecs = AgentAudioSettings::offeredCodecs(audioCodecs, _availableCodecs, dropped);
    }
    if (!dropped.isEmpty()) {
        qWarning() << "Agent.audioCodecs: dropped unknown codecs" << dropped << "- available:" << _availableCodecs;
    }
    QMetaObject::invokeMethod(this, [this] { negotiateAudioFormat(); });
}

glm::vec3 AgentAvatarAudio::getAudioPositionOffset() const {
    QMutexLocker locker(&_audioSettingsMutex);
    return _audioPositionOffset;
}

void AgentAvatarAudio::setAudioPositionOffset(const glm::vec3& audioPositionOffset) {
    QMutexLocker locker(&_audioSettingsMutex);
    _audioPositionOffset = audioPositionOffset;
}

glm::vec3 AgentAvatarAudio::audioPosition(const ScriptableAvatar& avatar) const {
    return AgentAudioSettings::audioPosition(avatar.getWorldPosition(), avatar.getWorldOrientation(),
        getAudioPositionOffset());
}

void AgentAvatarAudio::negotiateAudioFormat() {
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

void AgentAvatarAudio::handleSelectedAudioFormat(QSharedPointer<ReceivedMessage> message) {
    _negotiateAudioFormatRetryTimer.stop();
    _negotiateAudioFormatRetries = 0;
    QString selectedCodecName = message->readString();
    selectAudioFormat(selectedCodecName);
}

void AgentAvatarAudio::selectAudioFormat(const QString& selectedCodecName) {
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

void AgentAvatarAudio::handleRecordingAudioFrame(const QByteArray& frameData, float volume,
        const QSharedPointer<ScriptableAvatar>& scriptedAvatar) {
    if (_shouldMuteRecordingAudio) {
        return;
    }

    static quint16 audioSequenceNumber{ 0 };

    QByteArray audio(frameData);

    int16_t* samples = reinterpret_cast<int16_t*>(audio.data());
    int numSamples = AudioConstants::NETWORK_FRAME_SAMPLES_PER_CHANNEL;

    if (volume >= 0.0f && volume < 1.0f) {
        int32_t fract = (int32_t)(volume * (float)(1 << 16));   // Q16
        for (int i = 0; i < numSamples; i++) {
            samples[i] = (fract * (int32_t)samples[i]) >> 16;
        }
    }

    if (_agent.isNoiseGateEnabled()) {
        _audioGate.render(samples, samples, numSamples);
    }

    computeLoudness(&audio, scriptedAvatar);

    // state machine to detect gate opening and closing
    bool audioGateOpen = (scriptedAvatar->getAudioLoudness() != 0.0f);
    bool openedInLastBlock = !_audioGateOpen && audioGateOpen;  // the gate just opened
    bool closedInLastBlock = _audioGateOpen && !audioGateOpen;  // the gate just closed
    _audioGateOpen = audioGateOpen;
    Q_UNUSED(openedInLastBlock);

    // the codec must be flushed to silence before sending silent packets,
    // so delay the transition to silent packets by one packet after becoming silent.
    auto packetType = PacketType::MicrophoneAudioNoEcho;
    if (!audioGateOpen && !closedInLastBlock) {
        packetType = PacketType::SilentAudioFrame;
    }

    Transform audioTransform;
    auto headOrientation = scriptedAvatar->getHeadOrientation();
    audioTransform.setTranslation(audioPosition(*scriptedAvatar));
    audioTransform.setRotation(headOrientation);

    // this handler runs on the main thread, selectAudioFormat on the Agent's
    QByteArray encodedBuffer;
    QString codecName;
    {
        QMutexLocker codecLocker(&_codecMutex);
        if (_encoder) {
            _encoder->encode(audio, encodedBuffer);
        } else {
            encodedBuffer = audio;
        }
        codecName = _selectedCodecName;
    }

    AbstractAudioInterface::emitAudioPacket(encodedBuffer.data(), encodedBuffer.size(), audioSequenceNumber, false,
                                            audioTransform, scriptedAvatar->getWorldPosition(), glm::vec3(0),
                                            packetType, codecName);
}

void AgentAvatarAudio::encodeFrameOfZeros(QByteArray& encodedZeros) {
    _flushEncoder = false;
    static const QByteArray zeros(AudioConstants::NETWORK_FRAME_BYTES_PER_CHANNEL, 0);
    if (_encoder) {
        _encoder->encode(zeros, encodedZeros);
    } else {
        encodedZeros = zeros;
    }
}

void AgentAvatarAudio::computeLoudness(const QByteArray* decodedBuffer, QSharedPointer<ScriptableAvatar> scriptableAvatar) {
    float lastInputLoudness = 0.0f;
    if (decodedBuffer) {
        auto samples = reinterpret_cast<const int16_t*>(decodedBuffer->constData());
        int numSamples = decodedBuffer->size() / AudioConstants::SAMPLE_SIZE;

        assert(numSamples < 65536); // int32_t loudness cannot overflow
        if (numSamples > 0) {
            int32_t loudness = 0;
            for (int i = 0; i < numSamples; ++i) {
                loudness += std::abs((int32_t)samples[i]);
            }
            lastInputLoudness = (float)loudness / numSamples;
        }
    }
    scriptableAvatar->setAudioLoudness(lastInputLoudness);
}

void AgentAvatarAudio::processAgentAvatarAudio() {
    if (!_voiceStream) {
        sendAvatarAudioFrame();
        return;
    }
    // A QTimer skips the ticks a stalled script misses rather than catching up, while the stream's producer keeps real
    // time: without making them up the source's ring stays fuller after each stall until it drops audio
    const int framesDue = _voiceStream->framesDue(usecTimestampNow());
    for (int i = 0; i < framesDue; ++i) {
        if (!sendAvatarAudioFrame()) {
            break;
        }
    }
}

bool AgentAvatarAudio::sendAvatarAudioFrame() {
    auto recordingInterface = DependencyManager::get<RecordingScriptingInterface>();
    bool isPlayingRecording = recordingInterface->isPlaying();
    bool voiceStreamEnded = false;
    QString voiceStreamEndReason;

    if (_agent.isAvatar() &&
        ((_agent.isListeningToAudioStream() && !isPlayingRecording) || _avatarSound || _voiceStream)) {
        // if we have an avatar audio stream then send it out to our audio-mixer
        auto scriptedAvatar = DependencyManager::get<ScriptableAvatar>();
        bool silentFrame = true;

        int16_t numAvailableSamples = AudioConstants::NETWORK_FRAME_SAMPLES_PER_CHANNEL;
        const int16_t* nextSoundOutput = NULL;

        if (_voiceStream) {
            if (isPlayingRecording && !_shouldMuteRecordingAudio) {
                _shouldMuteRecordingAudio = true;
            }
            switch (_voiceStream->nextFrame(_voiceFrame)) {
                case AvatarVoiceStream::Frame::Audio:
                    nextSoundOutput = _voiceFrame;
                    silentFrame = false;
                    break;
                case AvatarVoiceStream::Frame::Silence:
                    break;
                case AvatarVoiceStream::Frame::Flush:
                    _flushEncoder = true;
                    break;
                case AvatarVoiceStream::Frame::Ended:
                    voiceStreamEnded = true;
                    voiceStreamEndReason = _voiceStream->getEndReason();
                    resetVoiceStream();
                    _flushEncoder = true;
                    break;
            }
        } else if (_avatarSound && _avatarSound->isReady()) {
            if (isPlayingRecording && !_shouldMuteRecordingAudio) {
                _shouldMuteRecordingAudio = true;
            }
            
            auto audioData = _avatarSound->getAudioData();
            nextSoundOutput = reinterpret_cast<const int16_t*>(audioData->rawData()
                    + _numAvatarSoundSentBytes);

            int numAvailableBytes = (audioData->getNumBytes() - _numAvatarSoundSentBytes) > AudioConstants::NETWORK_FRAME_BYTES_PER_CHANNEL
                ? AudioConstants::NETWORK_FRAME_BYTES_PER_CHANNEL
                : audioData->getNumBytes() - _numAvatarSoundSentBytes;
            numAvailableSamples = (int16_t)numAvailableBytes / sizeof(int16_t);


            // check if the all of the _numAvatarAudioBufferSamples to be sent are silence
            for (int i = 0; i < numAvailableSamples; ++i) {
                if (nextSoundOutput[i] != 0) {
                    silentFrame = false;
                    break;
                }
            }

            _numAvatarSoundSentBytes += numAvailableBytes;
            if (_numAvatarSoundSentBytes == (int)audioData->getNumBytes()) {
                // we're done with this sound object - so set our pointer back to NULL
                // and our sent bytes back to zero
                _avatarSound.clear();
                _numAvatarSoundSentBytes = 0;
                _flushEncoder = true;

                if (_shouldMuteRecordingAudio) {
                    _shouldMuteRecordingAudio = false;
                }
            }
        }

        auto audioPacket = NLPacket::create(silentFrame && !_flushEncoder
                ? PacketType::SilentAudioFrame
                : PacketType::MicrophoneAudioNoEcho);

        // seek past the sequence number, will be packed when destination node is known
        audioPacket->seek(sizeof(quint16));

        if (silentFrame && !_flushEncoder) {

            if (!_agent.isListeningToAudioStream()) {
                // if we have a silent frame and we're not listening then just send nothing and break out of here
                return true;
            }

            // write the codec
            audioPacket->writeString(_selectedCodecName);

            // write the number of silent samples so the audio-mixer can uphold timing
            audioPacket->writePrimitive(numAvailableSamples);

            // use the orientation and position of this avatar for the source of this audio
            audioPacket->writePrimitive(audioPosition(*scriptedAvatar));
            glm::quat headOrientation = scriptedAvatar->getHeadOrientation();
            audioPacket->writePrimitive(headOrientation);
            audioPacket->writePrimitive(scriptedAvatar->getWorldPosition());
            audioPacket->writePrimitive(glm::vec3(0));

            // no matter what, the loudness should be set to 0
            computeLoudness(nullptr, scriptedAvatar);
        } else if (nextSoundOutput || _flushEncoder) {

            // write the codec
            audioPacket->writeString(_selectedCodecName);

            // assume scripted avatar audio is mono and set channel flag to zero
            audioPacket->writePrimitive((quint8)0);

            // use the orientation and position of this avatar for the source of this audio
            audioPacket->writePrimitive(audioPosition(*scriptedAvatar));
            glm::quat headOrientation = scriptedAvatar->getHeadOrientation();
            audioPacket->writePrimitive(headOrientation);
            // the bounding box corner, unused while the box scale below is zero
            audioPacket->writePrimitive(scriptedAvatar->getWorldPosition());
            audioPacket->writePrimitive(glm::vec3(0));

            QByteArray encodedBuffer;
            if (_flushEncoder) {
                encodeFrameOfZeros(encodedBuffer);
                // loudness is 0
                computeLoudness(nullptr, scriptedAvatar);
            } else {
                QByteArray decodedBuffer(reinterpret_cast<const char*>(nextSoundOutput), numAvailableSamples*sizeof(int16_t));
                if (_encoder) {
                    // encode it
                    _encoder->encode(decodedBuffer, encodedBuffer);
                } else {
                    encodedBuffer = decodedBuffer;
                }
                computeLoudness(&decodedBuffer, scriptedAvatar);
            }
            audioPacket->write(encodedBuffer.constData(), encodedBuffer.size());
        }

        // we should never have both nextSoundOutput being null and silentFrame being false, but lets
        // assert on it in case things above change in a bad way
        assert(nextSoundOutput || silentFrame);

        // write audio packet to AudioMixer nodes
        auto nodeList = DependencyManager::get<NodeList>();
        nodeList->eachNode([this, &nodeList, &audioPacket](const SharedNodePointer& node) {
            // only send to nodes of type AudioMixer
            if (node->getType() == NodeType::AudioMixer) {
                // pack sequence number
                quint16 sequence = _outgoingScriptAudioSequenceNumbers[node->getUUID()]++;
                audioPacket->seek(0);
                audioPacket->writePrimitive(sequence);
                // send audio packet
                nodeList->sendUnreliablePacket(*audioPacket, *node);
            }
        });
    }

    // Last, as the script's handler may start another stream
    if (voiceStreamEnded) {
        emit voiceStreamFinished(voiceStreamEndReason);
    }
    return !voiceStreamEnded;
}
