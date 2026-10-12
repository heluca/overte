//
//  SplatCache.h
//  libraries/splat/src/splat
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_SplatCache_h
#define overte_SplatCache_h

#include <memory>
#include <vector>

#include <QtCore/QSharedPointer>

#include <glm/glm.hpp>

#include <AABox.h>
#include <DependencyManager.h>
#include <ResourceCache.h>
#include <gpu/Buffer.h>

// The loaded, immutable result of parsing one splat file. Shared by every entity that uses the same URL.
struct SplatData {
    // count() PackedSplat records (32 B each, layout in SplatCloud.h), for batch.setResourceBuffer.
    gpu::BufferPointer dataBuffer;
    // Splat centres in the same local frame as dataBuffer, for the depth sort. Held by shared pointer so an
    // in-flight sort job can outlive the resource.
    std::shared_ptr<const std::vector<glm::vec3>> positions;
    size_t count { 0 };
    AABox rawBounds;
    AABox naturalBounds;
};
using SplatDataPointer = std::shared_ptr<const SplatData>;
Q_DECLARE_METATYPE(SplatDataPointer)

/// A Gaussian splat cloud loaded from the network (http, https, atp, file).
class SplatResource : public Resource {
    Q_OBJECT
public:
    using Pointer = QSharedPointer<SplatResource>;
    using Positions = std::shared_ptr<const std::vector<glm::vec3>>;

    SplatResource(const QUrl& url) : Resource(url) {}
    SplatResource(const SplatResource& other) : Resource(other), _data(other._data) {}

    QString getType() const override { return "Splat"; }

    // All accessors return empty values until isLoaded().
    size_t count() const { return _data ? _data->count : 0; }
    gpu::BufferPointer dataBuffer() const { return _data ? _data->dataBuffer : gpu::BufferPointer(); }
    Positions positions() const { return _data ? _data->positions : Positions(); }
    // Percentile-trimmed bounds of the splat centres: the natural dimensions for scale-to-fit.
    AABox naturalBounds() const { return _data ? _data->naturalBounds : AABox(); }
    // Untrimmed bounds of the splat centres.
    AABox rawBounds() const { return _data ? _data->rawBounds : AABox(); }
    // Centre of naturalBounds(); the renderer pre-translates by its negation.
    glm::vec3 center() const { return _data ? _data->naturalBounds.calcCenter() : glm::vec3(0.0f); }

protected:
    void downloadFinished(const QByteArray& data) override;

    Q_INVOKABLE void setSplatData(SplatDataPointer data);

private:
    SplatDataPointer _data;
};

/// Stores cached splat clouds. Client-only: registered by interface with DependencyManager::set<SplatCache>().
class SplatCache : public ResourceCache, public Dependency {
    Q_OBJECT
    SINGLETON_DEPENDENCY

public:
    SplatResource::Pointer getSplat(const QUrl& url);

protected:
    QSharedPointer<Resource> createResource(const QUrl& url) override;
    QSharedPointer<Resource> createResourceCopy(const QSharedPointer<Resource>& resource) override;

private:
    SplatCache();
    virtual ~SplatCache() = default;
};

#endif // overte_SplatCache_h
