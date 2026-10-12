//
//  SplatSorterTests.cpp
//  tests/splat/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "SplatSorterTests.h"

#include <cmath>
#include <random>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include <splat/SplatSorter.h>

QTEST_GUILESS_MAIN(SplatSorterTests)

void SplatSorterTests::testBackToFrontAndCulling() {
    const std::vector<glm::vec3> positions = {
        { 0.0f, 0.0f, -2.0f },   // 0
        { 0.0f, 0.0f, 1.0f },    // 1: behind the eye
        { 0.5f, 0.5f, -5.0f },   // 2
        { 100.0f, 0.0f, -1.0f }, // 3: outside the field of view
        { 0.0f, 0.0f, -0.05f },  // 4: in front of the near plane
        { 0.0f, -0.5f, -3.0f },  // 5
        { 0.0f, 0.0f, -1.0f },   // 6
    };
    splat::SortView view;
    view.modelView = glm::mat4(1.0f);
    view.nearClip = 0.1f;
    view.tanHalfFov = glm::vec2(1.0f);

    splat::SplatSorter::Scratch scratch;
    std::vector<uint32_t> out;
    splat::SplatSorter::sortIndices(positions, view, scratch, out);
    QVERIFY(out == (std::vector<uint32_t>{ 2, 5, 0, 6 }));

    // The model-view matrix applies: moving the eye to z = -10 looking down +z (a half turn about y) reverses the order
    // and brings the splat at z = +1 into view.
    view.modelView = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
                     glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 10.0f));
    splat::SplatSorter::sortIndices(positions, view, scratch, out);
    QVERIFY(out == (std::vector<uint32_t>{ 1, 4, 6, 0, 5, 2 }));
}

void SplatSorterTests::testMatchesReferenceSort() {
    std::mt19937 random(1234);
    std::uniform_real_distribution<float> coordinate(-50.0f, 50.0f);
    std::vector<glm::vec3> positions(100000);
    for (auto& p : positions) {
        p = glm::vec3(coordinate(random), coordinate(random), coordinate(random));
    }
    splat::SortView view;
    view.modelView = glm::lookAt(glm::vec3(3.0f, 2.0f, 60.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    view.nearClip = 0.5f;
    view.tanHalfFov = glm::vec2(0.8f, 0.6f);

    std::vector<float> depths(positions.size());
    size_t expectedCount = 0;
    for (size_t i = 0; i < positions.size(); i++) {
        glm::vec4 eye = view.modelView * glm::vec4(positions[i], 1.0f);
        depths[i] = -eye.z;
        if (depths[i] > view.nearClip && std::fabs(eye.x) <= depths[i] * view.tanHalfFov.x &&
            std::fabs(eye.y) <= depths[i] * view.tanHalfFov.y) {
            expectedCount++;
        }
    }
    QVERIFY(expectedCount > 1000 && expectedCount < positions.size());

    splat::SplatSorter::Scratch scratch;
    std::vector<uint32_t> out;
    splat::SplatSorter::sortIndices(positions, view, scratch, out);
    // The sorter evaluates the matrix rows in its own order, so a splat exactly on a frustum plane may round the
    // other way.
    QVERIFY2(out.size() + 2 >= expectedCount && out.size() <= expectedCount + 2,
             qPrintable(QString("%1 sorted, %2 expected").arg(out.size()).arg(expectedCount)));

    const float DEPTH_EPSILON = 1e-4f;
    for (size_t i = 1; i < out.size(); i++) {
        QVERIFY2(depths[out[i - 1]] + DEPTH_EPSILON >= depths[out[i]], qPrintable(QString("order breaks at %1").arg(i)));
    }
}
