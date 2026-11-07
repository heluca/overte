//
//  AudioCodecNegotiation.h
//  libraries/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_AudioCodecNegotiation_h
#define overte_AudioCodecNegotiation_h

#include <map>
#include <utility>
#include <vector>

#include <QtCore/QStringList>

#include <plugins/Forward.h>

// How the audio mixer picks the codec for a client's NegotiateAudioFormat offer.
namespace AudioCodecNegotiation {

/// The codec names in the audio_env.codec_preference_order setting, most preferred first. Names are trimmed and empty
/// names are skipped, so "opus, zlib, pcm" and "opus,zlib,pcm," both give ("opus", "zlib", "pcm").
QStringList parsePreferenceOrder(const QString& setting);

/// The most preferred codec in <code>offered</code> that is in <code>available</code> and in
/// <code>preferenceOrder</code>. When nothing matches, the name is empty and the codec is null.
std::pair<QString, CodecPluginPointer> negotiate(const std::vector<QString>& offered,
                                                 const std::map<QString, CodecPluginPointer>& available,
                                                 const QStringList& preferenceOrder);

}

#endif // overte_AudioCodecNegotiation_h
