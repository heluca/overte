//
//  AutoExposure.cpp
//  libraries/render-utils/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
#include "AutoExposure.h"

#include <algorithm>
#include <cmath>

#include <gpu/Context.h>
#include <shaders/Shaders.h>

#include "render-utils/ShaderConstants.h"

namespace ru {
    using render_utils::slot::texture::Texture;
    using render_utils::slot::buffer::Buffer;
}

const int AutoExposure::LUMINANCE_SIZE = 32;
gpu::PipelinePointer AutoExposure::_luminancePipeline;
gpu::PipelinePointer AutoExposure::_adaptPipeline;

namespace {

gpu::FramebufferPointer makeFramebuffer(const std::string& name, int size) {
    auto texture = gpu::Texture::createRenderBuffer(gpu::Element(gpu::SCALAR, gpu::FLOAT, gpu::RED), size, size, gpu::Texture::SINGLE_MIP,
                                                    Sampler(Sampler::FILTER_MIN_MAG_POINT, Sampler::WRAP_CLAMP));
    auto framebuffer = gpu::FramebufferPointer(gpu::Framebuffer::create(name));
    framebuffer->setRenderBuffer(0, texture);
    return framebuffer;
}

}  // namespace

AutoExposure::AutoExposure() : _result(std::make_shared<AutoExposureResult>()) {}

void AutoExposure::configure(const Config& config) {
    _result->keyLog2 = std::log2(std::max(config.key, 0.0001f));
    _result->rangeUp = std::max(config.rangeUp, 0.0f);
    _result->rangeDown = std::max(config.rangeDown, 0.0f);
    _result->debug = config.debug;

    auto& params = _parameters.edit();
    params._speedUp = std::max(config.speedUp, 0.0f);
    params._speedDown = std::max(config.speedDown, 0.0f);
}

void AutoExposure::run(const render::RenderContextPointer& renderContext, const Inputs& inputs, Outputs& outputs) {
    assert(renderContext->args);
    RenderArgs* args = renderContext->args;

    const auto sourceFramebuffer = inputs.get0();
    const auto lightingModel = inputs.get1();

    outputs = nullptr;
    if (!sourceFramebuffer || args->_mirrorDepth > 0 || (lightingModel && !lightingModel->isAutoExposureEnabled())) {
        // Whatever was adapted before no longer applies; the next measurement starts afresh
        _hasHistory = false;
        return;
    }

    if (!_luminanceFramebuffer) {
        _luminanceFramebuffer = makeFramebuffer("autoExposureLuminance", LUMINANCE_SIZE);
        _adaptation[0] = makeFramebuffer("autoExposureAdaptation0", 1);
        _adaptation[1] = makeFramebuffer("autoExposureAdaptation1", 1);
    }
    if (!_luminancePipeline) {
        gpu::StatePointer state = std::make_shared<gpu::State>();
        state->setDepthTest(gpu::State::DepthTest(false, false));
        state->setColorWriteMask(true, true, true, true);
        _luminancePipeline = gpu::Pipeline::create(gpu::Shader::createProgram(shader::render_utils::program::autoExposure_luminance), state);
        _adaptPipeline = gpu::Pipeline::create(gpu::Shader::createProgram(shader::render_utils::program::autoExposure_adapt), state);
    }

    const auto now = std::chrono::steady_clock::now();
    float deltaTime = 0.0f;
    if (_hasHistory) {
        // A long pause (the window was hidden, say) adapts fully rather than jumping by a huge weight
        deltaTime = std::min(std::chrono::duration<float>(now - _lastRun).count(), 1.0f);
    }
    _lastRun = now;

    const auto sourceTexture = sourceFramebuffer->getRenderBuffer(0);
    const glm::ivec2 sourceSize(sourceTexture->getDimensions());
    const glm::ivec4 viewport = args->_viewport;
    {
        auto& params = _parameters.edit();
        params._footprint = glm::vec2(float(viewport.z) / float(sourceSize.x), float(viewport.w) / float(sourceSize.y)) / float(LUMINANCE_SIZE);
        params._deltaTime = deltaTime;
        params._reset = _hasHistory ? 0 : 1;
    }

    const int previous = _current;
    const int next = 1 - _current;

    gpu::doInBatch("AutoExposure::run", args->_context, [&](gpu::Batch& batch) {
        batch.enableStereo(false);
        batch.setProjectionTransform(glm::mat4());
        batch.resetViewTransform();
        batch.setUniformBuffer(ru::Buffer::AutoExposureParams, _parameters);

        if (!_hasHistory) {
            // Give the history a defined content (and image layout) before it is first read; the reset flag ignores it anyway
            batch.setFramebuffer(_adaptation[previous]);
            batch.clearColorFramebuffer(gpu::Framebuffer::BUFFER_COLOR0, glm::vec4(0.0f));
        }

        // 1. Log2 luminance of the viewport, one texel per block of it
        batch.setFramebuffer(_luminanceFramebuffer);
        batch.setViewportTransform(glm::ivec4(0, 0, LUMINANCE_SIZE, LUMINANCE_SIZE));
        batch.setModelTransform(gpu::Framebuffer::evalSubregionTexcoordTransform(sourceSize, viewport));
        batch.setPipeline(_luminancePipeline);
        batch.setResourceTexture(ru::Texture::AutoExposureSource, sourceTexture);
        batch.draw(gpu::TRIANGLE_STRIP, 4);

        // 2. Fold it into the adapted value, moving on from the previous frame's
        batch.setFramebuffer(_adaptation[next]);
        batch.setViewportTransform(glm::ivec4(0, 0, 1, 1));
        batch.setPipeline(_adaptPipeline);
        batch.setResourceTexture(ru::Texture::AutoExposureSource, _luminanceFramebuffer->getRenderBuffer(0));
        batch.setResourceTexture(ru::Texture::AutoExposurePrevious, _adaptation[previous]->getRenderBuffer(0));
        batch.draw(gpu::TRIANGLE_STRIP, 4);

        batch.setResourceTexture(ru::Texture::AutoExposureSource, nullptr);
        batch.setResourceTexture(ru::Texture::AutoExposurePrevious, nullptr);
    });

    _current = next;
    _hasHistory = true;
    _result->adaptation = _adaptation[next]->getRenderBuffer(0);
    outputs = _result;
}
