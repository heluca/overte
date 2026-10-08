//
//  SplatCloud.h
//  libraries/splat/src/splat
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_SplatCloud_h
#define overte_SplatCloud_h

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <AABox.h>

namespace splat {

// Zeroth-order spherical harmonic basis constant, 1 / (2 * sqrt(pi)).
constexpr float SH_C0 = 0.28209479177387814f;

// Fraction of splats trimmed from each end of every axis when computing the natural bounds,
// so stray "floaters" do not inflate the bound used for scale-to-fit, culling and picking.
constexpr float BOUNDS_TRIM_LOW = 0.005f;
constexpr float BOUNDS_TRIM_HIGH = 0.995f;

// The GPU payload of one splat, two uvec4 texels (32 B). The shader reads it from a resource buffer.
//   texel0 = (floatBits(pos.x), floatBits(pos.y), floatBits(pos.z), rgba8)
//            rgba8 is glm::packUnorm4x8: r in bits 0-7, g 8-15, b 16-23, a 24-31.
//   texel1 = (half2(scale.x, scale.y), half2(scale.z, 1), half2(rot.w, rot.x), half2(rot.y, rot.z))
//            each half2 is glm::packHalf2x16: the first component in bits 0-15, the second in bits 16-31.
//            scale is the linear standard deviation per local axis; rot is the unit quaternion with w >= 0.
//            The shader builds Sigma = R * S * S^T * R^T. Storing scale rather than Sigma keeps small splats exact:
//            s^2 underflows a half below s ~ 2.5e-4, s itself stays normal down to ~6e-5.
//            The half after scale.z is reserved (degree-0 SH or a flags field) and holds 1.0 (0x3C00): a zero high
//            half would make the word a float32 denormal, which the RGBA32F texture-buffer fetch on the GL 4.1 and
//            GLES paths may flush to zero, losing scale.z.
// Positions, scale and rotation are in the splat's local frame, already converted to Overte axes (see PlyParser.h).
struct PackedSplat {
    uint32_t texel0[4];
    uint32_t texel1[4];
};
static_assert(sizeof(PackedSplat) == 32, "PackedSplat must stay 32 bytes, the shader indexes it as 2 x uvec4");
static_assert(sizeof(glm::vec3) == 12, "SplatCloud::positions is consumed as a packed float3 array");

// Normalises the quaternion (w, x, y, z) and flips it to w >= 0. A zero or non-finite quaternion becomes identity.
glm::quat normalizedRotation(float w, float x, float y, float z);

// rotation must already be normalised (normalizedRotation).
PackedSplat packSplat(const glm::vec3& position, const glm::vec3& color, float alpha, const glm::vec3& scale,
                      const glm::quat& rotation);

// A decoded splat cloud, independent of file format and of the GPU.
class SplatCloud {
public:
    using Pointer = std::shared_ptr<SplatCloud>;

    size_t count() const { return positions.size(); }

    void reserve(size_t count);

    // color and alpha are clamped to [0, 1]; rotation must already be normalised (normalizedRotation).
    void append(const glm::vec3& position, const glm::vec3& color, float alpha, const glm::vec3& scale,
                const glm::quat& rotation);

    // Fills rawBounds and naturalBounds from positions. Call once after the last append.
    void computeBounds();

    // Splat centres for the depth sort, parallel to packed.
    std::vector<glm::vec3> positions;
    std::vector<PackedSplat> packed;

    // Min/max of the splat centres.
    AABox rawBounds;
    // Splat centres trimmed to the BOUNDS_TRIM_LOW..BOUNDS_TRIM_HIGH percentile per axis.
    AABox naturalBounds;
};

} // namespace splat

#endif // overte_SplatCloud_h
