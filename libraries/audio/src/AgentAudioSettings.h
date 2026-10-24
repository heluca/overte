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

// The per-Agent audio settings exposed as Agent.audioCodecs.
namespace AgentAudioSettings {

/// The codecs an Agent offers in NegotiateAudioFormat: the requested names that are in
/// <code>available</code>, in the requested order and without duplicates. Names not available are appended to
/// <code>dropped</code>. When nothing requested is available, the Agent offers every available codec.
QStringList offeredCodecs(const QStringList& requested, const QStringList& available, QStringList& dropped);

}

#endif // overte_AgentAudioSettings_h
