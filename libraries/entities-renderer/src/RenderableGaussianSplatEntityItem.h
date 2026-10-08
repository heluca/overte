//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef hifi_RenderableGaussianSplatEntityItem_h
#define hifi_RenderableGaussianSplatEntityItem_h

#include "RenderableEntityItem.h"

#include <GaussianSplatEntityItem.h>
#include <splat/SplatCache.h>
#include <splat/SplatSorter.h>

namespace render { namespace entities {

class GaussianSplatEntityRenderer : public TypedEntityRenderer<GaussianSplatEntityItem> {
    using Parent = TypedEntityRenderer<GaussianSplatEntityItem>;
    using Pointer = std::shared_ptr<GaussianSplatEntityRenderer>;
public:
    GaussianSplatEntityRenderer(const EntityItemPointer& entity);

    void renderSimulate(RenderArgs* args) override;

    ComponentMode getFadeOutMode() const override { return ComponentMode::COMPONENT_MODE_DISABLED; }

protected:
    ItemKey getKey() override;
    ShapeKey getShapeKey() override;
    // Splats are always alpha blended, whatever the entity alpha.
    bool isTransparent() const override { return true; }

private:
    void doRenderUpdateSynchronousTyped(const ScenePointer& scene, Transaction& transaction, const TypedEntityPointer& entity) override;
    void doRenderUpdateAsynchronousTyped(const TypedEntityPointer& entity) override;
    void doRender(RenderArgs* args) override;

    void onSplatLoaded(const TypedEntityPointer& entity);
    void releaseSplat();

    QString _splatURL;
    float _alpha { NAN };
    SplatResource::Pointer _resource;
    bool _splatLoaded { false };

    // Render-thread state, filled once the resource has loaded.
    gpu::BufferPointer _dataBuffer;
    gpu::BufferPointer _indexBuffer;
    gpu::BufferView _paramsBuffer;
    uint32_t _visibleCount { 0 };
    std::unique_ptr<splat::SplatSorter> _sorter;
    float _boundRadius { 0.0f };
    bool _hasSortView { false };
    glm::vec3 _sortedEye;
    glm::vec3 _sortedForward;

    // Also read by the post-update lambda that builds _renderTransform, so guarded by the renderer's lock.
    glm::vec3 _naturalDimensions { 1.0f };
    glm::vec3 _splatCenter { 0.0f };
};

} }
#endif // hifi_RenderableGaussianSplatEntityItem_h
