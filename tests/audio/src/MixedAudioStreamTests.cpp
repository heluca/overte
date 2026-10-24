//
//  MixedAudioStreamTests.cpp
//  tests/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "MixedAudioStreamTests.h"

#include <vector>

#include <AudioConstants.h>
#include <MixedAudioStream.h>
#include <NLPacket.h>
#include <ReceivedMessage.h>

QTEST_MAIN(MixedAudioStreamTests)

// What the Agent reads from its received stream: what the mixer sends each listener, 24 kHz stereo in 10 ms packets.
// The pcm codec is pass-through, as no codec at all is, so these tests need no codec plugin.

namespace {

const int FRAME_SAMPLES = AudioConstants::NETWORK_FRAME_SAMPLES_STEREO;
const int CAPACITY_FRAMES = 10;  // as the Agent's
const NLPacket::LocalID MIXER_LOCAL_ID = 7;

// As AudioMixerWorker's createAudioPacket, sendMixPacket and sendSilentPacket build them
QSharedPointer<ReceivedMessage> mixedPacket(quint16 sequence, int16_t value) {
    auto packet = NLPacket::create(PacketType::MixedAudio);
    packet->writePrimitive(sequence);
    packet->writeString(QString("pcm"));
    std::vector<int16_t> samples(FRAME_SAMPLES, value);
    packet->write(reinterpret_cast<const char*>(samples.data()), FRAME_SAMPLES * AudioConstants::SAMPLE_SIZE);
    packet->writeSourceID(MIXER_LOCAL_ID);  // the stream resets its sequence numbers when the sender changes
    packet->seek(0);  // ReceivedMessage takes the payload from the read position
    return QSharedPointer<ReceivedMessage>::create(*packet);
}

QSharedPointer<ReceivedMessage> silentPacket(quint16 sequence) {
    auto packet = NLPacket::create(PacketType::SilentAudioFrame);
    packet->writePrimitive(sequence);
    packet->writeString(QString("pcm"));
    packet->writePrimitive((quint16)AudioConstants::NETWORK_FRAME_SAMPLES_STEREO);
    packet->writeSourceID(MIXER_LOCAL_ID);  // the stream resets its sequence numbers when the sender changes
    packet->seek(0);  // ReceivedMessage takes the payload from the read position
    return QSharedPointer<ReceivedMessage>::create(*packet);
}

std::vector<int16_t> readAll(MixedAudioStream& stream) {
    std::vector<int16_t> all;
    int16_t samples[FRAME_SAMPLES];
    int read;
    while ((read = stream.readAvailableSamples(samples, FRAME_SAMPLES)) > 0) {
        all.insert(all.end(), samples, samples + read);
    }
    return all;
}

bool allEqual(const std::vector<int16_t>& samples, size_t from, size_t to, int16_t value) {
    for (size_t i = from; i < to; i++) {
        if (samples[i] != value) {
            return false;
        }
    }
    return true;
}

}  // namespace

void MixedAudioStreamTests::testMixedPacketIsOneFrame() {
    MixedAudioStream stream(CAPACITY_FRAMES, CAPACITY_FRAMES);
    for (quint16 sequence = 0; sequence < 5; sequence++) {
        stream.parseData(*mixedPacket(sequence, 1000 + sequence));
        const auto samples = readAll(stream);
        QCOMPARE((int)samples.size(), FRAME_SAMPLES);
        QVERIFY(allEqual(samples, 0, samples.size(), 1000 + sequence));
        stream.clearBuffer();
    }
}

void MixedAudioStreamTests::testSilentPacketLeavesTwentyMilliseconds() {
    // The stream counts a silent packet's stereo sample count once per channel, so a 10 ms packet leaves 20 ms of
    // zeros. Agent::handleAudioPacket therefore writes one silent frame itself instead of reading these.
    MixedAudioStream stream(CAPACITY_FRAMES, CAPACITY_FRAMES);
    stream.parseData(*mixedPacket(0, 1000));
    QCOMPARE((int)readAll(stream).size(), FRAME_SAMPLES);
    stream.clearBuffer();

    stream.parseData(*silentPacket(1));
    const auto samples = readAll(stream);
    QCOMPARE((int)samples.size(), 2 * FRAME_SAMPLES);
    QVERIFY(allEqual(samples, 0, samples.size(), 0));
}

void MixedAudioStreamTests::testSequenceGapAddsConcealedFrames() {
    MixedAudioStream stream(CAPACITY_FRAMES, CAPACITY_FRAMES);
    stream.parseData(*mixedPacket(0, 1000));
    readAll(stream);
    stream.clearBuffer();

    // Packets 1 and 2 are lost: one concealed frame for each comes before packet 3's frame (zeros without a decoder)
    stream.parseData(*mixedPacket(3, 1003));
    const auto samples = readAll(stream);
    QCOMPARE((int)samples.size(), 3 * FRAME_SAMPLES);
    QVERIFY(allEqual(samples, 0, 2 * FRAME_SAMPLES, 0));
    QVERIFY(allEqual(samples, 2 * FRAME_SAMPLES, samples.size(), 1003));
    stream.clearBuffer();

    // A late packet is ignored, so nothing is read twice
    stream.parseData(*mixedPacket(2, 1002));
    QCOMPARE((int)readAll(stream).size(), 0);
}

void MixedAudioStreamTests::testReadIgnoresJitterBuffer() {
    // popSamples holds frames back while the jitter buffer refills, and conceals a frame into the ring when it runs dry.
    // The Agent clears the ring after every packet, so it reads with readAvailableSamples, which takes what is there.
    MixedAudioStream stream(CAPACITY_FRAMES, CAPACITY_FRAMES);
    stream.parseData(*mixedPacket(0, 2000));
    QVERIFY(stream.isStarved());
    QCOMPARE(stream.popSamples(FRAME_SAMPLES, true), 0);
    QCOMPARE(stream.getSamplesAvailable(), FRAME_SAMPLES);

    int16_t samples[FRAME_SAMPLES];
    QCOMPARE(stream.readAvailableSamples(samples, FRAME_SAMPLES), FRAME_SAMPLES);
    QCOMPARE(samples[0], (int16_t)2000);
    QCOMPARE(samples[FRAME_SAMPLES - 1], (int16_t)2000);
    QCOMPARE(stream.readAvailableSamples(samples, FRAME_SAMPLES), 0);
    QCOMPARE(stream.getSamplesAvailable(), 0);
}
