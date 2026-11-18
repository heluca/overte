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

bool gpu::vk::compileGlslToSpirv(const std::string& glsl, VkShaderStageFlagBits stage, std::vector<uint32_t>& spirv, std::string& log,
                                 shader::Reflection& reflection) {
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

    // The build reflects its shaders after spirv-opt, so the stub a runtime shader replaces may have used far
    // fewer resources than the replacement does; the pipeline layout needs what this shader actually uses.
    // All IO variables, not only the live ones: glslang keeps every declared input in the entry point interface,
    // and each of those needs a vertex attribute.
    reflection = shader::Reflection();
    program.buildReflection(EShReflectionSeparateBuffers | EShReflectionAllIOVariables);
    // A resource with no binding (a push constant block) has no place in the backend's descriptor sets, and the
    // backend declares no push constant ranges: the shader cannot run, so treat it as a compile failure.
    auto addBinding = [&](shader::Reflection::LocationMap& map, const glslang::TObjectReflection& object) {
        if (object.getBinding() < 0) {
            log = "Vulkan backend: '" + object.name + "' has no binding; push constants are not supported here";
            return false;
        }
        map[object.name] = object.getBinding();
        return true;
    };
    for (int i = 0; i < program.getNumUniformBlocks(); ++i) {
        if (!addBinding(reflection.uniformBuffers, program.getUniformBlock(i))) {
            return false;
        }
    }
    for (int i = 0; i < program.getNumBufferBlocks(); ++i) {
        if (!addBinding(reflection.resourceBuffers, program.getBufferBlock(i))) {
            return false;
        }
    }
    for (int i = 0; i < program.getNumUniformVariables(); ++i) {
        const auto& uniform = program.getUniform(i);
        // Vulkan GLSL has no loose uniforms, so a uniform outside any block is a sampler
        if (uniform.index < 0 && !addBinding(reflection.textures, uniform)) {
            return false;
        }
    }
    if (language == EShLangVertex) {
        for (int i = 0; i < program.getNumPipeInputs(); ++i) {
            const auto& input = program.getPipeInput(i);
            // Built-ins (gl_VertexIndex, gl_InstanceIndex) are not vertex attributes
            if (input.name.rfind("gl_", 0) != 0) {
                reflection.inputs[input.name] = (int32_t)input.layoutLocation();
            }
        }
    }
    reflection.updateValid();
    return !spirv.empty();
}
