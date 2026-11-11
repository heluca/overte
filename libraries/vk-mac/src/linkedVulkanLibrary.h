//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#pragma once

// QVulkanInstance loads a Vulkan library of its own to resolve functions (vkCreateMetalSurfaceEXT among them) for the
// VkInstance we hand it. On macOS there is no system Vulkan loader: we link MoltenVK directly, and Qt would either find
// no library at all or a different MoltenVK copy that knows nothing of our instance, so surfaceForWindow fails or crashes.
// This points Qt's lookup (QT_VULKAN_LIB) at the image that provides the vkGetInstanceProcAddr this process linked.
// Call it before QVulkanInstance::create(). It leaves an explicit QT_VULKAN_LIB alone.
void useLinkedVulkanLibraryForQt();
