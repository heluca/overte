//
//  VKShaderCompiler.cpp
//  libraries/gpu-vk/src/gpu/vk
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
#include "VKShaderCompiler.h"

#include <mutex>

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

namespace {

EShLanguage toGlslangStage(VkShaderStageFlagBits stage) {
    switch (stage) {
        case VK_SHADER_STAGE_VERTEX_BIT:
            return EShLangVertex;
        case VK_SHADER_STAGE_GEOMETRY_BIT:
            return EShLangGeometry;
        case VK_SHADER_STAGE_COMPUTE_BIT:
            return EShLangCompute;
        case VK_SHADER_STAGE_FRAGMENT_BIT:
        default:
            return EShLangFragment;
    }
}

}  // namespace

bool gpu::vk::compileGlslToSpirv(const std::string& glsl, VkShaderStageFlagBits stage, std::vector<uint32_t>& spirv, std::string& log) {
    // glslang keeps process-wide pool allocators; initialise them once, never finalize (the backend lives as long as the process)
    static std::once_flag glslangInitialized;
    std::call_once(glslangInitialized, [] { glslang::InitializeProcess(); });

    const EShLanguage language = toGlslangStage(stage);
    glslang::TShader shader(language);
    const char* text = glsl.c_str();
    shader.setStrings(&text, 1);
    // The same environment as `glslangValidator -V100`: it predefines VULKAN, which the dialect header keys its layouts on
    shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

    const EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
    const int defaultVersion = 450;
    if (!shader.parse(GetDefaultResources(), defaultVersion, false, messages)) {
        log = shader.getInfoLog();
        return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        log = program.getInfoLog();
        return false;
    }

    spirv.clear();
    glslang::GlslangToSpv(*program.getIntermediate(language), spirv);
    log = shader.getInfoLog();
    return !spirv.empty();
}
