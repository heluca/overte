//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//
//  Record: 32 B per splat in a resource buffer shared by every entity using the URL (splat::PackedSplat):
//    texel0 = (pos.xyz as float bits, rgba8), texel1 = (half scale.xyz, reserved half, half quaternion w x y z).
//  gaussianSplat.slv builds Sigma from scale and rotation, projects it with the EWA Jacobian and emits a 3-sigma
//  screen-space quad per instance (4-vertex triangle strip); gaussianSplat.slf blends it premultiplied, back to front.
//
//  Sort: renderSimulate takes the main-view eye into splat-local space. When it has moved more than 1% of the bound
//  radius or turned more than 2 degrees, a splat::SplatSorter job (thread pool: tbb key and cull pass, 3 x 11-bit LSD
//  radix) orders the visible splats. A later frame swaps the result in and uploads it as the per-instance index
//  stream. Identity order is drawn until the first sort lands.
//

#include "RenderableGaussianSplatEntityItem.h"

#include <cmath>
#include <mutex>
#include <numeric>

#include <DependencyManager.h>
#include <StencilMaskPass.h>
#include <ViewFrustum.h>
#include <shaders/Shaders.h>

using namespace render;
using namespace render::entities;

namespace {

const uint32_t VERTICES_PER_SPLAT = 4;
const uint32_t SPLAT_PARAMS_SLOT = 0;
const uint32_t SPLAT_DATA_SLOT = 0;
// Metres. A single splat or a planar capture has a zero-sized bound, which would divide the render scale by zero.
const float MIN_NATURAL_DIMENSION = 0.01f;
const float RESORT_DISTANCE_FRACTION = 0.01f;
const float RESORT_ANGLE_COS = std::cos(glm::radians(2.0f));
// Splats are culled by centre but their quads reach further, and the view may turn 2 degrees before the next sort.
const float SORT_FRUSTUM_MARGIN = 1.3f;

uint8_t CUSTOM_PIPELINE_NUMBER = 0;
gpu::PipelinePointer splatPipeline;
gpu::Stream::FormatPointer splatIndexFormat;

ShapePipelinePointer splatPipelineFactory(const ShapePlumber& plumber, const ShapeKey& key, RenderArgs* args) {
    if (!splatPipeline) {
        auto state = std::make_shared<gpu::State>();
        state->setCullMode(gpu::State::CULL_NONE);
        state->setDepthTest(true, false, ComparisonFunction::LESS_EQUAL);
        // Premultiplied "over", which needs the instances back to front.
        state->setBlendFunction(true, gpu::State::ONE, gpu::State::BLEND_OP_ADD, gpu::State::INV_SRC_ALPHA,
                                gpu::State::ONE, gpu::State::BLEND_OP_ADD, gpu::State::INV_SRC_ALPHA);
        PrepareStencil::testMaskResetNoAA(*state);
        auto program = gpu::Shader::createProgram(shader::entities_renderer::program::gaussianSplat);
        splatPipeline = gpu::Pipeline::create(program, state);
    }
    return std::make_shared<render::ShapePipeline>(splatPipeline, nullptr, nullptr, nullptr);
}

} // namespace

GaussianSplatEntityRenderer::GaussianSplatEntityRenderer(const EntityItemPointer& entity) : Parent(entity) {
    const glm::vec4 params(1.0f, 0.0f, 0.0f, 0.0f);
    _paramsBuffer = std::make_shared<gpu::Buffer>(gpu::Buffer::UniformBuffer, sizeof(params), (const gpu::Byte*)&params);
    _indexBuffer = std::make_shared<gpu::Buffer>(gpu::Buffer::VertexBuffer);

    static std::once_flag once;
    std::call_once(once, [] {
        CUSTOM_PIPELINE_NUMBER = render::ShapePipeline::registerCustomShapePipelineFactory(splatPipelineFactory);
        splatIndexFormat = std::make_shared<gpu::Stream::Format>();
        splatIndexFormat->setAttribute(gpu::Stream::POSITION, 0, gpu::Element(gpu::SCALAR, gpu::UINT32, gpu::RAW), 0,
                                       gpu::Stream::PER_INSTANCE);
    });
}

