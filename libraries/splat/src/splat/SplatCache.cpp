//
//  SplatCache.cpp
//  libraries/splat/src/splat
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "SplatCache.h"

#include <QtCore/QRunnable>
#include <QtCore/QThread>
#include <QtCore/QThreadPool>

#include <Finally.h>
#include <Profile.h>
#include <StatTracker.h>

#include "PlyParser.h"
#include "SplatLogging.h"

int splatDataPointerMetaTypeId = qRegisterMetaType<SplatDataPointer>("SplatDataPointer");

namespace {

// Parses a downloaded splat file on the global thread pool, as GeometryReader does for models.
class SplatReader : public QRunnable {
public:
    SplatReader(const QWeakPointer<Resource>& resource, const QUrl& url, const QByteArray& data) :
        _resource(resource), _url(url), _data(data) {
        DependencyManager::get<StatTracker>()->incrementStat("PendingProcessing");
    }

    void run() override;

private:
    QWeakPointer<Resource> _resource;
    QUrl _url;
    QByteArray _data;
};

void SplatReader::run() {
    DependencyManager::get<StatTracker>()->decrementStat("PendingProcessing");
    CounterStat counter("Processing");
    PROFILE_RANGE_EX(resource_parse, "SplatReader::run", 0xFF00FF00, 0, { { "url", _url.toString() } });
    auto originalPriority = QThread::currentThread()->priority();
    if (originalPriority == QThread::InheritPriority) {
        originalPriority = QThread::NormalPriority;
    }
    QThread::currentThread()->setPriority(QThread::LowPriority);
    Finally setPriorityBackToNormal([originalPriority]() {
        QThread::currentThread()->setPriority(originalPriority);
    });

    if (!_resource.toStrongRef().data()) {
        return;
    }

    splat::SplatCloud cloud;
    QString error;
    bool parsed = false;
    if (splat::isPly(_data)) {
        parsed = splat::parsePly(_data, cloud, error);
    } else {
        error = "unsupported format; only binary INRIA 3D Gaussian Splatting .ply is supported";
    }
    // Do not hold the raw file while building the GPU payload.
    _data = QByteArray();

    std::shared_ptr<SplatData> data;
    if (parsed) {
        data = std::make_shared<SplatData>();
        data->count = cloud.count();
        data->rawBounds = cloud.rawBounds;
        data->naturalBounds = cloud.naturalBounds;
        data->dataBuffer = std::make_shared<gpu::Buffer>(gpu::Buffer::ResourceBuffer,
                                                         cloud.packed.size() * sizeof(splat::PackedSplat),
                                                         reinterpret_cast<const gpu::Byte*>(cloud.packed.data()));
        std::vector<splat::PackedSplat>().swap(cloud.packed);
        data->positions = std::make_shared<std::vector<glm::vec3>>(std::move(cloud.positions));
    }

    auto resource = _resource.toStrongRef();
    if (!resource) {
        qCDebug(splat_logging) << "Abandoning load of" << _url << "; resource was released during parse";
        return;
    }
    if (!parsed) {
        qCWarning(splat_logging) << "Failed to load splat" << _url << "--" << error;
        QMetaObject::invokeMethod(resource.data(), "finishedLoading", Q_ARG(bool, false));
        return;
    }
    SplatDataPointer result = data;
    QMetaObject::invokeMethod(resource.data(), "setSplatData", Q_ARG(SplatDataPointer, result));
}

} // namespace

void SplatResource::downloadFinished(const QByteArray& data) {
    QThreadPool::globalInstance()->start(new SplatReader(_self, _url, data));
}

void SplatResource::setSplatData(SplatDataPointer data) {
    _data = data;
    setSize((qint64)(_data->dataBuffer->getSize() + _data->positions->size() * sizeof(glm::vec3)));
    qCDebug(splat_logging) << "Loaded" << _data->count << "splats from" << _url;
    finishedLoading(true);
}

SplatCache::SplatCache() {
    setUnusedResourceCacheSize(DEFAULT_UNUSED_MAX_SIZE);
    setObjectName("SplatCache");
}

SplatResource::Pointer SplatCache::getSplat(const QUrl& url) {
    return getResource(url).staticCast<SplatResource>();
}

QSharedPointer<Resource> SplatCache::createResource(const QUrl& url) {
    return QSharedPointer<SplatResource>(new SplatResource(url), &Resource::deleter);
}

QSharedPointer<Resource> SplatCache::createResourceCopy(const QSharedPointer<Resource>& resource) {
    return QSharedPointer<SplatResource>(new SplatResource(*resource.staticCast<SplatResource>()), &Resource::deleter);
}
