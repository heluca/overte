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

#include <glm/glm.hpp>

#include <plugins/CodecPlugin.h>
#include <ReceivedMessage.h>
#include <Sound.h>

#include "AudioGate.h"
#include "MixedAudioStream.h"

class Agent;
class ScriptableAvatar;

/// The audio an Agent sends as its avatar: codec negotiation with the audio mixer, the 100 Hz send loop for
/// Agent.playAvatarSound, and the audio frames of a playing recording. Lives on the Agent's thread as its child.
class AgentAvatarAudio : public QObject {
    Q_OBJECT

public:
    AgentAvatarAudio(Agent& agent, MixedAudioStream& receivedAudioStream);
    ~AgentAvatarAudio();

    bool isPlayingAvatarSound() const { return _avatarSound != NULL; }
    void setAvatarSound(SharedSoundPointer avatarSound) { _avatarSound = avatarSound; }

    QStringList getAudioCodecs() const;
    void setAudioCodecs(const QStringList& audioCodecs);

    glm::vec3 getAudioPositionOffset() const;
    void setAudioPositionOffset(const glm::vec3& audioPositionOffset);

    void negotiateAudioFormat();

    void startSending();
    void stopSending();

    /// Encodes and sends one audio frame of a playing recording. Runs on the main thread.
    void handleRecordingAudioFrame(const QByteArray& frameData, float volume,
        const QSharedPointer<ScriptableAvatar>& scriptedAvatar);

private slots:
    void handleSelectedAudioFormat(QSharedPointer<ReceivedMessage> message);
    void processAgentAvatarAudio();

private:
    void selectAudioFormat(const QString& selectedCodecName);
    void encodeFrameOfZeros(QByteArray& encodedZeros);
    void computeLoudness(const QByteArray* decodedBuffer, QSharedPointer<ScriptableAvatar>);
    glm::vec3 audioPosition(const ScriptableAvatar& avatar) const;

    Agent& _agent;
    MixedAudioStream& _receivedAudioStream;

    SharedSoundPointer _avatarSound;
    bool _shouldMuteRecordingAudio { false };
    int _numAvatarSoundSentBytes = 0;
    QHash<QUuid, quint16> _outgoingScriptAudioSequenceNumbers;

    AudioGate _audioGate;
    bool _audioGateOpen { true };

    QStringList _availableCodecs;  // set in the constructor

    // The script and the audio send loop share the Agent's thread; recording playback's audio frame handler runs on
    // the main thread and reads the offset through audioPosition.
    mutable QMutex _audioSettingsMutex;
    QStringList _audioCodecs;
    glm::vec3 _audioPositionOffset { 0.0f };

    // Written only by selectAudioFormat and the destructor on the Agent's thread, which therefore reads them unlocked;
    // the lock is for recording playback's audio frame handler on the main thread.
    QMutex _codecMutex;
    CodecPluginPointer _codec;
    QString _selectedCodecName;
    Encoder* _encoder { nullptr };
    QTimer _avatarAudioTimer;
    QTimer _negotiateAudioFormatRetryTimer;
    int _negotiateAudioFormatRetries { 0 };
    QStringList _lastOfferedCodecs;
    bool _flushEncoder { false };
};

#endif // overte_AgentAvatarAudio_h
