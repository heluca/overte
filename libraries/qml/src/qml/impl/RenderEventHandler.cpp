//
//  Created by Bradley Austin Davis on 2018-01-04
//  Copyright 2013-2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "RenderEventHandler.h"

#ifndef DISABLE_QML

#include <gl/Config.h>
#include <gl/QOpenGLContextWrapper.h>
#include <gl/GLHelpers.h>

#include <QtQuick/QQuickWindow>
#include <QQuickOpenGLUtils>

#include <shared/NsightHelpers.h>
#include "Profiling.h"
#include "SharedObject.h"
#include "TextureCache.h"
#include <gl/IOSurfaceTexture.h>
#include "RenderControl.h"
#include "../Logging.h"

using namespace hifi::qml::impl;

bool RenderEventHandler::event(QEvent* e) {
    switch (static_cast<OffscreenEvent::Type>(e->type())) {
        case OffscreenEvent::Render:
            onRender();
            return true;

        case OffscreenEvent::RenderSync:
            onRenderSync();
            return true;

        case OffscreenEvent::Initialize:
            onInitalize();
            return true;

        case OffscreenEvent::Quit:
            onQuit();
            return true;

        default:
            break;
    }
    return QObject::event(e);
}

RenderEventHandler::RenderEventHandler(SharedObject* shared, QThread* targetThread) :
        _shared(shared) {
    // Create the GL canvas in the same thread as the share canvas
    if (!_canvas.create(SharedObject::getSharedContext())) {
        qFatal("Unable to create new offscreen GL context");
    }

    _canvas.moveToThreadWithContext(targetThread);
    moveToThread(targetThread);
}

// I'm not sure if several contexts can be initalized with the same shared context concurrently, so better safe than sorry.
static std::mutex renderControlInitMutex;

void RenderEventHandler::onInitalize() {
    std::lock_guard<std::mutex> lock(renderControlInitMutex);
    if (_shared->isQuit()) {
        return;
    }

    _canvas.setThreadContext();
    if (!_canvas.makeCurrent()) {
        qFatal("Unable to make QML rendering context current on render thread");
    }
    _shared->initializeRenderControl(_canvas.getContext());
    _initialized = true;
}

void RenderEventHandler::resize() {
    PROFILE_RANGE(render_qml_gl, __FUNCTION__);
    auto targetSize = _shared->getSize();
    if (_currentSize != targetSize) {
        auto& offscreenTextures = SharedObject::getTextureCache();
        // Release hold on the textures of the old size
        if (_currentSize != QSize()) {
            _shared->releaseTextureAndFence();
            offscreenTextures.releaseSize(_currentSize);
        }

        _currentSize = targetSize;

        // Acquire the new texture size
        if (_currentSize != QSize()) {
            qCDebug(qmlLogging) << "Upating offscreen textures to " << _currentSize.width() << " x " << _currentSize.height();
            offscreenTextures.acquireSize(_currentSize);
        }
    }

    // The surface size, the handed-over texture and the input mapping are all in logical pixels, but Qt 6 lays
    // the scene out in the render target's pixel size divided by the render window's device pixel ratio. With a
    // proxy window on a scaled screen, a render target of the logical size would only hold the top left part of
    // the scene, so render at the device pixel size instead.
    QSize renderSize;
    if (_currentSize != QSize()) {
        renderSize = _currentSize * _shared->_quickWindow->effectiveDevicePixelRatio();
    }
    if (_renderSize == renderSize) {
        return;
    }
    _renderSize = renderSize;

    if (_depthStencil) {
        glDeleteRenderbuffers(1, &_depthStencil);
        _depthStencil = 0;
    }
    if (_renderTexture) {
        glDeleteTextures(1, &_renderTexture);
        _renderTexture = 0;
    }
    if (_renderSize == QSize()) {
        return;
    }

    if (_renderSize != _currentSize) {
        qCDebug(qmlLogging) << "Rendering offscreen QML at " << _renderSize.width() << " x " << _renderSize.height();
        glGenTextures(1, &_renderTexture);
        glBindTexture(GL_TEXTURE_2D, _renderTexture);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, _renderSize.width(), _renderSize.height());
        glBindTexture(GL_TEXTURE_2D, 0);
        if (!_resolveFbo) {
            glGenFramebuffers(1, &_resolveFbo);
        }
    }

    glGenRenderbuffers(1, &_depthStencil);
    glBindRenderbuffer(GL_RENDERBUFFER, _depthStencil);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, _renderSize.width(), _renderSize.height());
    if (!_fbo) {
        glGenFramebuffers(1, &_fbo);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _depthStencil);
    glViewport(0, 0, _renderSize.width(), _renderSize.height());
    glScissor(0, 0, _renderSize.width(), _renderSize.height());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

