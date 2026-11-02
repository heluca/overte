//
//  Created by Bradley Austin Davis on 2016/03/19
//  Copyright 2016-2018 High Fidelity, Inc.
//  Copyright 2022-2025 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#pragma once

#include <QtCore/QCoreApplication>
#include <QtGui/QWindow>
#include <QtGui/qevent.h>
#include <QtCore/QTimer>
#include <QtCore/QDebug>
#include <functional>

#include "Allocation.h"
#include "Config.h"
#include "Context.h"
#include "VulkanSwapChain.h"

class VKWindow : public QWindow {
    Q_OBJECT
public:
    VKWindow(QScreen* screen = nullptr);
    virtual ~VKWindow();

    void createSwapchain();
    void createSurface();

    bool event(QEvent *event) override;

    // One line describing the window, swapchain and (on macOS) CAMetalLayer sizes, for logging.
    QString describeSurface() const;

    // Asks for a swapchain that presents linear values with headroom above SDR white (half floats in the extended
    // linear sRGB colour space) when the surface offers one; the change happens at the next present. Any thread.
    void setHDRRequested(bool hdr);
    bool isHDRRequested() const { return _hdrRequested; }

    // Asks for the EDR headroom of the screen showing the window, the brightest value it shows as a multiple of
    // SDR white; `deliver` gets it on the main thread (1.0 where the platform cannot say). Any thread.
    void requestDisplayHeadroom(std::function<void(float)> deliver);

signals:
    void aboutToClose();

protected:
    friend struct vks::Context;
    void emitClosing();

public slots:
    virtual void resizeFramebuffer();

protected:
    void resizeEvent(QResizeEvent* event) override;

    void setupRenderPass();
    void setupDepthStencil();
    void setupFramebuffers();
    void createCommandBuffers();
    void vulkanCleanup(); // Called by the context before backend is destroyed.

public:
    VKWidget *_primaryWidget{ nullptr };
    vks::Context& _context{ vks::Context::get() };
    VkRenderPass _renderPass{};
    VkExtent2D _extent;
    VulkanSwapChain _swapchain;
    VkSemaphore _acquireCompleteSemaphore{};
    VkSemaphore _renderCompleteSemaphore{};
    std::vector<VkCommandBuffer> _drawCommandBuffers;
    struct : vks::Allocation {
        bool isAllocated {false};
        VkImage image;
        VkImageView view;
    } _depthStencil{};
    std::vector<VkFramebuffer> _frameBuffers;
    std::atomic<bool> _isVulkanCleanupComplete{ false };
    std::atomic<bool> _needsResizing{ true };
    std::atomic<bool> _hdrRequested{ false };
    VkFence _previousFrameFence{ VK_NULL_HANDLE };
    VkCommandBuffer _previousCommandBuffer{ VK_NULL_HANDLE };
    VkSemaphore _previousAcquireCompleteSemaphore{ VK_NULL_HANDLE };
    VkSemaphore _previousRenderCompleteSemaphore{ VK_NULL_HANDLE };
};