ItemKey GaussianSplatEntityRenderer::getKey() {
    // isTransparent() puts the item in the transparent bucket, which also keeps it out of the shadow pass.
    auto builder = ItemKey::Builder(Parent::getKey());
    builder.withSimulate();
    return builder.build();
}

ShapeKey GaussianSplatEntityRenderer::getShapeKey() {
    return ShapeKey::Builder().withCustom(CUSTOM_PIPELINE_NUMBER).withTranslucent().build();
}

void GaussianSplatEntityRenderer::doRenderUpdateSynchronousTyped(const ScenePointer& scene, Transaction& transaction, const TypedEntityPointer& entity) {
    void* key = (void*)this;
    AbstractViewStateInterface::instance()->pushPostUpdateLambda(key, [this, entity] {
        withWriteLock([&] {
            // Scale-to-fit, as Model does: the trimmed bound of the splat centres fills the entity's dimensions.
            _renderTransform = getModelTransform(); // contains parent scale, if this entity scales with its parent
            _renderTransform.postScale(entity->getUnscaledDimensions() / _naturalDimensions);
            _renderTransform.postTranslate(-_splatCenter);
            _transformReady = true;
        });
    });
}

void GaussianSplatEntityRenderer::doRenderUpdateAsynchronousTyped(const TypedEntityPointer& entity) {
    QString splatURL = entity->getSplatURL();
    if (_splatURL != splatURL) {
        _splatURL = splatURL;
        releaseSplat();
        if (!splatURL.isEmpty()) {
            // Null in tools that link entities-renderer without registering the cache.
            auto splatCache = DependencyManager::get<SplatCache>();
            if (splatCache) {
                _resource = splatCache->getSplat(QUrl(splatURL));
            }
        }
    }

    float alpha = entity->getAlpha();
    if (_alpha != alpha) {
        _alpha = alpha;
        _paramsBuffer.edit<glm::vec4>().x = alpha;
    }

    if (_resource && !_splatLoaded) {
        if (_resource->isLoaded() && _resource->dataBuffer()) {
            onSplatLoaded(entity);
        } else if (!_resource->isFailed()) {
            emit requestRenderUpdate();
        }
    }
}

void GaussianSplatEntityRenderer::onSplatLoaded(const TypedEntityPointer& entity) {
    _splatLoaded = true;
    _dataBuffer = _resource->dataBuffer();

    const uint32_t count = (uint32_t)_resource->count();
    std::vector<uint32_t> identity(count);
    std::iota(identity.begin(), identity.end(), 0u);
    // Sized once per load; sort results then overwrite a prefix in place, so the GL buffer is never re-created.
    _indexBuffer->resize(count * sizeof(uint32_t));
    _indexBuffer->setSubData(0, count * sizeof(uint32_t), reinterpret_cast<const gpu::Byte*>(identity.data()));
    // _visibleCount stays 0 until renderSimulate sees the transform rebuilt for the new bound.
    _sorter = std::make_unique<splat::SplatSorter>(_resource->positions());
    _hasSortView = false;

    const glm::vec3 naturalDimensions = glm::max(_resource->naturalBounds().getDimensions(), glm::vec3(MIN_NATURAL_DIMENSION));
    _boundRadius = 0.5f * glm::length(naturalDimensions);
    const glm::vec3 center = _resource->center();
    withWriteLock([&] {
        _naturalDimensions = naturalDimensions;
        _splatCenter = center;
        _transformReady = false;
    });
    // As with Image, only the renderer learns the natural size; the Create app reads it back from the entity.
    entity->setNaturalDimension(naturalDimensions);
    // The render transform depends on the natural bound: rebuild it.
    emit requestRenderUpdate();
}

void GaussianSplatEntityRenderer::releaseSplat() {
    _resource.reset();
    _splatLoaded = false;
    _dataBuffer.reset();
    // A sort in flight keeps its own reference to the positions and finishes harmlessly.
    _sorter.reset();
    _visibleCount = 0;
    _hasSortView = false;
    withWriteLock([&] {
        _transformReady = false;
    });
}