void RenderEventHandler::onRender() {
    qmlRender(false);
}

void RenderEventHandler::onRenderSync() {
    qmlRender(true);
}

void RenderEventHandler::qmlRender(bool sceneGraphSync) {
    if (_shared->isQuit()) {
        return;
    }

    if (_canvas.getContext() != QOpenGLContextWrapper::currentContext()) {
        qFatal("QML rendering context not current on render thread");
    }

    PROFILE_RANGE(render_qml_gl, __FUNCTION__);

    gl::globalLock();

    _shared->_renderControl->beginFrame();

    if (!_shared->preRender(sceneGraphSync)) {
        gl::globalRelease();
        return;
    }

    resize();

    if (_currentSize != QSize()) {
        PROFILE_RANGE(render_qml_gl, "render");
        GLuint texture = SharedObject::getTextureCache().acquireTexture(_currentSize);
        GLuint renderTarget = _renderTexture ? _renderTexture : texture;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo);
        glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, renderTarget, 0);
        if (nsightActive()) {
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        } else {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            _shared->setRenderTarget(renderTarget, _renderSize);
            _shared->_renderControl->render();
        }
        _shared->_renderControl->endFrame();
        _shared->_lastRenderTime = usecTimestampNow();
        if (_renderTexture) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, _fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _resolveFbo);
            glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture, 0);
            // The scene graph leaves its scissor state behind, and the scissor test applies to blits.
            glDisable(GL_SCISSOR_TEST);
            glBlitFramebuffer(0, 0, _renderSize.width(), _renderSize.height(), 0, 0, _currentSize.width(),
                              _currentSize.height(), GL_COLOR_BUFFER_BIT, GL_LINEAR);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        if (!gl::isIOSurfaceTexture(texture)) {
            // IOSurface textures are GL_TEXTURE_RECTANGLE and have no mip chain.
            glBindTexture(GL_TEXTURE_2D, texture);
            glGenerateMipmap(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        auto fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        // Fence will be used in another thread / context, so a flush is required
        glFlush();
        _shared->updateTextureAndFence({ texture, fence });
        QQuickOpenGLUtils::resetOpenGLState();
    }
    gl::globalRelease();
}

void RenderEventHandler::onQuit() {
    if (_initialized) {
        if (_canvas.getContext() != QOpenGLContextWrapper::currentContext()) {
            qFatal("QML rendering context not current on render thread");
        }

        if (_depthStencil) {
            glDeleteRenderbuffers(1, &_depthStencil);
            _depthStencil = 0;
        }

        if (_fbo) {
            glDeleteFramebuffers(1, &_fbo);
            _fbo = 0;
        }

        if (_renderTexture) {
            glDeleteTextures(1, &_renderTexture);
            _renderTexture = 0;
        }

        if (_resolveFbo) {
            glDeleteFramebuffers(1, &_resolveFbo);
            _resolveFbo = 0;
        }

        _shared->shutdownRendering(_currentSize);
        _canvas.doneCurrent();
    }
    _canvas.moveToThreadWithContext(qApp->thread());
    moveToThread(qApp->thread());
    QThread::currentThread()->quit();
}

#endif
