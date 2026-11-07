//
//  Created by Bradley Austin Davis on 2016/03/19
//  Copyright 2013-2018 High Fidelity, Inc.
//  Copyright 2024-2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <algorithm>
#include <functional>
#include <list>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include <glm/glm.hpp>


#include <QtGlobal>
#if defined(Q_OS_WIN)
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(Q_OS_ANDROID)
#define VK_USE_PLATFORM_ANDROID_KHR
#elif defined(Q_OS_DARWIN)
// Declares VK_EXT_metal_surface, which QVulkanInstance creates the window surface with through MoltenVK,
// and VK_EXT_metal_objects, which imports the IOSurfaces shared with OpenGL.
#define VK_USE_PLATFORM_METAL_EXT
#endif

#include <vulkan/vulkan.h>

#define VULKAN_USE_VMA 1

#if VULKAN_USE_VMA
#include "vk_mem_alloc.h"
#endif
