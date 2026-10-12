//
//  SplatSorter.h
//  libraries/splat/src/splat
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_SplatSorter_h
#define overte_SplatSorter_h

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace splat {

// The view a sort is computed for.
struct SortView {
    // Splat-local space to eye space (right-handed, looking down -z), including any scale.
    glm::mat4 modelView;
    // Eye-space distance in front of which splats are dropped.
    float nearClip { 0.0f };
    // Tangent of the half field of view per axis, already widened: splats whose centre lies outside are dropped.
    glm::vec2 tanHalfFov { 1.0f };
};

// Back-to-front order of the splats inside a view, for one splat entity.
//
// Each sort runs on the global thread pool in two passes: a parallel pass computes every splat's eye depth and drops
// those behind the near plane or outside the widened frustum, then an LSD radix sort (3 passes of 11 bits) orders the
// survivors by depth as 32-bit keys. The result goes into the back half of a double buffer; the render thread picks
// it up with takeResult(). One sort is in flight at a time.
class SplatSorter {
public:
    using Positions = std::shared_ptr<const std::vector<glm::vec3>>;

    explicit SplatSorter(const Positions& positions);
    ~SplatSorter();

    SplatSorter(const SplatSorter&) = delete;
    SplatSorter& operator=(const SplatSorter&) = delete;

    // True while a sort runs or its result waits for takeResult(); sort() is ignored meanwhile.
    bool isBusy() const;

    // Starts a sort for view on the global thread pool.
    void sort(const SortView& view);

    // When a sort has finished, makes its result the front buffer and returns it: splat indices, farthest first.
    // Returns nullptr otherwise. The pointer stays valid until the next call that returns non-null.
    const std::vector<uint32_t>* takeResult();

    // The synchronous core, exposed for tests. scratch is reused between calls to avoid reallocating.
    struct Scratch {
        std::vector<uint32_t> keysA;
        std::vector<uint32_t> keysB;
        std::vector<uint32_t> indicesA;
        std::vector<uint32_t> indicesB;
    };
    static void sortIndices(const std::vector<glm::vec3>& positions, const SortView& view, Scratch& scratch,
                            std::vector<uint32_t>& out, const std::atomic<bool>* abandoned = nullptr);

private:
    struct State;
    std::shared_ptr<State> _state;
};

} // namespace splat

#endif // overte_SplatSorter_h
