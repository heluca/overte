//
//  AgentAudioCodec.h
//  assignment-client/src
//
//  Copyright 2013 High Fidelity, Inc.
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_AgentAudioCodec_h
#define overte_AgentAudioCodec_h

#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QSharedPointer>
#include <QtCore/QStringList>
#include <QtCore/QTimer>

#include <plugins/CodecPlugin.h>
#include <ReceivedMessage.h>

#include "MixedAudioStream.h"

class Agent;

/// An Agent's audio codec: the Agent.audioCodecs setting, negotiation with the audio mixer, and the encoder for the
/// avatar audio the Agent sends. Lives on the Agent's thread as a child of AgentAvatarAudio.
class AgentAudioCodec : public QObject {
    Q_OBJECT

public:
    AgentAudioCodec(QObject* parent, Agent& agent, MixedAudioStream& receivedAudioStream);
    ~AgentAudioCodec();

    QStringList getAudioCodecs() const;
    void setAudioCodecs(const QStringList& audioCodecs);

    void negotiateAudioFormat();

    /// Encodes one frame with the selected codec, or copies it when none is selected. Returns the codec name the frame
    /// is for, read under the same lock. Any thread.
    QString encode(const QByteArray& decodedBuffer, QByteArray& encodedBuffer);
    /// Encodes one frame of silence, as encode does. Any thread.
    void encodeFrameOfZeros(QByteArray& encodedZeros);
    /// Any thread.
    QString codecName() const;

private slots:
    void handleSelectedAudioFormat(QSharedPointer<ReceivedMessage> message);

private:
    void selectAudioFormat(const QString& selectedCodecName);

    Agent& _agent;
    MixedAudioStream& _receivedAudioStream;

    QStringList _availableCodecs;  // set in the constructor

    // Agent.audioCodecs, read by the script and by negotiateAudioFormat
    mutable QMutex _audioCodecsMutex;
    QStringList _audioCodecs;

    // Written only by selectAudioFormat and the destructor on the Agent's thread, which therefore read them unlocked;
    // encode and codecName lock, for recording playback's audio frame handler on the main thread.
    mutable QMutex _codecMutex;
    CodecPluginPointer _codec;
    QString _selectedCodecName;
    Encoder* _encoder { nullptr };
    QTimer _negotiateAudioFormatRetryTimer;
    int _negotiateAudioFormatRetries { 0 };
    QStringList _lastOfferedCodecs;
};

#endif // overte_AgentAudioCodec_h
