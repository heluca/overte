//
//  VKShaderCompiler.h
//  libraries/gpu-vk/src/gpu/vk
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace gpu { namespace vk {

// Compiles the shader library's glsl450 dialect text to SPIR-V with glslang, under the same rules the build
// uses for the shaders it ships (glslangValidator -V100: Vulkan 1.0 client, SPIR-V 1.0, VULKAN defined).
// Procedural shaders arrive as GLSL from a domain at runtime, so they cannot be compiled ahead of time.
// Returns false and fills `log` when the shader does not compile; `log` carries any warnings on success.
bool compileGlslToSpirv(const std::string& glsl, VkShaderStageFlagBits stage, std::vector<uint32_t>& spirv, std::string& log);

} }  // namespace gpu::vk
