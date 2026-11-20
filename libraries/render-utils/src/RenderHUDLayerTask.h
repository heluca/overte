//
//  Created by Sam Gateau on 2019/06/14
//  Copyright 2013-2019 High Fidelity, Inc.
//  Copyright 2024 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_RenderHUDLayerTask_h
#define hifi_RenderHUDLayerTask_h

#include "LightingModel.h"
#include "HazeStage.h"
#include "DeferredFrameTransform.h"
#include "ToneMapAndResampleTask.h"

class CompositeHUD {
public:
    // IF specified the input Framebuffer is actively set by the batch of this job before calling the HUDOperator.
    // If not, the current Framebuffer is left unchanged.
    //using Inputs = gpu::FramebufferPointer;
    using JobModel = render::Job::ModelI<CompositeHUD, gpu::FramebufferPointer>;

    CompositeHUD(uint transformSlot) : _transformSlot(transformSlot) {}

    void run(const render::RenderContextPointer& renderContext, const gpu::FramebufferPointer& inputs);

private:
    uint _transformSlot;
};

// HDR output: HUD-layer items are drawn after the tone mapper with SDR white at 1.0, so the final framebuffer is brought
// to SDR white units before them and back to the swapchain's SDR white after them. Blending is linear, so transparent
// items blend with the scene exactly as if they had been drawn scaled themselves.
class ScaleHUDLayerToHDRWhite {
public:
    // Framebuffer the HUD layer draws into, opaque items, transparent items
    using Inputs = render::VaryingSet3<gpu::FramebufferPointer, render::ItemBounds, render::ItemBounds>;
    using JobModel = render::Job::ModelI<ScaleHUDLayerToHDRWhite, Inputs>;

    // toSwapchainWhite false runs before the HUD-layer items, true after them
    ScaleHUDLayerToHDRWhite(bool toSwapchainWhite) : _toSwapchainWhite(toSwapchainWhite) {}

    void run(const render::RenderContextPointer& renderContext, const Inputs& inputs);

private:
    bool _toSwapchainWhite;
    ScaleFramebufferColor _scale;
};

class RenderHUDLayerTask {
public:
    // Framebuffer where to draw, lighting model, opaque items, transparent items
    using Input = render::VaryingSet6<gpu::FramebufferPointer, LightingModelPointer, render::ItemBounds, render::ItemBounds, HazeStage::FramePointer, DeferredFrameTransformPointer>;
    using JobModel = render::Task::ModelI<RenderHUDLayerTask, Input>;

    void build(JobModel& task, const render::Varying& input, render::Varying& output, render::ShapePlumberPointer shapePlumber, uint transformSlot);
};

#endif // hifi_RenderHUDLayerTask_h