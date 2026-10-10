//
//  ScriptModuleMapV8.cpp
//  libraries/script-engine/src/v8
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptModuleMapV8.h"

ScriptModuleRecordV8Pointer ScriptModuleMapV8::find(int epoch, const QUrl& url) const {
    auto found = _records.find(Key(epoch, url.toString()));
    return found == _records.end() ? ScriptModuleRecordV8Pointer() : found->second;
}

ScriptModuleRecordV8Pointer ScriptModuleMapV8::find(v8::Isolate* isolate, v8::Local<v8::Module> module) const {
    // Identity hashes are not unique, so each candidate is compared by handle
    auto range = _byIdentityHash.equal_range(module->GetIdentityHash());
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second->module.Get(isolate) == module) {
            return it->second;
        }
    }
    return ScriptModuleRecordV8Pointer();
}

ScriptModuleRecordV8Pointer ScriptModuleMapV8::insert(int epoch, const QUrl& url) {
    auto record = std::make_shared<ScriptModuleRecordV8>();
    record->url = url;
    record->epoch = epoch;
    auto inserted = _records.emplace(Key(epoch, url.toString()), record);
    Q_ASSERT(inserted.second);
    return record;
}

void ScriptModuleMapV8::setCompiled(v8::Isolate* isolate, const ScriptModuleRecordV8Pointer& record,
                                    v8::Local<v8::Module> module) {
    record->module.Reset(isolate, module);
    record->state = ScriptModuleRecordV8::State::Compiled;
    _byIdentityHash.emplace(module->GetIdentityHash(), record);
}
