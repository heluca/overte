//
//  ScriptModuleMapV8.h
//  libraries/script-engine/src/v8
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

/// @addtogroup ScriptEngine
/// @{

#ifndef overte_ScriptModuleMapV8_h
#define overte_ScriptModuleMapV8_h

#include <functional>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QtCore/QHash>
#include <QtCore/QString>
#include <QtCore/QUrl>

#include "v8.h"

/// [V8] One ES module of an engine's module map.
struct ScriptModuleRecordV8 {
    enum class State { Fetching, Compiled, Failed };

    State state { State::Fetching };
    /// Resolved URL as imported: the map key, the ScriptOrigin resource name and import.meta.url
    QUrl url;
    int epoch { 0 };
    v8::Global<v8::Module> module;
    /// Specifier -> resolved URL for each static import, filled when a loader walks the record
    QHash<QString, QUrl> resolvedImports;

    /// Why the record is Failed. A fetch failure is reported at each importer's line, a compile failure at its own.
    QString error;
    bool fetchFailed { false };
    QString errorFileName;
    int errorLine { -1 };

    /// Called once the record leaves Fetching, so that loaders share a fetch that is in flight
    std::vector<std::function<void()>> waiters;
};

using ScriptModuleRecordV8Pointer = std::shared_ptr<ScriptModuleRecordV8>;

/// [V8] Per-isolate map of ES modules, keyed by (epoch, resolved URL). Records live until the engine is destroyed.
class ScriptModuleMapV8 {
public:
    ScriptModuleRecordV8Pointer find(int epoch, const QUrl& url) const;
    /// Finds the record of a compiled module, as V8 hands it to the resolve and import.meta callbacks
    ScriptModuleRecordV8Pointer find(v8::Isolate* isolate, v8::Local<v8::Module> module) const;
    /// Adds a Fetching record; there must not be one for (epoch, url) yet
    ScriptModuleRecordV8Pointer insert(int epoch, const QUrl& url);
    /// Stores the compiled module in the record and makes it findable by module
    void setCompiled(v8::Isolate* isolate, const ScriptModuleRecordV8Pointer& record, v8::Local<v8::Module> module);

private:
    using Key = std::pair<int, QString>;
    std::map<Key, ScriptModuleRecordV8Pointer> _records;
    std::unordered_multimap<int, ScriptModuleRecordV8Pointer> _byIdentityHash;
};

#endif  // overte_ScriptModuleMapV8_h

/// @}
