//
//  Created by Bradley Austin Davis on 2018/07/27
//  Copyright 2013-2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#pragma once

#ifndef hifi_ContextAwareProfile_h
#define hifi_ContextAwareProfile_h

#include <atomic>
#include <QtCore/QHash>
#include <QtCore/QReadWriteLock>
#include <QtCore/QSet>

#if !defined(Q_OS_ANDROID)
#include <QQuickWebEngineProfile>
#include <QtWebEngineCore/QWebEngineUrlRequestInterceptor>

using ContextAwareProfileParent = QQuickWebEngineProfile;
using RequestInterceptorParent = QWebEngineUrlRequestInterceptor;
#else
#include <QtCore/QObject>

using ContextAwareProfileParent = QObject;
using RequestInterceptorParent = QObject;
#endif

class QQmlContext;

// One profile object exists per QML context, so that its request interceptor can apply that context's file
// access restriction. These profiles must stay off the record (the QQuickWebEngineProfile default). Under
// Qt 6 every profile object is its own Chromium browser context, and persistent profiles that share a storage
// name (they used to share "qmlWebEngine") share one storage directory. Chromium's storage service keeps a
// single DOM storage backend per directory: each profile that binds it disconnects the others, which rebind
// at once, so the browser thread spins forever and no page in any web view finishes loading.
class ContextAwareProfile : public ContextAwareProfileParent {
    Q_OBJECT
public:
    static void restrictContext(QQmlContext* context, bool restrict = true);
    bool isRestricted();
    Q_INVOKABLE bool isRestrictedGetProperty();
protected:

    class RequestInterceptor : public RequestInterceptorParent {
    public:
        RequestInterceptor(ContextAwareProfile* parent) : RequestInterceptorParent(parent), _profile(parent) { }
        bool isRestricted() { return _profile->isRestricted(); }
    protected:
        ContextAwareProfile* _profile;
    };

    ContextAwareProfile(QQmlContext* parent);
    ~ContextAwareProfile();

private:
    typedef QSet<ContextAwareProfile*> ContextAwareProfileSet;
    typedef QHash<QQmlContext*, ContextAwareProfileSet> ContextMap;

    QQmlContext* _context{ nullptr };
    std::atomic<bool> _isRestricted{ false };

    static QReadWriteLock _contextMapProtect;
    static ContextMap _contextMap;
};

#endif // hifi_FileTypeProfile_h
