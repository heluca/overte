//
//  SplatSorter.cpp
//  libraries/splat/src/splat
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "SplatSorter.h"

#include <cmath>

#include <QtConcurrent/QtConcurrentRun>

#include <Profile.h>
#include <TBBHelpers.h>

namespace splat {

namespace {

constexpr uint32_t CULLED_KEY = 0xFFFFFFFFu;
constexpr int RADIX_BITS = 11;
constexpr int RADIX_PASSES = 3;
constexpr uint32_t RADIX_SIZE = 1u << RADIX_BITS;
constexpr uint32_t RADIX_MASK = RADIX_SIZE - 1;
constexpr size_t KEY_GRAIN = 16384;

using Histograms = std::vector<uint32_t>;  // RADIX_PASSES x RADIX_SIZE

// One stable counting pass on the digit at shift. offsets holds the exclusive prefix sums for that digit and is
// consumed. keysOut may be null on the last pass, where only the order is needed.
void scatter(const uint32_t* keysIn, const uint32_t* indicesIn, uint32_t* keysOut, uint32_t* indicesOut, size_t count,
             int shift, uint32_t* offsets) {
    for (size_t i = 0; i < count; i++) {
        const uint32_t key = keysIn[i];
        const uint32_t slot = offsets[(key >> shift) & RADIX_MASK]++;
        if (keysOut) {
            keysOut[slot] = key;
        }
        indicesOut[slot] = indicesIn[i];
    }
}

} // namespace

struct SplatSorter::State {
    Positions positions;
    std::vector<uint32_t> buffers[2];
    // Touched by the render thread only. The job writes the other buffer, chosen when it is launched.
    int front { 0 };
    // Touched by the job only.
    Scratch scratch;
    std::atomic<bool> busy { false };
    std::atomic<bool> ready { false };
    std::atomic<bool> abandoned { false };
};

SplatSorter::SplatSorter(const Positions& positions) : _state(std::make_shared<State>()) {
    _state->positions = positions;
}

SplatSorter::~SplatSorter() {
    // A running job holds its own reference to the state and frees it when it returns.
    _state->abandoned = true;
}

bool SplatSorter::isBusy() const {
    return _state->busy || _state->ready;
}

void SplatSorter::sort(const SortView& view) {
    if (isBusy() || !_state->positions) {
        return;
    }
    _state->busy = true;
    auto state = _state;
    const int back = 1 - state->front;
    QtConcurrent::run([state, view, back] {
        sortIndices(*state->positions, view, state->scratch, state->buffers[back], &state->abandoned);
        if (!state->abandoned) {
            state->ready = true;
        }
        state->busy = false;
    });
}

const std::vector<uint32_t>* SplatSorter::takeResult() {
    if (!_state->ready) {
        return nullptr;
    }
    _state->front = 1 - _state->front;
    _state->ready = false;
    return &_state->buffers[_state->front];
}

void SplatSorter::sortIndices(const std::vector<glm::vec3>& positions, const SortView& view, Scratch& scratch,
                              std::vector<uint32_t>& out, const std::atomic<bool>* abandoned) {
    PROFILE_RANGE(render, "SplatSorter::sortIndices");
    auto isAbandoned = [abandoned] { return abandoned && abandoned->load(); };

    const size_t count = positions.size();
    scratch.keysB.resize(count);

    // Rows of the model-view matrix: only eye x, y and z are needed.
    const glm::mat4& m = view.modelView;
    const glm::vec4 rowX(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 rowY(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 rowZ(m[0][2], m[1][2], m[2][2], m[3][2]);
    const float nearClip = view.nearClip;
    const glm::vec2 tanHalfFov = view.tanHalfFov;
    uint32_t* allKeys = scratch.keysB.data();
    tbb::parallel_for(tbb::blocked_range<size_t>(0, count, KEY_GRAIN), [&](const tbb::blocked_range<size_t>& range) {
        for (size_t i = range.begin(); i < range.end(); i++) {
            const glm::vec4 p(positions[i], 1.0f);
            const float depth = -glm::dot(rowZ, p);
            const bool visible = depth > nearClip && std::fabs(glm::dot(rowX, p)) <= depth * tanHalfFov.x &&
                                 std::fabs(glm::dot(rowY, p)) <= depth * tanHalfFov.y;
            // depth > 0 here, so its float bits order like the depth itself; inverting them puts the farthest first.
            allKeys[i] = visible ? ~glm::floatBitsToUint(depth) : CULLED_KEY;
        }
    });
    if (isAbandoned()) {
        return;
    }

    // Compact the visible splats and count all three digits in the same walk.
    scratch.keysA.resize(count);
    scratch.indicesA.resize(count);
    scratch.indicesB.resize(count);
    Histograms histograms(RADIX_PASSES * RADIX_SIZE, 0);
    size_t visibleCount = 0;
    for (size_t i = 0; i < count; i++) {
        const uint32_t key = allKeys[i];
        if (key == CULLED_KEY) {
            continue;
        }
        scratch.keysA[visibleCount] = key;
        scratch.indicesA[visibleCount] = (uint32_t)i;
        visibleCount++;
        for (int pass = 0; pass < RADIX_PASSES; pass++) {
            histograms[pass * RADIX_SIZE + ((key >> (pass * RADIX_BITS)) & RADIX_MASK)]++;
        }
    }
    for (int pass = 0; pass < RADIX_PASSES; pass++) {
        uint32_t sum = 0;
        for (uint32_t digit = 0; digit < RADIX_SIZE; digit++) {
            uint32_t& bucket = histograms[pass * RADIX_SIZE + digit];
            const uint32_t bucketCount = bucket;
            bucket = sum;
            sum += bucketCount;
        }
    }

    // A -> B -> A -> out. keysB held every splat's key; it is free once compacted.
    out.resize(visibleCount);
    scatter(scratch.keysA.data(), scratch.indicesA.data(), scratch.keysB.data(), scratch.indicesB.data(), visibleCount,
            0, &histograms[0]);
    if (isAbandoned()) {
        return;
    }
    scatter(scratch.keysB.data(), scratch.indicesB.data(), scratch.keysA.data(), scratch.indicesA.data(), visibleCount,
            RADIX_BITS, &histograms[RADIX_SIZE]);
    if (isAbandoned()) {
        return;
    }
    scatter(scratch.keysA.data(), scratch.indicesA.data(), nullptr, out.data(), visibleCount, 2 * RADIX_BITS,
            &histograms[2 * RADIX_SIZE]);
}

} // namespace splat
