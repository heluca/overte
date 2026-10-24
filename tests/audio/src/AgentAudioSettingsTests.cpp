//
//  AgentAudioSettingsTests.cpp
//  tests/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "AgentAudioSettingsTests.h"

#include <AgentAudioSettings.h>

QTEST_MAIN(AgentAudioSettingsTests)

using namespace AgentAudioSettings;

static const QStringList AVAILABLE { "opus", "pcm", "zlib" };

void AgentAudioSettingsTests::testOfferedSubset() {
    QStringList dropped;
    QCOMPARE(offeredCodecs({ "pcm" }, AVAILABLE, dropped), QStringList { "pcm" });
    QCOMPARE(offeredCodecs({ "zlib", "opus" }, AVAILABLE, dropped), (QStringList { "zlib", "opus" }));
    QVERIFY(dropped.isEmpty());
}

void AgentAudioSettingsTests::testOfferedDropsUnknown() {
    QStringList dropped;
    QCOMPARE(offeredCodecs({ "flac", "pcm", "PCM" }, AVAILABLE, dropped), QStringList { "pcm" });
    QCOMPARE(dropped, (QStringList { "flac", "PCM" }));
}

void AgentAudioSettingsTests::testOfferedFallsBackToAvailable() {
    QStringList dropped;
    QCOMPARE(offeredCodecs({}, AVAILABLE, dropped), AVAILABLE);
    QVERIFY(dropped.isEmpty());
    QCOMPARE(offeredCodecs({ "flac" }, AVAILABLE, dropped), AVAILABLE);
    QCOMPARE(dropped, QStringList { "flac" });
}

void AgentAudioSettingsTests::testOfferedDuplicates() {
    QStringList dropped;
    QCOMPARE(offeredCodecs({ "pcm", "opus", "pcm" }, AVAILABLE, dropped), (QStringList { "pcm", "opus" }));
}
