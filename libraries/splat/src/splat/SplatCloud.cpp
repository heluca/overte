//
//  SplatCloud.cpp
//  libraries/splat/src/splat
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "SplatCloud.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/packing.hpp>

namespace splat {

namespace {

// Largest finite half-float. glm::packHalf2x16 turns anything larger into infinity.
constexpr float HALF_MAX = 65504.0f;

uint32_t packHalf2(float a, float b) {
    return glm::packHalf2x16(glm::clamp(glm::vec2(a, b), glm::vec2(-HALF_MAX), glm::vec2(HALF_MAX)));
}

} // namespace

glm::quat normalizedRotation(float w, float x, float y, float z) {
    // Members are assigned by name: the glm::quat constructor's argument order depends on GLM_FORCE_QUAT_DATA_XYZW.
    glm::quat q;
    float length = std::sqrt(w * w + x * x + y * y + z * z);
    if (!(length > 0.0f) || !std::isfinite(length)) {
        q.w = 1.0f;
        q.x = q.y = q.z = 0.0f;
        return q;
    }
    // q and -q are the same rotation; one sign makes the packed form unique.
    float sign = w < 0.0f ? -1.0f : 1.0f;
    q.w = sign * w / length;
    q.x = sign * x / length;
    q.y = sign * y / length;
    q.z = sign * z / length;
    return q;
}

PackedSplat packSplat(const glm::vec3& position, const glm::vec3& color, float alpha, const glm::vec3& scale,
                      const glm::quat& rotation) {
    PackedSplat p;
    p.texel0[0] = glm::floatBitsToUint(position.x);
    p.texel0[1] = glm::floatBitsToUint(position.y);
    p.texel0[2] = glm::floatBitsToUint(position.z);
    p.texel0[3] = glm::packUnorm4x8(glm::clamp(glm::vec4(color, alpha), 0.0f, 1.0f));
    p.texel1[0] = packHalf2(scale.x, scale.y);
    p.texel1[1] = packHalf2(scale.z, 0.0f);
    p.texel1[2] = packHalf2(rotation.w, rotation.x);
    p.texel1[3] = packHalf2(rotation.y, rotation.z);
    return p;
}

void SplatCloud::reserve(size_t count) {
    positions.reserve(count);
    packed.reserve(count);
}

void SplatCloud::append(const glm::vec3& position, const glm::vec3& color, float alpha, const glm::vec3& scale,
                        const glm::quat& rotation) {
    positions.push_back(position);
    packed.push_back(packSplat(position, color, alpha, scale, rotation));
}

void SplatCloud::computeBounds() {
    const size_t n = positions.size();
    if (n == 0) {
        rawBounds = AABox();
        naturalBounds = AABox();
        return;
    }

    glm::vec3 rawMin = positions[0];
    glm::vec3 rawMax = positions[0];
    for (const auto& p : positions) {
        rawMin = glm::min(rawMin, p);
        rawMax = glm::max(rawMax, p);
    }
    rawBounds = AABox(rawMin, rawMax - rawMin);

    const size_t lowIndex = (size_t)std::floor(BOUNDS_TRIM_LOW * (float)(n - 1));
    const size_t highIndex = (size_t)std::ceil(BOUNDS_TRIM_HIGH * (float)(n - 1));
    glm::vec3 trimmedMin;
    glm::vec3 trimmedMax;
    std::vector<float> axis(n);
    for (int a = 0; a < 3; a++) {
        for (size_t i = 0; i < n; i++) {
            axis[i] = positions[i][a];
        }
        std::nth_element(axis.begin(), axis.begin() + highIndex, axis.end());
        trimmedMax[a] = axis[highIndex];
        std::nth_element(axis.begin(), axis.begin() + lowIndex, axis.begin() + highIndex);
        trimmedMin[a] = axis[lowIndex];
    }
    naturalBounds = AABox(trimmedMin, trimmedMax - trimmedMin);
}

} // namespace splat
