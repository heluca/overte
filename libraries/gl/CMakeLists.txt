set(TARGET_NAME gl)
setup_hifi_library(Gui Widgets)
link_hifi_libraries(shared)

set(OpenGL_GL_PREFERENCE "GLVND")
target_opengl()
target_glad()

# support EGL on Linux for Wayland
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_egl()
endif()

# IOSurface backed textures shared with the Vulkan (MoltenVK) backend, see IOSurfaceTexture.cpp
if (APPLE)
    find_library(IOSurface IOSurface)
    find_library(OpenGL OpenGL)
    target_link_libraries(${TARGET_NAME} ${IOSurface} ${OpenGL})
endif()
