//
//  Created by Dr. Karol Suprynowicz on 2026-05-16
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

CAMetalLayer *layerForWindow(QWindow *window);

// Sizes MoltenVK compares when deciding whether a swapchain still fits its CAMetalLayer:
// the layer's drawableSize must equal both the swapchain extent and bounds * contentsScale.
struct MetalLayerMetrics {
    float contentsScale { 0.0f };
    float boundsWidth { 0.0f };
    float boundsHeight { 0.0f };
    float drawableWidth { 0.0f };
    float drawableHeight { 0.0f };
};
MetalLayerMetrics metalLayerMetrics(QWindow *window);
