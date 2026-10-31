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

#include <chrono>

#include <NodeList.h>
#include <udt/PacketHeaders.h>

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
    _audioCodec(this, agent, receivedAudioStream),
    _avatarAudioTimer(this)
{
    // 100Hz timer for audio
    const int TARGET_INTERVAL_MSEC = 10; // 10ms
    connect(&_avatarAudioTimer, &QTimer::timeout, this, &AgentAvatarAudio::processAgentAvatarAudio);
    _avatarAudioTimer.setSingleShot(false);
    _avatarAudioTimer.setInterval(TARGET_INTERVAL_MSEC);
    _avatarAudioTimer.setTimerType(Qt::PreciseTimer);
}

// Runs in Agent::aboutToFinish on the Agent's thread, after the recording frame handlers are unregistered: no new
// recording frame call starts, but Frame::handleFrame calls a copied handler outside its lock, so a call already running
// on the main thread is not waited for (#81). No timer may tick and no packet may arrive once the encoder is
// released (#50, #51, #53). ~AgentAudioCodec, which runs next, releases it.
AgentAvatarAudio::~AgentAvatarAudio() {
    _avatarAudioTimer.stop();
}

void AgentAvatarAudio::setAvatarSound(SharedSoundPointer avatarSound) {
    _avatarSound = avatarSound;
    // After the swap, as in playVoiceStream, so a stream played from the signal clears the sound
    if (avatarSound) {
        stopVoiceStream();
    }
}

void AgentAvatarAudio::playVoiceStream(std::unique_ptr<AvatarVoiceStream> voiceStream) {
    const bool replacesStream = (bool)_voiceStream;
    if (_avatarSound || (replacesStream && _voiceStream->wasAudible())) {
        _flushEncoder = true;
    }
    _avatarSound.clear();
    _numAvatarSoundSentBytes = 0;
    _shouldMuteRecordingAudio = false;
    _voiceStream = std::move(voiceStream);
    // After the swap, so a script that starts another stream from the signal is not undone
    if (replacesStream) {
        emit voiceStreamFinished("stopped");
    }
}

void AgentAvatarAudio::stopVoiceStream() {
    if (_voiceStream) {
        // A flush left pending would replace the first frame of the next sound
        _flushEncoder = _flushEncoder || _voiceStream->wasAudible();
        resetVoiceStream();
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

    // this handler runs on the main thread, codec selection on the Agent's
    QByteArray encodedBuffer;
    QString codecName = _audioCodec.encode(audio, encodedBuffer);

    AbstractAudioInterface::emitAudioPacket(encodedBuffer.data(), encodedBuffer.size(), audioSequenceNumber, false,
                                            audioTransform, scriptedAvatar->getWorldPosition(), glm::vec3(0),
                                            packetType, codecName);
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
    // time: without making them up the source's ring stays fuller after each stall until it drops audio. A steady
    // clock, not usecTimestampNow(): a wall clock stepping back would hold the voice for as long as the step.
    const int framesDue = _voiceStream->framesDue((quint64)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
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
                    _flushEncoder = _flushEncoder || _voiceStream->wasAudible();
                    resetVoiceStream();
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
            audioPacket->writeString(_audioCodec.codecName());

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
            audioPacket->writeString(_audioCodec.codecName());

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
                _flushEncoder = false;
                _audioCodec.encodeFrameOfZeros(encodedBuffer);
                // loudness is 0
                computeLoudness(nullptr, scriptedAvatar);
            } else {
                QByteArray decodedBuffer(reinterpret_cast<const char*>(nextSoundOutput), numAvailableSamples*sizeof(int16_t));
                _audioCodec.encode(decodedBuffer, encodedBuffer);
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
