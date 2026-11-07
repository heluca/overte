//
//  AgentAudioSettings.h
//  libraries/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_AgentAudioSettings_h
#define overte_AgentAudioSettings_h

#include <QtCore/QStringList>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// The per-Agent audio settings exposed as Agent.audioCodecs and Agent.audioPositionOffset.
namespace AgentAudioSettings {

/// The codecs an Agent offers in NegotiateAudioFormat: the requested names that are in
/// <code>available</code>, in the requested order and without duplicates. Names not available are appended to
/// <code>dropped</code>. When nothing requested is available, the Agent offers every available codec.
QStringList offeredCodecs(const QStringList& requested, const QStringList& available, QStringList& dropped);

/// The position an Agent sends to the audio mixer: <code>offset</code> is in the avatar frame, so it is rotated by
/// the avatar's orientation before it is added to the avatar's world position.
glm::vec3 audioPosition(const glm::vec3& worldPosition, const glm::quat& orientation, const glm::vec3& offset);

}

#endif // overte_AgentAudioSettings_h
