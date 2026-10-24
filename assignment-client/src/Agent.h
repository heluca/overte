//
//  Agent.h
//  assignment-client/src
//
//  Created by Stephen Birarda on 7/1/13.
//  Copyright 2013 High Fidelity, Inc.
//  Copyright 2023 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef hifi_Agent_h
#define hifi_Agent_h

#include <memory>
#include <vector>

#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QSharedPointer>
#include <QtCore/QUrl>
#include <QtCore/QTimer>
#include <QUuid>
#include <QtCore/QSharedPointer>

#include <EntityEditPacketSender.h>
#include <EntityTree.h>
#include <ScriptEngine.h>
#include <ThreadedAssignment.h>

#include <Sound.h>
#include "AgentAvatarAudio.h"
#include "AudioStreamSink.h"
#include "MixedAudioStream.h"
#include "entities/EntityTreeHeadlessViewer.h"
#include "avatars/ScriptableAvatar.h"

class ScriptEngine;
class ScriptManager;
using ScriptEnginePointer = std::shared_ptr<ScriptEngine>;
using ScriptManagerPointer = std::shared_ptr<ScriptManager>;

class Agent : public ThreadedAssignment {
    Q_OBJECT

    Q_PROPERTY(bool isAvatar READ isAvatar WRITE setIsAvatar)
    Q_PROPERTY(bool isPlayingAvatarSound READ isPlayingAvatarSound)
    Q_PROPERTY(bool isListeningToAudioStream READ isListeningToAudioStream WRITE setIsListeningToAudioStream)
    Q_PROPERTY(bool isNoiseGateEnabled READ isNoiseGateEnabled WRITE setIsNoiseGateEnabled)
    Q_PROPERTY(float lastReceivedAudioLoudness READ getLastReceivedAudioLoudness)
    Q_PROPERTY(QUuid sessionUUID READ getSessionUUID)

public:
    Agent(ReceivedMessage& message);

    bool isPlayingAvatarSound() const { return _avatarAudio && _avatarAudio->isPlayingAvatarSound(); }

    bool isListeningToAudioStream() const { return _isListeningToAudioStream; }
    void setIsListeningToAudioStream(bool isListeningToAudioStream);

    bool isNoiseGateEnabled() const { return _isNoiseGateEnabled; }
    void setIsNoiseGateEnabled(bool isNoiseGateEnabled);

    float getLastReceivedAudioLoudness() const { return _lastReceivedAudioLoudness; }
    QUuid getSessionUUID() const;

    QStringList getAudioCodecs() const;
    void setAudioCodecs(const QStringList& audioCodecs);

    glm::vec3 getAudioPositionOffset() const;
    void setAudioPositionOffset(const glm::vec3& audioPositionOffset);

    bool isFinished() const { return _isFinished; }

    // The Agent's thread only, which is also the script's
    bool startAudioSink(const QUrl& url, int framesPerMessage, const AudioStreamCredential& credential);
    void stopAudioSink();
    bool isAudioSinkActive() const { return (bool)_audioSink; }

    virtual void aboutToFinish() override;

public slots:
    void run() override;

    void playAvatarSound(SharedSoundPointer avatarSound);

    void setIsAvatar(bool isAvatar);
    bool isAvatar() const { return _isAvatar; }

    Q_INVOKABLE virtual void stop() override;

signals:
    void audioSinkFinished(const QString& reason);

private slots:
    void handleSettings(const QJsonObject& settingsObject);
    void requestScript();
    void scriptRequestFinished();
    void executeScript();

    void handleAudioPacket(QSharedPointer<ReceivedMessage> message);
    void handleOctreePacket(QSharedPointer<ReceivedMessage> message, SharedNodePointer senderNode);

    void nodeActivated(SharedNodePointer activatedNode);
    void nodeKilled(SharedNodePointer killedNode);

private:
    ScriptManagerPointer _scriptManager;
    EntityEditPacketSender _entityEditSender;
    EntityTreeHeadlessViewer _entityViewer;

    MixedAudioStream _receivedAudioStream;
    float _lastReceivedAudioLoudness;

    void queryAvatars();
    void resetAudioSink();

    QString _scriptContents;
    QTimer* _scriptRequestTimeout { nullptr };
    ResourceRequest* _pendingScriptRequest { nullptr };
    bool _isListeningToAudioStream = false;
    bool _isAvatar = false;
    QTimer* _avatarQueryTimer = nullptr;
    bool _isNoiseGateEnabled { false };

    // created in the constructor, destroyed in aboutToFinish
    std::unique_ptr<AgentAvatarAudio> _avatarAudio;

    // the received mix forwarded to a WebSocket, while the script runs
    std::unique_ptr<AudioStreamSink> _audioSink;
    quint32 _audioSinkSerial { 0 };
};

#endif // hifi_Agent_h
