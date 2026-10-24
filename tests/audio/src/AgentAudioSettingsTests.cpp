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

#include <glm/gtc/constants.hpp>

#include <AgentAudioSettings.h>

QTEST_MAIN(AgentAudioSettingsTests)

using namespace AgentAudioSettings;

static const QStringList AVAILABLE { "opus", "pcm", "zlib" };

static bool closeTo(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(a - b) < 1.0e-5f;
}

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

void AgentAudioSettingsTests::testAudioPositionZeroOffset() {
    const glm::vec3 position { 1.0f, 2.0f, 3.0f };
    const glm::quat orientation = glm::angleAxis(0.7f, glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f)));
    QVERIFY(closeTo(audioPosition(position, orientation, glm::vec3(0.0f)), position));
}

void AgentAudioSettingsTests::testAudioPositionRotated() {
    const glm::vec3 position { 10.0f, 0.0f, -4.0f };
    const glm::vec3 headHeight { 0.0f, 0.6f, 0.0f };
    const glm::vec3 inFront { 0.0f, 0.0f, -0.1f };

    // Upright: the offset is unchanged whatever the yaw about the vertical.
    const glm::quat yawed = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    QVERIFY(closeTo(audioPosition(position, yawed, headHeight), position + headHeight));

    // A quarter turn left about the vertical takes "in front" (-z) to -x.
    QVERIFY(closeTo(audioPosition(position, yawed, inFront), position + glm::vec3(-0.1f, 0.0f, 0.0f)));

    // Lying on the back (pitched up a quarter turn about x): "up" now points to +z.
    const glm::quat pitched = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
    QVERIFY(closeTo(audioPosition(position, pitched, headHeight), position + glm::vec3(0.0f, 0.0f, 0.6f)));
}
