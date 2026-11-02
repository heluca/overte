//
//  ToneMapAndResampleTask.cpp
//  libraries/render-utils/src
//
//  Created by Anna Brewer on 7/3/19.
//  Copyright 2019 High Fidelity, Inc.
//  Copyright 2024 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "ToneMapAndResampleTask.h"

#include <gpu/Context.h>
#include <shaders/Shaders.h>

#include "render-utils/ShaderConstants.h"
#include "StencilMaskPass.h"
#include "FramebufferCache.h"

using namespace render;
using namespace shader::gpu::program;
using namespace shader::render_utils::program;

gpu::PipelinePointer ToneMapAndResample::_pipeline;
gpu::PipelinePointer ToneMapAndResample::_mirrorPipeline;

ToneMapAndResample::ToneMapAndResample() {
    Parameters parameters;
    _parametersBuffer = gpu::BufferView(std::make_shared<gpu::Buffer>(gpu::Buffer::UniformBuffer, sizeof(Parameters), (const gpu::Byte*) &parameters));
    _noAdaptation = gpu::Texture::createRenderBuffer(gpu::Element(gpu::SCALAR, gpu::FLOAT, gpu::RED), 1, 1, gpu::Texture::SINGLE_MIP,
                                                     Sampler(Sampler::FILTER_MIN_MAG_POINT, Sampler::WRAP_CLAMP));
}

void ToneMapAndResample::init() {
    // shared_ptr to gpu::State
    gpu::StatePointer blitState = std::make_shared<gpu::State>();

    blitState->setDepthTest(gpu::State::DepthTest(false, false));
    blitState->setColorWriteMask(true, true, true, true);

    _pipeline = gpu::PipelinePointer(gpu::Pipeline::create(gpu::Shader::createProgram(toneMapping), blitState));
    _mirrorPipeline = gpu::PipelinePointer(gpu::Pipeline::create(gpu::Shader::createProgram(toneMapping_mirrored), blitState));
}

void ToneMapAndResample::setExposure(float exposure) {
    if (_exposure != exposure) {
        _exposure = exposure;
        _parametersBuffer.edit<Parameters>()._exposure = exposure;
    }
}

void ToneMapAndResample::setAutoExposure(const AutoExposureResultPointer& autoExposure) {
    const auto& params = _parametersBuffer.get<Parameters>();
    const int enabled = autoExposure ? 1 : 0;
    const float keyLog2 = autoExposure ? autoExposure->keyLog2 : 0.0f;
    const float rangeUp = autoExposure ? autoExposure->rangeUp : 0.0f;
    const float rangeDown = autoExposure ? autoExposure->rangeDown : 0.0f;
    const int debug = (autoExposure && autoExposure->debug) ? 1 : 0;
    if (params._autoEnabled != enabled || params._autoKeyLog2 != keyLog2 || params._autoRangeUp != rangeUp ||
        params._autoRangeDown != rangeDown || params._autoDebug != debug) {
        auto& edit = _parametersBuffer.edit<Parameters>();
        edit._autoEnabled = enabled;
        edit._autoKeyLog2 = keyLog2;
        edit._autoRangeUp = rangeUp;
        edit._autoRangeDown = rangeDown;
        edit._autoDebug = debug;
    }
}

void ToneMapAndResample::setCurve(TonemappingCurve curve) {
    auto& params = _parametersBuffer.get<Parameters>();
    if (params._toneCurve != (int)curve) {
        _parametersBuffer.edit<Parameters>()._toneCurve = (int)curve;
    }
}

void ToneMapAndResample::configure(const Config& config) {
    _debug = config.debug;
    _debugExposure = config.exposure;
    _debugCurve = (TonemappingCurve)config.curve;
}

void ToneMapAndResample::run(const RenderContextPointer& renderContext, const Input& input, Output& output) {
    assert(renderContext->args);
    assert(renderContext->args->hasViewFrustum());

    RenderArgs* args = renderContext->args;

    auto lightingBuffer = input.get0()->getRenderBuffer(0);
    auto destinationFramebuffer = input.get1();
    const auto tonemappingFrame = input.get2();
    const auto autoExposure = input.get3();

    const auto& tonemappingStage = renderContext->_scene->getStage<TonemappingStage>();
    graphics::TonemappingPointer tonemapping;
    if (tonemappingStage && tonemappingFrame->_elements.size()) {
        tonemapping = tonemappingStage->getElement(tonemappingFrame->_elements.front());
    }

    if (args->_mirrorDepth > 0) {
        setCurve(TonemappingCurve::SRGB);
        setExposure(0.0f);
        setAutoExposure(nullptr);
    } else if (_debug) {
        setCurve(_debugCurve);
        setExposure(_debugExposure);
    } else if (tonemapping) {
        setCurve(tonemapping->getCurve());
        setExposure(tonemapping->getExposure());
    }
    if (args->_mirrorDepth == 0) {
        setAutoExposure(autoExposure);
    }

    if (!destinationFramebuffer) {
        destinationFramebuffer = args->_blitFramebuffer;
    }

    if (!lightingBuffer || !destinationFramebuffer) {
        return;
    }

    if (!_pipeline) {
        init();
    }

    const auto bufferSize = destinationFramebuffer->getSize();

    auto srcBufferSize = glm::ivec2(lightingBuffer->getDimensions());

    glm::ivec4 destViewport{ 0, 0, bufferSize.x, bufferSize.y };

    gpu::doInBatch("Resample::run", args->_context, [&](gpu::Batch& batch) {
        batch.enableStereo(false);
        batch.setFramebuffer(destinationFramebuffer);

        batch.setViewportTransform(destViewport);
        batch.setProjectionTransform(glm::mat4());
        batch.resetViewTransform();
        bool shouldMirror = args->_numMirrorFlips >= (args->_renderMode != RenderArgs::MIRROR_RENDER_MODE ? 1 : 0);
        batch.setPipeline(shouldMirror ? _mirrorPipeline : _pipeline);

        batch.setModelTransform(gpu::Framebuffer::evalSubregionTexcoordTransform(srcBufferSize, args->_viewport));
        batch.setUniformBuffer(render_utils::slot::buffer::ToneMappingParams, _parametersBuffer);
        batch.setResourceTexture(render_utils::slot::texture::ToneMappingColor, lightingBuffer);
        batch.setResourceTexture(render_utils::slot::texture::ToneMappingAdaptation,
                                 (autoExposure && autoExposure->adaptation) ? autoExposure->adaptation : _noAdaptation);
        batch.draw(gpu::TRIANGLE_STRIP, 4);
        batch.setResourceTexture(render_utils::slot::texture::ToneMappingAdaptation, nullptr);
    });

    // Set full final viewport
    args->_viewport = destViewport;

    output = destinationFramebuffer;
}
