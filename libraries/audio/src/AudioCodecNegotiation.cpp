//
//  AudioCodecNegotiation.cpp
//  libraries/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "AudioCodecNegotiation.h"

#include <limits>

namespace AudioCodecNegotiation {

QStringList parsePreferenceOrder(const QString& setting) {
    QStringList order;
    for (const auto& name : setting.split(",")) {
        const QString trimmed = name.trimmed();
        if (!trimmed.isEmpty()) {
            order.append(trimmed);
        }
    }
    return order;
}

std::pair<QString, CodecPluginPointer> negotiate(const std::vector<QString>& offered,
                                                 const std::map<QString, CodecPluginPointer>& available,
                                                 const QStringList& preferenceOrder) {
    std::pair<QString, CodecPluginPointer> selected;
    int minPreference = std::numeric_limits<int>::max();
    for (const auto& codec : offered) {
        auto it = available.find(codec);
        if (it != available.end()) {
            int preference = preferenceOrder.indexOf(codec);
            if (preference >= 0 && preference < minPreference) {
                minPreference = preference;
                selected = *it;
            }
        }
    }
    return selected;
}

}
