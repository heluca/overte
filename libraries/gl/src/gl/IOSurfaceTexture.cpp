//
//  IOSurfaceTexture.cpp
//  libraries/gl/src/gl
//
//  Created by Robert Helewka on 2026-10-06.
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//
#include "IOSurfaceTexture.h"

#include <QtCore/QtGlobal>

#ifdef Q_OS_MAC

#include <mutex>
#include <unordered_map>

#include <QtCore/QDebug>

#include "Config.h"
#include "GLLogging.h"

// The Apple headers only declare the CGL entry points and typedefs; glad already provides the GL
// enums and function pointers, and gl/Config.h blocks the system gl.h so the two do not clash.
#include <IOSurface/IOSurfaceRef.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLIOSurface.h>

namespace {

struct SurfaceTexture {
    IOSurfaceRef surface { nullptr };
    uint32_t width { 0 };
    uint32_t height { 0 };
};

std::mutex& registryMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<uint32_t, SurfaceTexture>& registry() {
    static std::unordered_map<uint32_t, SurfaceTexture> textures;
    return textures;
}

// kCVPixelFormatType_32BGRA without pulling in CoreVideo. BGRA is the one 8 bit format that both
// CGLTexImageIOSurface2D and Metal (MTLPixelFormatBGRA8Unorm) accept on an IOSurface.
constexpr uint32_t PIXEL_FORMAT_BGRA = 'BGRA';
constexpr uint32_t BYTES_PER_ELEMENT = 4;

IOSurfaceRef createSurface(uint32_t width, uint32_t height) {
    auto makeNumber = [](uint32_t value) {
        return CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
    };
    CFNumberRef widthNumber = makeNumber(width);
    CFNumberRef heightNumber = makeNumber(height);
    CFNumberRef bytesPerElement = makeNumber(BYTES_PER_ELEMENT);
    CFNumberRef pixelFormat = makeNumber(PIXEL_FORMAT_BGRA);

    const void* keys[] = { kIOSurfaceWidth, kIOSurfaceHeight, kIOSurfaceBytesPerElement, kIOSurfacePixelFormat };
    const void* values[] = { widthNumber, heightNumber, bytesPerElement, pixelFormat };
    CFDictionaryRef properties = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 4,
                                                    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    IOSurfaceRef surface = IOSurfaceCreate(properties);

    CFRelease(properties);
    CFRelease(widthNumber);
    CFRelease(heightNumber);
    CFRelease(bytesPerElement);
    CFRelease(pixelFormat);
    return surface;
}

}  // namespace

bool gl::useIOSurfaceTextures() {
#ifdef USE_GL
    // OpenGL backend: the renderer samples the QML texture directly, no sharing needed.
    return false;
#else
    // Vulkan backend (the rendering backend is chosen at build time with OVERTE_RENDERING_BACKEND).
    // OVERTE_DISABLE_IOSURFACE=1 forces the glGetTexImage readback path, for comparison and debugging.
    static const bool disabled = qEnvironmentVariableIsSet("OVERTE_DISABLE_IOSURFACE");
    return !disabled;
#endif
}

uint32_t gl::createIOSurfaceTexture(uint32_t width, uint32_t height) {
    CGLContextObj cglContext = CGLGetCurrentContext();
    if (!cglContext) {
        qCWarning(glLogging) << "createIOSurfaceTexture: no current CGL context";
        return 0;
    }
    IOSurfaceRef surface = createSurface(width, height);
    if (!surface) {
        qCWarning(glLogging) << "createIOSurfaceTexture: IOSurfaceCreate failed for" << width << "x" << height;
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_RECTANGLE, texture);
    // Rectangle textures have no mip levels, so only the non-mipmapped filters are valid.
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    CGLError error = CGLTexImageIOSurface2D(cglContext, GL_TEXTURE_RECTANGLE, GL_RGBA, width, height,
                                            GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, surface, 0);
    glBindTexture(GL_TEXTURE_RECTANGLE, 0);
    if (error != kCGLNoError) {
        qCWarning(glLogging) << "createIOSurfaceTexture: CGLTexImageIOSurface2D failed:" << CGLErrorString(error);
        glDeleteTextures(1, &texture);
        CFRelease(surface);
        return 0;
    }

    std::lock_guard<std::mutex> lock(registryMutex());
    registry()[texture] = { surface, width, height };
    return texture;
}

void gl::destroyIOSurfaceTexture(uint32_t texture) {
    SurfaceTexture entry;
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        auto it = registry().find(texture);
        if (it == registry().end()) {
            return;
        }
        entry = it->second;
        registry().erase(it);
    }
    GLuint name = texture;
    glDeleteTextures(1, &name);
    CFRelease(entry.surface);
}

bool gl::isIOSurfaceTexture(uint32_t texture) {
    std::lock_guard<std::mutex> lock(registryMutex());
    return registry().count(texture) != 0;
}

void* gl::ioSurfaceForTexture(uint32_t texture, uint32_t* width, uint32_t* height) {
    std::lock_guard<std::mutex> lock(registryMutex());
    auto it = registry().find(texture);
    if (it == registry().end()) {
        return nullptr;
    }
    if (width) {
        *width = it->second.width;
    }
    if (height) {
        *height = it->second.height;
    }
    CFRetain(it->second.surface);
    return it->second.surface;
}

void gl::releaseIOSurface(void* ioSurface) {
    if (ioSurface) {
        CFRelease(static_cast<IOSurfaceRef>(ioSurface));
    }
}

#else

bool gl::useIOSurfaceTextures() { return false; }
uint32_t gl::createIOSurfaceTexture(uint32_t, uint32_t) { return 0; }
void gl::destroyIOSurfaceTexture(uint32_t) {}
bool gl::isIOSurfaceTexture(uint32_t) { return false; }
void* gl::ioSurfaceForTexture(uint32_t, uint32_t*, uint32_t*) { return nullptr; }
void gl::releaseIOSurface(void*) {}

#endif  // Q_OS_MAC