void GaussianSplatEntityRenderer::renderSimulate(RenderArgs* args) {
    // The secondary camera runs its own simulate pass; sorting for its view would fight the main view's order.
    if (!_sorter || args->_renderMode == RenderArgs::RenderMode::SECONDARY_CAMERA_RENDER_MODE) {
        return;
    }

    Transform renderTransform;
    bool transformReady = false;
    withReadLock([&] {
        renderTransform = _renderTransform;
        transformReady = _transformReady;
    });
    // Until then the transform is file-scale and un-centred: drawing or sorting against it would be wrong.
    if (!transformReady) {
        return;
    }
    if (!_hasSortView) {
        // Identity order, drawn until the first sort lands.
        _visibleCount = (uint32_t)_resource->count();
    }

    if (const std::vector<uint32_t>* sorted = _sorter->takeResult()) {
        _visibleCount = (uint32_t)sorted->size();
        if (_visibleCount > 0) {
            _indexBuffer->setSubData(0, _visibleCount * sizeof(uint32_t), reinterpret_cast<const gpu::Byte*>(sorted->data()));
        }
    }
    if (_sorter->isBusy()) {
        return;
    }

    const glm::mat4 localToWorld = renderTransform.getMatrix();
    const glm::mat4 worldToLocal = glm::inverse(localToWorld);
    const ViewFrustum& frustum = args->getViewFrustum();
    // In splat-local space a static entity seen from a static head needs no new sort, whatever the entity transform.
    const glm::vec3 eye = glm::vec3(worldToLocal * glm::vec4(frustum.getPosition(), 1.0f));
    const glm::vec3 forward = glm::normalize(glm::mat3(worldToLocal) * frustum.getDirection());
    if (_hasSortView && glm::distance(eye, _sortedEye) <= RESORT_DISTANCE_FRACTION * _boundRadius &&
        glm::dot(forward, _sortedForward) >= RESORT_ANGLE_COS) {
        return;
    }

    splat::SortView view;
    view.modelView = glm::inverse(frustum.getView()) * localToWorld;
    view.nearClip = frustum.getNearClip();
    // Off-centre (HMD) projections reach further on one side; the larger side bounds both.
    const glm::mat4& projection = frustum.getProjection();
    view.tanHalfFov = SORT_FRUSTUM_MARGIN * (glm::vec2(1.0f) + glm::abs(glm::vec2(projection[2][0], projection[2][1]))) /
                      glm::vec2(projection[0][0], projection[1][1]);
    _sorter->sort(view);
    _sortedEye = eye;
    _sortedForward = forward;
    _hasSortView = true;
}

void GaussianSplatEntityRenderer::doRender(RenderArgs* args) {
    // Mirrors, portals and the secondary camera would blend with the main view's sort order (design R9).
    if (_visibleCount == 0 || !_dataBuffer || args->_mirrorDepth > 0 ||
        args->_renderMode == RenderArgs::RenderMode::SECONDARY_CAMERA_RENDER_MODE) {
        return;
    }

    // billboardMode is not applied to rendering in this version, only to picking (GaussianSplatEntityItem::isInsidePickBox).
    Transform transform;
    withReadLock([&] {
        transform = _renderTransform;
    });

    gpu::Batch& batch = *args->_batch;
    batch.setModelTransform(transform, _prevRenderTransform);
    if (args->_renderMode == Args::RenderMode::DEFAULT_RENDER_MODE || args->_renderMode == Args::RenderMode::MIRROR_RENDER_MODE) {
        _prevRenderTransform = transform;
    }

    batch.setUniformBuffer(SPLAT_PARAMS_SLOT, _paramsBuffer);
    batch.setResourceBuffer(SPLAT_DATA_SLOT, _dataBuffer);
    batch.setInputFormat(splatIndexFormat);
    batch.setInputBuffer(0, _indexBuffer, 0, sizeof(uint32_t));
    batch.drawInstanced(_visibleCount, gpu::TRIANGLE_STRIP, VERTICES_PER_SPLAT);
}
