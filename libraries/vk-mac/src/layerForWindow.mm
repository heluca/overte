//
//  Created by Dr. Karol Suprynowicz, Julian Groß and Lexi "Big Cheese" on 2026-05-16
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QtGui/QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <AppKit/AppKit.h>
#include <QuartzCore/QuartzCore.h>

#include "layerForWindow.h"

// Get the CAMetalLayer to create a MoltenVk surface on, inside a given QWindow.
CAMetalLayer* layerForWindow(QWindow *window)
{
    Q_ASSERT(window);
#ifdef Q_OS_MACOS
    NSView *view = reinterpret_cast<NSView *>(window->winId());
#else
    UIView *view = reinterpret_cast<UIView *>(window->winId());
#endif
    Q_ASSERT(view);
    Q_ASSERT([view isKindOfClass:[NSView class]]);
    if (![view.layer isKindOfClass:[CAMetalLayer class]])
    {
        [view setLayer:[CAMetalLayer layer]];
    }
    Q_ASSERT([view.layer isKindOfClass:[CAMetalLayer class]]);
    return static_cast<CAMetalLayer *>(view.layer);
}

MetalLayerMetrics metalLayerMetrics(QWindow *window)
{
    MetalLayerMetrics metrics;
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    if (!view || ![view.layer isKindOfClass:[CAMetalLayer class]]) {
        return metrics;
    }
    CAMetalLayer *layer = static_cast<CAMetalLayer *>(view.layer);
    metrics.contentsScale = layer.contentsScale;
    metrics.boundsWidth = layer.bounds.size.width;
    metrics.boundsHeight = layer.bounds.size.height;
    metrics.drawableWidth = layer.drawableSize.width;
    metrics.drawableHeight = layer.drawableSize.height;
    return metrics;
}
