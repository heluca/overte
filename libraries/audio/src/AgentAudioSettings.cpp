//
//  AgentAudioSettings.cpp
//  libraries/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "AgentAudioSettings.h"

namespace AgentAudioSettings {

QStringList offeredCodecs(const QStringList& requested, const QStringList& available, QStringList& dropped) {
    QStringList offered;
    for (const auto& name : requested) {
        if (!available.contains(name)) {
            dropped.append(name);
        } else if (!offered.contains(name)) {
            offered.append(name);
        }
    }
    return offered.isEmpty() ? available : offered;
}

}
