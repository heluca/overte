//
//  AudioCodecNegotiationTests.cpp
//  tests/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "AudioCodecNegotiationTests.h"

#include <memory>

#include <AudioCodecNegotiation.h>
#include <plugins/CodecPlugin.h>

QTEST_MAIN(AudioCodecNegotiationTests)

using namespace AudioCodecNegotiation;

namespace {

class FakeCodec : public CodecPlugin {
public:
    explicit FakeCodec(const QString& name) : _name(name) {}
    const QString getName() const override { return _name; }
    Encoder* createEncoder(int, int) override { return nullptr; }
    Decoder* createDecoder(int, int) override { return nullptr; }
    void releaseEncoder(Encoder*) override {}
    void releaseDecoder(Decoder*) override {}

private:
    const QString _name;
};

std::map<QString, CodecPluginPointer> availableCodecs() {
    std::map<QString, CodecPluginPointer> codecs;
    for (const QString name : { "opus", "pcm", "zlib" }) {
        codecs[name] = std::make_shared<FakeCodec>(name);
    }
    return codecs;
}

}

void AudioCodecNegotiationTests::testParseDefault() {
    QCOMPARE(parsePreferenceOrder("opus,zlib,pcm"), (QStringList { "opus", "zlib", "pcm" }));
}

void AudioCodecNegotiationTests::testParseTrimsSpaces() {
    // The settings page placeholder is "opus, zlib, pcm".
    QCOMPARE(parsePreferenceOrder("opus, zlib, pcm"), (QStringList { "opus", "zlib", "pcm" }));
    QCOMPARE(parsePreferenceOrder(" opus\t,  pcm "), (QStringList { "opus", "pcm" }));
}

void AudioCodecNegotiationTests::testParseSkipsEmpty() {
    QCOMPARE(parsePreferenceOrder("opus,,pcm,"), (QStringList { "opus", "pcm" }));
    QCOMPARE(parsePreferenceOrder(", opus , ,pcm"), (QStringList { "opus", "pcm" }));
    QVERIFY(parsePreferenceOrder("").isEmpty());
    QVERIFY(parsePreferenceOrder(" , ,").isEmpty());
}

void AudioCodecNegotiationTests::testNegotiateSpacedSettingForNonOpusClient() {
    const auto available = availableCodecs();
    const QStringList order = parsePreferenceOrder("opus, zlib, pcm");

    auto selected = negotiate({ "pcm" }, available, order);
    QCOMPARE(selected.first, QString("pcm"));
    QVERIFY(selected.second == available.at("pcm"));

    selected = negotiate({ "pcm", "zlib" }, available, order);
    QCOMPARE(selected.first, QString("zlib"));
    QVERIFY(selected.second == available.at("zlib"));
}

void AudioCodecNegotiationTests::testNegotiatePrefersSettingOrder() {
    const auto available = availableCodecs();

    auto selected = negotiate({ "pcm", "zlib", "opus" }, available, parsePreferenceOrder("opus,zlib,pcm"));
    QCOMPARE(selected.first, QString("opus"));
    QVERIFY(selected.second == available.at("opus"));

    selected = negotiate({ "opus", "zlib", "pcm" }, available, parsePreferenceOrder("pcm, opus"));
    QCOMPARE(selected.first, QString("pcm"));
    QVERIFY(selected.second == available.at("pcm"));
}

void AudioCodecNegotiationTests::testNegotiateNoMatchLeavesMapUnchanged() {
    const auto available = availableCodecs();
    const QStringList order = parsePreferenceOrder("opus,zlib,pcm");

    // Nothing offered, unknown names (names are case-sensitive), and an empty name.
    const std::vector<std::vector<QString>> offers { {}, { "flac" }, { "PCM" }, { "" } };
    for (const auto& offer : offers) {
        auto selected = negotiate(offer, available, order);
        QVERIFY(selected.first.isEmpty());
        QVERIFY(!selected.second);
    }
    // An available codec that the preference order leaves out.
    auto selected = negotiate({ "zlib" }, available, parsePreferenceOrder("opus, pcm"));
    QVERIFY(selected.first.isEmpty());
    QVERIFY(!selected.second);

    QCOMPARE(available.size(), (size_t)3);
    QVERIFY(available.find("") == available.end());
}
