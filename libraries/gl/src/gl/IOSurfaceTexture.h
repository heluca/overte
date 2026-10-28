//
//  IOSurfaceTexture.h
//  libraries/gl/src/gl
//
//  Created by Robert Helewka on 2026-10-06.
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//
#pragma once

#include <cstdint>

namespace gl {

// OpenGL textures whose storage is an IOSurface (macOS only).
//
// On macOS the QML UI is rendered with OpenGL while the scene is rendered with Vulkan through MoltenVK.
// Apple's OpenGL has no GL_EXT_memory_object, so the only zero-copy way to share a texture between
// the two APIs is an IOSurface: OpenGL binds it to a GL_TEXTURE_RECTANGLE texture with
// CGLTexImageIOSurface2D, and MoltenVK wraps the same surface in a VkImage with
// VkImportMetalIOSurfaceInfoEXT (VK_EXT_metal_objects).
//
// All functions are no-ops / return false on other platforms so callers need no #ifdefs.

// True when textures handed from OpenGL to the renderer should be IOSurface backed
// (macOS with the Vulkan backend active).
bool useIOSurfaceTextures();

// Creates an IOSurface of the given size and a GL_TEXTURE_RECTANGLE texture bound to it in the
// current OpenGL context. Returns the texture name, or 0 on failure. The surface stays alive at
// least until destroyIOSurfaceTexture is called for the texture.
uint32_t createIOSurfaceTexture(uint32_t width, uint32_t height);

// Deletes a texture created with createIOSurfaceTexture and drops this module's reference to the surface.
void destroyIOSurfaceTexture(uint32_t texture);

bool isIOSurfaceTexture(uint32_t texture);

// Returns the IOSurfaceRef (as an opaque pointer) behind a texture created with createIOSurfaceTexture,
// or nullptr. The reference is retained on the caller's behalf while the registry lock is held, so the
// surface cannot be freed by a concurrent destroyIOSurfaceTexture; the caller owns it and must pass it to
// releaseIOSurface. Optionally reports the surface size.
void* ioSurfaceForTexture(uint32_t texture, uint32_t* width = nullptr, uint32_t* height = nullptr);

// Releases a reference obtained from ioSurfaceForTexture.
void releaseIOSurface(void* ioSurface);

}  // namespace gl
