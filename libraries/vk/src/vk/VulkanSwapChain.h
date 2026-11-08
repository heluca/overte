/*
* Class wrapping access to the swap chain
* 
* A swap chain is a collection of framebuffers used for rendering and presentation to the windowing system
*
* Copyright (C) 2016-2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <stdlib.h>
#include <string>
#include <assert.h>
#include <stdio.h>
#include <vector>

#include <vulkan/vulkan.h>
#include "Context.h"
#include "VulkanTools.h"

#ifdef __ANDROID__
#include "VulkanAndroid.h"
#endif

#ifdef __APPLE__
#include <sys/utsname.h>
#endif

typedef struct _SwapChainBuffers {
    VkImage image;
    VkImageView view;
} SwapChainBuffer;

class VulkanSwapChain
{
private:
    vks::Context *_context {nullptr};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
public:
    VkExtent2D extent{0, 0};
    VkFormat colorFormat;
    VkColorSpaceKHR colorSpace;
    // Every format and colour space the surface offers, read once when the surface is created
    std::vector<VkSurfaceFormatKHR> surfaceFormats;
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;    
    uint32_t imageCount;
    std::vector<VkImage> images;
    std::vector<SwapChainBuffer> buffers;
    uint32_t queueNodeIndex = UINT32_MAX;

    /* Use a surface created for the window by the windowing system integration (QVulkanInstance::surfaceForWindow) */
    void initSurface(VkSurfaceKHR windowSurface);
#if defined(_DIRECT2DISPLAY)
    void createDirect2DisplaySurface(uint32_t width, uint32_t height);
#endif
    /* Set the Vulkan objects required for swapchain creation and management, must be called before swapchain creation */
    void setContext(vks::Context *context);
    /**
    * Create the swapchain and get its images with given width and height
    * 
    * @param width Pointer to the width of the swapchain (may be adjusted to fit the requirements of the swapchain)
    * @param height Pointer to the height of the swapchain (may be adjusted to fit the requirements of the swapchain)
    * @param vsync (Optional, default = false) Can be used to force vsync-ed rendering (by using VK_PRESENT_MODE_FIFO_KHR as presentation mode)
    */
    void create(uint32_t* width, uint32_t* height, bool vsync = false, bool fullscreen = false, bool hdr = false);
    /** Picks colorFormat and colorSpace for the next create(): with hdr, a half-float image in the extended linear
    * sRGB colour space when the surface offers one (values above 1.0 are then brighter than SDR white), otherwise
    * the 8-bit sRGB image. Returns true when the HDR pair was chosen. */
    bool selectFormat(bool hdr);
    /* True when the swapchain presents linear values with headroom above SDR white */
    bool isHDR() const { return colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT && colorFormat == VK_FORMAT_R16G16B16A16_SFLOAT; }
    /* "format/colourspace" for logs */
    std::string describeFormat() const;
    static std::string describeFormat(const VkSurfaceFormatKHR& surfaceFormat);
    /**
    * Acquires the next image in the swap chain
    * 
    * @param presentCompleteSemaphore (Optional) Semaphore that is signaled when the image is ready for use
    * @param imageIndex Pointer to the image index that will be increased if the next image could be acquired
    * 
    * @note The function will always wait until the next image has been acquired by setting timeout to UINT64_MAX
    * 
    * @return VkResult of the image acquisition
    */
    VkResult acquireNextImage(VkSemaphore presentCompleteSemaphore, uint32_t* imageIndex);
    /**
    * Queue an image for presentation
    *
    * @param queue Presentation queue for presenting the image
    * @param imageIndex Index of the swapchain image to queue for presentation
    * @param waitSemaphore (Optional) Semaphore that is waited on before the image is presented (only used if != VK_NULL_HANDLE)
    *
    * @return VkResult of the queue presentation
    */
    VkResult queuePresent(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore = VK_NULL_HANDLE);
    /* Free all Vulkan resources acquired by the swapchain */
    void cleanup();
};
