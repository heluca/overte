//
//  AgentAvatarAudio.h
//  assignment-client/src
//
//  Copyright 2013 High Fidelity, Inc.
//  Copyright 2023 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_AgentAvatarAudio_h
#define overte_AgentAvatarAudio_h

#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QSharedPointer>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QUuid>

#include <memory>

#include <glm/glm.hpp>

#include <AudioConstants.h>
#include <AvatarVoiceStream.h>
#include <Sound.h>

#include "AgentAudioCodec.h"
#include "AudioGate.h"
#include "MixedAudioStream.h"

class Agent;
class ScriptableAvatar;

/// The audio an Agent sends as its avatar: the 100 Hz send loop for Agent.playAvatarSound and Agent.playVoiceStream,
/// and the audio frames of a playing recording, encoded by its AgentAudioCodec. Lives on the Agent's thread as its
/// child.
class AgentAvatarAudio : public QObject {
    Q_OBJECT

public:
    AgentAvatarAudio(Agent& agent, MixedAudioStream& receivedAudioStream);
    ~AgentAvatarAudio();

    bool isPlayingAvatarSound() const { return _avatarSound != NULL; }
    // A sound stops a playing voice stream, with voiceStreamFinished("stopped")
    void setAvatarSound(SharedSoundPointer avatarSound);

    // The Agent's thread only. A voice stream replaces a playing sound, and a voice stream it replaces finishes with
    // "stopped".
    bool isPlayingVoiceStream() const { return (bool)_voiceStream; }
    void playVoiceStream(std::unique_ptr<AvatarVoiceStream> voiceStream);
    void stopVoiceStream();
    // Drops the voice stream without voiceStreamFinished, for teardown
    void resetVoiceStream();

    QStringList getAudioCodecs() const { return _audioCodec.getAudioCodecs(); }
    void setAudioCodecs(const QStringList& audioCodecs) { _audioCodec.setAudioCodecs(audioCodecs); }

    glm::vec3 getAudioPositionOffset() const;
    void setAudioPositionOffset(const glm::vec3& audioPositionOffset);

    void negotiateAudioFormat() { _audioCodec.negotiateAudioFormat(); }

    void startSending();
    void stopSending();

    /// Encodes and sends one audio frame of a playing recording. Runs on the main thread.
    void handleRecordingAudioFrame(const QByteArray& frameData, float volume,
        const QSharedPointer<ScriptableAvatar>& scriptedAvatar);

signals:
    // Emitted on the Agent's thread from the send loop, or from stopVoiceStream and what replaces the stream
    void voiceStreamFinished(const QString& reason);

private slots:
    void processAgentAvatarAudio();

private:
    // Sends one frame; false once a voice stream has ended in it
    bool sendAvatarAudioFrame();
    void computeLoudness(const QByteArray* decodedBuffer, QSharedPointer<ScriptableAvatar>);
    glm::vec3 audioPosition(const ScriptableAvatar& avatar) const;

    Agent& _agent;
    MixedAudioStream& _receivedAudioStream;

    SharedSoundPointer _avatarSound;
    // Unlocked: the script, the send timer, executeScript and aboutToFinish all run on the Agent's thread
    std::unique_ptr<AvatarVoiceStream> _voiceStream;
    int16_t _voiceFrame[AudioConstants::NETWORK_FRAME_SAMPLES_PER_CHANNEL];
    bool _shouldMuteRecordingAudio { false };
    int _numAvatarSoundSentBytes = 0;
    QHash<QUuid, quint16> _outgoingScriptAudioSequenceNumbers;

    AudioGate _audioGate;
    bool _audioGateOpen { true };

    // The script and the audio send loop share the Agent's thread; recording playback's audio frame handler runs on
    // the main thread and reads the offset through audioPosition.
    mutable QMutex _audioSettingsMutex;
    glm::vec3 _audioPositionOffset { 0.0f };

    AgentAudioCodec _audioCodec;
    QTimer _avatarAudioTimer;
    bool _flushEncoder { false };
};

#endif // overte_AgentAvatarAudio_h
