# 
#  Created by Bradley Austin Davis on 2016/02/16
#
#  Distributed under the Apache License, Version 2.0.
#  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
# 
macro(TARGET_VULKAN)
    if(APPLE)
        # Conan's MoltenVK package provides the moltenvk::moltenvk target, not MoltenVK_LIBRARY. Linking the target
        # matters with a Qt that does not itself depend on Conan's MoltenVK (a system Qt).
        find_package(MoltenVK QUIET REQUIRED)
        target_link_libraries(${TARGET_NAME} moltenvk::moltenvk)
    else()
        find_package(Vulkan QUIET REQUIRED)
        target_include_directories(${TARGET_NAME} PRIVATE ${VULKAN_INCLUDE_DIR})
        target_link_libraries(${TARGET_NAME} ${VULKAN_LIBRARY})
    endif()
    find_package(VulkanMemoryAllocator QUIET REQUIRED)
    target_link_libraries(${TARGET_NAME} GPUOpen::VulkanMemoryAllocator)
endmacro()
